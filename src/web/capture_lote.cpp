// src/web/capture_lote.cpp
#include "capture_lote.h"
#include "capture_common.h"
#include "web_common.h"
#include "globals.h"
#include "files_utils.h"

#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>
#include <vector>

// Externals (asegúrate están declaradas en globals.h)
extern volatile bool captureMode;
extern volatile bool captureBatchMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

extern volatile bool awaitingSelfRegister;
extern String currentSelfRegUID;
extern unsigned long awaitingSinceMs;
extern std::vector<SelfRegSession> selfRegSessions;

// (en algunos ficheros se usa currentSelfRegToken; declarar extern por si existe en globals)
extern String currentSelfRegToken;

// Small helpers
static String htmlEscape(const String &s) {
  String r;
  r.reserve(s.length());
  for (size_t i = 0; i < (size_t)s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      case '\'': r += "&#39;"; break;
      default: r += c;
    }
  }
  return r;
}
static String jsEscape(const String &s) {
  String r;
  r.reserve(s.length() * 2);
  for (size_t i = 0; i < (size_t)s.length(); ++i) {
    char c = s[i];
    if (c == '\\') { r += "\\\\"; }
    else if (c == '\'') { r += "\\'"; }
    else if (c == '\n') { r += "\\n"; }
    else if (c == '\r') { r += "\\r"; }
    else r += c;
  }
  return r;
}

static String computeScheduleBaseMat() {
  String scheduleOwner = currentScheduledMateria();
  String scheduleBaseMat;
  int idx = scheduleOwner.indexOf("||");
  if (idx < 0) { scheduleBaseMat = scheduleOwner; scheduleBaseMat.trim(); }
  else { scheduleBaseMat = scheduleOwner.substring(0, idx); scheduleBaseMat.trim(); }
  return scheduleBaseMat;
}

// New helper: try append uid to capture queue but refuse if it's a teacher.
// Returns true if appended, false if not (and in that case logs denial).
static bool appendUidToCaptureQueueIfStudent(const String &uid) {
  if (uid.length() == 0) return false;
  // If it's a teacher, do not allow adding to batch
  String tr = findTeacherByUID(uid);
  if (tr.length() > 0) {
    // Log denied and notify
    String recDenied = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"MAESTRO_NO_BATCH\"";
    appendLineToFile(DENIED_FILE, recDenied);
    addNotification(uid, String(""), String(""), String("Intento de captura por lote de tarjeta registrada como maestro (no permitido)."));
    #ifdef USE_DISPLAY
    showTemporaryRedMessage("Maestro - no permitido en Lote", 2000);
    #endif
    return false;
  }
  // fallback to existing append function (assumed available)
  return appendUidToCaptureQueue(uid);
}

// -------------------- Page --------------------
void capture_lote_page() {
  // Optional return_to param (from students page)
  String return_to = "/";
  if (server.hasArg("return_to")) {
    String rt = server.arg("return_to"); rt.trim();
    if (rt.length() && rt[0] == '/') return_to = rt;
  }

  // Clear capture queue file at page load (fresh start)
  clearCaptureQueueFile();

  captureMode = true;
  captureBatchMode = true;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  #ifdef USE_DISPLAY
  showCaptureMode(true, false);
  #endif

  String scheduleBaseMat = computeScheduleBaseMat();

  // Prepare server-side options for global select if needed
  String coursesOptionsHtml = "";
  if (scheduleBaseMat.length() == 0) {
    auto courses = loadCourses();
    for (size_t i = 0; i < courses.size(); ++i) {
      String label = courses[i].materia;
      if (courses[i].profesor.length()) label += " (" + courses[i].profesor + ")";
      coursesOptionsHtml += "<option value='" + htmlEscape(courses[i].materia) + "'>" + htmlEscape(label) + "</option>";
    }
  }

  String html = htmlHeader("Capturar - Batch");
  html += "<div class='card'><h2>Batch capture</h2>";
  html += "<p class='small'>Acerque varias tarjetas; las UIDs quedarán en una cola. Revise los datos a la derecha y luego 'Terminar y Guardar'.</p>";

  html += "<div style='display:flex;gap:12px;align-items:flex-start;'>";
  html += "<div style='width:260px;display:flex;flex-direction:column;gap:8px;'>";

  // Borrar última (amarillo)
  html += "<form method='POST' action='/capture_remove_last' style='display:inline;margin-bottom:6px;'><button class='btn btn-yellow' type='submit'>Borrar última</button></form>";

  // Cancel (POST /cancel_capture)
  html += "<form method='POST' action='/cancel_capture' style='display:inline;margin-bottom:6px;' onsubmit='return confirm(\"Cancelar y limpiar cola? Esto borrará los UIDs en la cola.\")'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  html += "<button class='btn btn-red' type='submit'>Cancelar / Limpiar Cola</button></form>";

  // Terminar y Guardar - hidden materia
  html += "<form id='finishForm' method='POST' action='/capture_finish' style='display:inline;margin-top:6px;' onsubmit='return confirm(\"Terminar y guardar las entradas para los UIDs en la cola?\")'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  if (scheduleBaseMat.length() > 0) {
    html += "<input type='hidden' id='batch_materia' name='materia' value='" + htmlEscape(scheduleBaseMat) + "'>";
    html += "<button id='finishBtn' class='btn btn-green' type='submit'>Terminar y Guardar</button>";
  } else {
    html += "<input type='hidden' id='batch_materia' name='materia' value=''>";
    html += "<div style='font-size:12px;color:#555;margin-top:6px;'>Seleccione la materia usando el control de arriba (se aplicará a todas las filas)</div>";
    html += "<button id='finishBtn' class='btn btn-green' type='submit' disabled>Terminar y Guardar</button>";
  }
  html += "</form>";

  html += "</div>"; // left column

  html += "<div style='flex:1;min-width:320px;'>";
  if (scheduleBaseMat.length() == 0) {
    html += "<div style='margin-bottom:8px;display:flex;gap:8px;align-items:center;'>";
    html += "<label style='font-weight:600;'>Materia (aplica a todo el lote):</label>";
    html += "<select id='global_materia_select' style='min-width:220px;'><option value=''>-- Seleccionar --</option>";
    html += coursesOptionsHtml;
    html += "</select>";
    html += "</div>";
  } else {
    html += "<div style='margin-bottom:8px;color:#0b1220;font-weight:600;'>Materia en horario: " + htmlEscape(scheduleBaseMat) + "</div>";
  }

  html += "<p>Cola actual: <span id='queue_count'>0</span> UID(s).</p>";
  html += "<div id='banners_container' style='min-height:0;'></div>";
  html += "<div id='queue_list' style='background:#f5f7fb;padding:8px;border-radius:8px;min-height:120px;margin-top:8px;color:#0b1220;'>Cargando...</div>";
  html += "</div>";

  html += "</div></div>" + htmlFooter();

  // Client JS
  String optionsJs = jsEscape(coursesOptionsHtml);
  String scheduleFlag = scheduleBaseMat.length() ? "true" : "false";

  html += R"rawliteral(
    <script>
    var scheduleHasMateria = )rawliteral";
  html += scheduleFlag;
  html += R"rawliteral(;
    var selectedMateria = '';
    function setGlobalMateriaValue(val) {
      selectedMateria = val || '';
      var hidden = document.getElementById('batch_materia');
      if (hidden) hidden.value = selectedMateria;
      var finishBtn = document.getElementById('finishBtn');
      if (finishBtn) finishBtn.disabled = (!selectedMateria || selectedMateria.trim() == '');
    }
    function pollQueue() {
      fetch('/capture_batch_poll')
        .then(r=>r.json())
        .then(j=>{
          var cntEl = document.getElementById('queue_count');
          if (cntEl) cntEl.textContent = j.uids ? j.uids.length : 0;
          var bc = document.getElementById('banners_container');
          if (j.awaiting) {
            if (bc && !document.getElementById('batch_yellow_msg')) {
              var y = document.createElement('div'); y.id='batch_yellow_msg';
              y.style.marginBottom='8px'; y.style.padding='8px'; y.style.borderRadius='6px'; y.style.background='#fff8d6';
              y.style.color='#111'; y.style.fontWeight='700'; y.style.textAlign='center';
              y.innerHTML='Atención: Registrando nuevo usuario. No pasar tarjeta hasta terminar el registro.';
              bc.appendChild(y);
            }
            var ex = document.getElementById('batch_yellow_msg');
            if (ex) ex.innerHTML = 'Atención: Registrando nuevo usuario (UID: ' + (j.awaiting_uid||'') + '). No pasar tarjeta hasta terminar el registro.';
          } else {
            var e = document.getElementById('batch_yellow_msg'); if (e) e.remove();
          }
          if (j.wrong_card) {
            if (bc && !document.getElementById('batch_red_msg')) {
              var red = document.createElement('div'); red.id='batch_red_msg';
              red.style.marginBottom='8px'; red.style.padding='8px'; red.style.borderRadius='6px'; red.style.background='#ef4444';
              red.style.color='#fff'; red.style.fontWeight='700'; red.style.textAlign='center';
              red.textContent='Espere su turno: registro en curso';
              bc.insertBefore(red, bc.firstChild);
              setTimeout(function(){ var rr = document.getElementById('batch_red_msg'); if (rr) rr.remove(); }, 2000);
            }
          }
          var list = document.getElementById('queue_list');
          if (!j.uids || j.uids.length==0) { list.innerHTML = 'No hay UIDs capturadas aún.'; }
          else {
            var html = '<table style="width:100%;border-collapse:collapse;"><tr><th style="text-align:left;padding:6px">UID</th><th style="text-align:left;padding:6px">Reg</th><th style="text-align:left;padding:6px">Nombre</th><th style="text-align:left;padding:6px">Cuenta</th><th style="text-align:left;padding:6px">Materia</th></tr>';
            for (var i=0;i<j.uids.length;i++){
              var u = j.uids[i];
              var reg = u.registered ? '✅' : '❌';
              var nm = u.name || '';
              var acc = u.account || '';
              var mat = '';
              if (scheduleHasMateria) mat = u.materia || '';
              else mat = (selectedMateria && selectedMateria.length) ? selectedMateria : (u.materia || '');
              html += '<tr><td style="padding:6px;border-top:1px solid #ddd;">' + (u.uid||'') + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + reg + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + (nm||'') + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + (acc||'') + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + (mat||'') + '</td></tr>';
            }
            html += '</table>';
            list.innerHTML = html;
          }
          if (!scheduleHasMateria) {
            var g = document.getElementById('global_materia_select');
            if (g) g.value = selectedMateria || '';
          }
          var finishBtn = document.getElementById('finishBtn');
          if (finishBtn) {
            var disable = false;
            if (j.awaiting) disable = true;
            if (!scheduleHasMateria) {
              if (!selectedMateria || selectedMateria.trim()=='') disable = true;
            }
            finishBtn.disabled = disable;
            if (disable) finishBtn.classList.remove('btn-green'); else finishBtn.classList.add('btn-green');
          }
        }).catch(e=>{});
      setTimeout(pollQueue, 900);
    }
    document.addEventListener('DOMContentLoaded', function(){
      var g = document.getElementById('global_materia_select');
      if (g) g.addEventListener('change', function(){ setGlobalMateriaValue(this.value); });
      var hidden = document.getElementById('batch_materia');
      if (hidden && hidden.value && hidden.value.trim()!='') { selectedMateria = hidden.value; }
      pollQueue();
    });
    </script>
  )rawliteral";

  server.send(200, "text/html", html);
}

// Batch poll
void capture_lote_batchPollGET() {
  auto u = readCaptureQueue();
  if (u.size() == 1 && u[0].length() == 0) u.clear();

  // compute schedule base materia once
  String scheduleBaseMat = computeScheduleBaseMat();

  bool cardTriggered = false;
  if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
    for (size_t i = 0; i < u.size(); ++i) {
      if (u[i] == currentSelfRegUID) { cardTriggered = true; break; }
    }
  }

  static unsigned long wrongCardStartTime = 0;
  static unsigned long lastShowWrongRedMs = 0;
  bool wrongCard = false;

  if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
    if (captureUID.length() > 0) {
      if (captureUID != currentSelfRegUID) {
        wrongCard = true;
        if (wrongCardStartTime == 0) wrongCardStartTime = millis();
        #ifdef USE_DISPLAY
        if (lastShowWrongRedMs == 0 || (millis() - lastShowWrongRedMs) > 2000) {
          showTemporaryRedMessage("Espere su turno: registro en curso", 2000);
          lastShowWrongRedMs = millis();
        }
        #endif
        // discard the card to avoid blocking future batches
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      } else {
        // correct card for self-register -> try append but forbid teacher
        if (appendUidToCaptureQueueIfStudent(captureUID)) {
          captureUID = "";
          captureName = "";
          captureAccount = "";
          captureDetectedAt = 0;
        }
      }
    }
  }

  if (wrongCardStartTime > 0 && (millis() - wrongCardStartTime) < 2500) {
    wrongCard = true;
  } else {
    wrongCardStartTime = 0;
    if (lastShowWrongRedMs != 0 && (millis() - lastShowWrongRedMs) > 2500) lastShowWrongRedMs = 0;
  }

  if (!awaitingSelfRegister && captureUID.length() > 0) {
    if (appendUidToCaptureQueueIfStudent(captureUID)) {
      captureUID = "";
      captureName = "";
      captureAccount = "";
      captureDetectedAt = 0;
    } else {
      // if not appended because teacher, already logged/denied by helper
      captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
    }
  }

  u = readCaptureQueue();

  String j = "{\"uids\":[";
  for (size_t i = 0; i < u.size(); ++i) {
    if (i) j += ",";
    String uid = u[i];
    String rec = findAnyUserByUID(uid);
    bool reg = rec.length() > 0;
    String name="", account="", materia="";
    if (reg) {
      auto c = parseQuotedCSVLine(rec);
      if (c.size() > 1) name = c[1];
      if (c.size() > 2) account = c[2];
      if (c.size() > 3) materia = c[3];
    }
    if (scheduleBaseMat.length() > 0) materia = scheduleBaseMat;
    else materia = String("");

    j += "{\"uid\":\"" + jsonEscape(uid) + "\","; 
    j += "\"registered\":" + String(reg ? "true" : "false") + ",";
    j += "\"name\":\"" + jsonEscape(name) + "\",";
    j += "\"account\":\"" + jsonEscape(account) + "\",";
    j += "\"materia\":\"" + jsonEscape(materia) + "\"}";
  }
  j += "],";
  j += "\"awaiting\":" + String(awaitingSelfRegister ? "true" : "false") + ",";
  j += "\"card_triggered\":" + String(cardTriggered ? "true" : "false") + ",";
  j += "\"wrong_card\":" + String(wrongCard ? "true" : "false") + ",";
  j += "\"awaiting_uid\":\"" + jsonEscape(currentSelfRegUID) + "\"";
  j += "}";

  server.send(200, "application/json", j);
}

// Pause/resume unchanged
void capture_lote_pausePOST() {
  if (captureBatchMode && captureMode) {
    captureMode = false;
    #ifdef USE_DISPLAY
    showCaptureMode(true,true);
    #endif
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "paused");
    return;
  }
  if (captureBatchMode && !captureMode) {
    captureMode = true;
    #ifdef USE_DISPLAY
    showCaptureMode(true,false);
    #endif
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "resumed");
    return;
  }
  captureMode = true;
  captureBatchMode = true;
  #ifdef USE_DISPLAY
  showCaptureMode(true,false);
  #endif
  server.sendHeader("Location", "/capture_batch");
  server.send(303, "text/plain", "started");
}

void capture_lote_removeLastPOST() {
  auto q = readCaptureQueue();
  if (q.size() == 0) {
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "queue empty");
    return;
  }
  q.pop_back();
  writeCaptureQueue(q);
  String rt = "/";
  if (server.hasArg("return_to")) rt = server.arg("return_to");
  server.sendHeader("Location", rt.length() ? rt : String("/capture_batch"));
  server.send(303, "text/plain", "removed last");
}

void capture_lote_generateLinksPOST() {
  auto lines = readCaptureQueue();
  if (lines.size() == 0) {
    server.sendHeader("Location", "/capture");
    server.send(303, "text/plain", "queue empty");
    return;
  }

  std::vector<String> urls;
  for (auto &ln : lines) {
    String uid = ln; uid.trim();
    if (uid.length() == 0) continue;
    uint32_t r = (uint32_t)esp_random();
    uint32_t m = (uint32_t)millis();
    char buf[32];
    snprintf(buf, sizeof(buf), "%08X%08X", r, m);
    SelfRegSession s;
    s.token = String(buf);
    s.uid = uid;
    s.createdAtMs = millis();
    s.ttlMs = 5UL * 60UL * 1000UL;
    s.materia = String();
    selfRegSessions.push_back(s);
    urls.push_back(String("/self_register?token=") + s.token);
  }

  clearCaptureQueueFile();

  String html = htmlHeader("Links de Auto-registro");
  html += "<div class='card'><h2>Links generados</h2>";
  html += "<p class='small'>Estos enlaces expirarán en 5 minutos. Entregue el QR o enlace al alumno para que complete su registro.</p>";
  html += "<ul>";
  for (auto &u : urls) html += "<li><a href='" + u + "'>" + u + "</a></li>";
  html += "</ul>";
  html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture'>Volver</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// Finish batch: now also upserts USERS_FILE rows to add materia to users (or creates user rows)
void capture_lote_finishPOST() {
  auto q = readCaptureQueue();
  if (q.size() == 0) {
    captureMode = false; captureBatchMode = false;
    #ifdef USE_DISPLAY
    showCaptureMode(false,false);
    #endif
    String rt = server.hasArg("return_to") ? server.arg("return_to") : String("/capture_batch");
    server.sendHeader("Location", rt);
    server.send(303, "text/plain", "nothing");
    return;
  }

  if (awaitingSelfRegister) {
    String html = htmlHeader("No se puede terminar");
    html += "<div class='card'><h3 style='color:red;'>No se puede terminar: hay un registro en curso. Espere a que termine.</h3>";
    html += "<p><a class='btn btn-blue' href='/capture_batch'>Volver</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // Determine chosen materia: if supplied use it; else try schedule
  String chosenMateria;
  if (server.hasArg("materia")) {
    chosenMateria = server.arg("materia"); chosenMateria.trim();
  }
  if (chosenMateria.length() == 0) {
    chosenMateria = computeScheduleBaseMat();
  }
  if (chosenMateria.length() == 0) {
    String html = htmlHeader("Materia requerida");
    html += "<div class='card'><h3 style='color:red;'>Seleccione una materia antes de terminar el lote.</h3>";
    html += "<p><a class='btn btn-blue' href='/capture_batch'>Volver</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // Process queue: for each UID, append ATT and ensure USERS_FILE has a row for chosenMateria
  for (auto &uid : q) {
    uid.trim();
    if (uid.length() == 0) continue;

    // If UID belongs to a teacher, skip and mark denied
    String trec = findTeacherByUID(uid);
    if (trec.length() > 0) {
      String recDenied = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"MAESTRO_NO_BATCH_FINISH\"";
      appendLineToFile(DENIED_FILE, recDenied);
      addNotification(uid, String(""), String(""), String("Intento de finalizar lote con tarjeta de maestro (no permitido)."));
      continue;
    }

    // find first user row (if any)
    String rec = findAnyUserByUID(uid);
    if (rec.length() > 0) {
      // parse to get name/account
      auto c = parseQuotedCSVLine(rec);
      String name = (c.size() > 1 ? c[1] : "");
      String account = (c.size() > 2 ? c[2] : "");
      // If user doesn't yet have this materia recorded, append a new USERS_FILE row with same name/account/materia
      if (!existsUserUidMateria(uid, chosenMateria)) {
        String userRow = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMateria + "\"," + "\"" + nowISO() + "\"";
        appendLineToFile(USERS_FILE, userRow);
      }
      // append attendance
      String att = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMateria + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, att);
    } else {
      // user not registered: create user row with empty name/account and chosenMateria, plus attendance or denied?
      // We'll create user row and then write attendance
      String userRow = "\"" + uid + "\"," + "\"\"," + "\"\"," + "\"" + chosenMateria + "\"," + "\"" + nowISO() + "\"";
      appendLineToFile(USERS_FILE, userRow);
      String att = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"\"," + "\"\"," + "\"" + chosenMateria + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, att);
      // Optionally notify admin that a new user was auto-created with blank data
      addNotification(uid, String(""), String(""), String("Batch capture: usuario no registrado. Se creó fila nueva (sin nombre/cuenta)."));
    }
  }

  clearCaptureQueueFile();
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif

  String rt = server.hasArg("return_to") ? server.arg("return_to") : String("/capture");
  server.sendHeader("Location", rt);
  server.send(303, "text/plain", "finished");
}

// CANCEL handler: limpiar cola y limpiar estado de captura y self-register para evitar tarjeta "pegada"
void capture_lote_cancelPOST() {
  // store old waiting UID, then clear sessions that reference it
  String oldWaitingUID = currentSelfRegUID;

  // limpiar la cola y estado de captura
  clearCaptureQueueFile();
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  // limpiar flags de self-register y remover sessions pendientes del UID
  if (oldWaitingUID.length()) {
    for (int i = (int)selfRegSessions.size() - 1; i >= 0; --i) {
      if (selfRegSessions[i].uid == oldWaitingUID) {
        selfRegSessions.erase(selfRegSessions.begin() + i);
      }
    }
  }
  awaitingSelfRegister = false;
  currentSelfRegUID = "";
  awaitingSinceMs = 0;
  currentSelfRegToken = "";

  // regresar a modo normal (sin captura)
  captureMode = false;
  captureBatchMode = false;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif

  // respetar return_to si viene
  String rt = "/";
  if (server.hasArg("return_to")) rt = server.arg("return_to");
  server.sendHeader("Location", rt.length() ? rt : String("/"));
  server.send(303, "text/plain", "cancelled");
}
