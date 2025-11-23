// src/web/capture_lote.cpp
#include "capture_lote.h"
#include "capture_common.h"
#include "web_common.h"
#include "globals.h"
#include "files_utils.h"

#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>

// Externals
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

// Helper: obtain scheduleBaseMat (same approach as before)
static String computeScheduleBaseMat() {
  String scheduleOwner = currentScheduledMateria();
  String scheduleBaseMat;
  int idx = scheduleOwner.indexOf("||");
  if (idx < 0) { scheduleBaseMat = scheduleOwner; scheduleBaseMat.trim(); }
  else { scheduleBaseMat = scheduleOwner.substring(0, idx); scheduleBaseMat.trim(); }
  return scheduleBaseMat;
}

void capture_lote_page() {
  // Optional return_to param (from students page)
  String return_to = "/";
  if (server.hasArg("return_to")) {
    String rt = server.arg("return_to"); rt.trim();
    if (rt.length() && rt[0] == '/') return_to = rt;
  }

  // Clear capture queue file at page load
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

  String html = htmlHeader("Capturar - Batch");
  html += "<div class='card'><h2>Batch capture</h2>";
  html += "<p class='small'>Acerca varias tarjetas; las UIDs quedarán en una cola. Revise los datos a la derecha y luego 'Terminar y Guardar'.</p>";

  html += "<div style='display:flex;gap:12px;align-items:flex-start;'>";
  html += "<div style='width:220px;display:flex;flex-direction:column;gap:8px;'>";

  // Borrar última (amarillo)
  html += "<form method='POST' action='/capture_remove_last' style='display:inline'><button class='btn btn-yellow' type='submit'>Borrar última</button></form>";

  // Cancel (POST /cancel_capture) - agregar return_to hidden
  html += "<form method='POST' action='/cancel_capture' style='display:inline' onsubmit='return confirm(\"Cancelar y limpiar cola? Esto borrará los UIDs en la cola.\")'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  html += "<button class='btn btn-red' type='submit'>Cancelar / Limpiar Cola</button></form>";

  // Terminar y Guardar - si schedule no tiene materia mostramos select
  html += "<form id='finishForm' method='POST' action='/capture_finish' style='display:inline' onsubmit='return confirm(\"Terminar y guardar las entradas para los UIDs en la cola?\")'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  if (scheduleBaseMat.length() > 0) {
    html += "<input type='hidden' name='materia' value='" + scheduleBaseMat + "'>";
    html += "<button id='finishBtn' class='btn btn-green' type='submit'>Terminar y Guardar</button>";
  } else {
    // No hay materia en el horario — forzamos seleccionar una
    auto courses = loadCourses();
    html += "<label>Asignar materia para este lote:</label><br>";
    html += "<select id='batch_materia' name='materia' required>";
    html += "<option value=''>-- Seleccionar materia --</option>";
    for (auto &c : courses) html += "<option value='" + c.materia + "'>" + c.materia + " (" + c.profesor + ")</option>";
    html += "</select><br>";
    html += "<button id='finishBtn' class='btn btn-green' type='submit' disabled>Terminar y Guardar</button>";
    // JS will enable button when a materia is chosen and no awaitingSelfRegister
  }
  html += "</form>";

  html += "</div>";

  html += "<div style='flex:1;min-width:260px;'>";
  html += "<p>Cola actual: <span id='queue_count'>0</span> UID(s).</p>";
  html += "<div id='banners_container' style='min-height:0;'></div>";
  html += "<div id='queue_list' style='background:#f5f7fb;padding:8px;border-radius:8px;min-height:120px;margin-top:8px;color:#0b1220;'>Cargando...</div>";
  html += "</div>";
  html += "</div>";
  html += "</div>" + htmlFooter();

  // Client JS: polling + preventing finish while awaiting self-register + enable/disable finish button according to materia selection
  html += R"rawliteral(
    <script>
    var redTimer = null;
    var fastPolling = false;

    function showYellowBanner(uid) {
      var bc = document.getElementById('banners_container');
      if(!bc) return;
      var existing = document.getElementById('batch_yellow_msg');
      if(!existing) {
        var y = document.createElement('div');
        y.id = 'batch_yellow_msg';
        y.style.marginBottom = '8px';
        y.style.padding = '8px';
        y.style.borderRadius = '6px';
        y.style.background = '#fff8d6';
        y.style.color = '#111';
        y.style.fontWeight = '700';
        y.style.textAlign = 'center';
        y.innerHTML = 'Atención: Registrando nuevo usuario. No pasar tarjeta hasta terminar el registro.';
        bc.appendChild(y);
      } else {
        if(uid && uid.length) existing.innerHTML = 'Atención: Registrando nuevo usuario (UID: ' + uid + '). No pasar tarjeta hasta terminar el registro.';
      }
    }

    function hideYellowBanner() {
      var e = document.getElementById('batch_yellow_msg');
      if(e) e.remove();
    }

    function showRedBannerTemporarily() {
      var bc = document.getElementById('banners_container');
      if(!bc) return;
      var existing = document.getElementById('batch_red_msg');
      if(!existing) {
        var redDiv = document.createElement('div');
        redDiv.id = 'batch_red_msg';
        redDiv.style.marginBottom = '8px';
        redDiv.style.padding = '8px';
        redDiv.style.borderRadius = '6px';
        redDiv.style.background = '#ef4444';
        redDiv.style.color = '#ffffff';
        redDiv.style.fontWeight = '700';
        redDiv.style.textAlign = 'center';
        redDiv.textContent = 'Espere su turno: registro en curso';
        bc.insertBefore(redDiv, bc.firstChild);
      }
      if(redTimer) clearTimeout(redTimer);
      redTimer = setTimeout(function(){
        var e = document.getElementById('batch_red_msg');
        if(e) e.remove();
        redTimer = null;
      }, 2000);
    }

    function pollQueue(){
      fetch('/capture_batch_poll')
        .then(r=>r.json())
        .then(j=>{
          var cntEl = document.getElementById('queue_count');
          var list = document.getElementById('queue_list');
          if(cntEl) cntEl.textContent = j.uids ? j.uids.length : 0;

          if (j.awaiting) {
            showYellowBanner(j.awaiting_uid || '');
          } else {
            hideYellowBanner();
          }

          if (j.wrong_card) {
            showRedBannerTemporarily();
            fastPolling = true;
            setTimeout(()=>{ fastPolling = false; }, 2000);
          }

          if(!j.uids || j.uids.length==0) {
            list.innerHTML = 'No hay UIDs capturadas aún.';
          } else {
            var html = '<table style="width:100%;border-collapse:collapse;"><tr><th style="text-align:left;padding:6px">UID</th><th style="text-align:left;padding:6px">Reg</th><th style="text-align:left;padding:6px">Nombre</th><th style="text-align:left;padding:6px">Cuenta</th><th style="text-align:left;padding:6px">Materia</th></tr>';
            for(var i=0;i<j.uids.length;i++){
              var u = j.uids[i];
              var reg = u.registered ? '✅' : '❌';
              var nm = u.name || '';
              var acc = u.account || '';
              var mat = u.materia || '';
              html += '<tr><td style="padding:6px;border-top:1px solid #ddd;">' + u.uid + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + reg + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + nm + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + acc + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + mat + '</td></tr>';
            }
            html += '</table>';
            list.innerHTML = html;
          }

          // Disable finish button if awaitingSelfRegister is true or if materia needed and not selected
          var finishBtn = document.getElementById('finishBtn');
          var matSel = document.getElementById('batch_materia');
          if (finishBtn) {
            var disable = false;
            if (j.awaiting) disable = true;
            if (matSel && (!matSel.value || matSel.value.trim()=='')) disable = true;
            finishBtn.disabled = disable;
            if (disable) finishBtn.classList.remove('btn-green'); else finishBtn.classList.add('btn-green');
          }
        })
        .catch(e=>{
          fastPolling = false;
        });

      if (fastPolling) setTimeout(pollQueue, 300);
      else setTimeout(pollQueue, 1000);
    }
    pollQueue();

    // Enable finish button when a materia is selected (if selection exists)
    document.addEventListener('DOMContentLoaded', function(){
      var sel = document.getElementById('batch_materia');
      if (sel) {
        sel.addEventListener('change', function(){
          var finishBtn = document.getElementById('finishBtn');
          if (!finishBtn) return;
          if (sel.value && sel.value.trim()!='') finishBtn.disabled = false;
          else finishBtn.disabled = true;
        });
      }
    });

    window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e){} });
    </script>
  )rawliteral";

  server.send(200, "text/html", html);
}

// Batch poll (mejor sincronización y wrong_card timing)
void capture_lote_batchPollGET() {
  auto u = readCaptureQueue();
  if (u.size() == 1 && u[0].length() == 0) u.clear();

  bool cardTriggered = false;
  if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
    for (size_t i = 0; i < u.size(); ++i) {
      if (u[i] == currentSelfRegUID) { cardTriggered = true; break; }
    }
  }

  static unsigned long wrongCardStartTime = 0;
  static unsigned long lastShowWrongRedMs = 0; // <<-- evita re-dibujar mensaje rojo cada poll
  bool wrongCard = false;

  if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
    if (captureUID.length() > 0) {
      if (captureUID != currentSelfRegUID) {
        wrongCard = true;
        // solo iniciar temporizador la primera vez que detectamos wrong card o si ya expiró
        if (wrongCardStartTime == 0) {
          wrongCardStartTime = millis();
          Serial.println("DEBUG: WRONG_CARD activado - Iniciando temporizador");
        }
        // Mostrar mensaje rojo en display, pero evitar invocarlo en cada poll (evita 'pegado')
        #ifdef USE_DISPLAY
        if (lastShowWrongRedMs == 0 || (millis() - lastShowWrongRedMs) > 2000) {
          showTemporaryRedMessage("Espere su turno: registro en curso", 2000);
          lastShowWrongRedMs = millis();
        }
        #endif

        // descartar la tarjeta leída (para no acumular)
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      } else {
        Serial.println("DEBUG: Tarjeta correcta - agregando a cola");
        if (appendUidToCaptureQueue(captureUID)) {
          captureUID = "";
          captureName = "";
          captureAccount = "";
          captureDetectedAt = 0;
        }
      }
    }
  }

  // Mantener wrongCard como true por una ventana corta (2.5s) después del inicio
  if (wrongCardStartTime > 0 && (millis() - wrongCardStartTime) < 2500) {
    wrongCard = true;
    Serial.println("DEBUG: WRONG_CARD mantenido activo por temporizador");
  } else {
    wrongCardStartTime = 0;
    // resetear lastShowWrongRedMs si ya pasaron >2.5s para permitir futuros mensajes
    if (lastShowWrongRedMs != 0 && (millis() - lastShowWrongRedMs) > 2500) lastShowWrongRedMs = 0;
  }

  if (!awaitingSelfRegister && captureUID.length() > 0) {
    Serial.println("DEBUG: No hay self-register - agregando tarjeta normalmente: " + captureUID);
    if (appendUidToCaptureQueue(captureUID)) {
      captureUID = "";
      captureName = "";
      captureAccount = "";
      captureDetectedAt = 0;
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

  Serial.println("DEBUG: Enviando JSON - wrong_card: " + String(wrongCard ? "true" : "false"));
  server.send(200, "application/json", j);
}

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
  // Respect return_to if provided
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

// Finish batch: requerir materia si no hay scheduleBaseMat, y bloquear si awaitingSelfRegister
void capture_lote_finishPOST() {
  // If queue empty, just redirect back (respect return_to)
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

  // If there is an awaiting self-register, disallow finishing
  if (awaitingSelfRegister) {
    // Redirect back with alert (simple)
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
    // try compute from schedule
    chosenMateria = computeScheduleBaseMat();
  }
  if (chosenMateria.length() == 0) {
    // cannot finish without materia
    String html = htmlHeader("Materia requerida");
    html += "<div class='card'><h3 style='color:red;'>Seleccione una materia antes de terminar el lote.</h3>";
    html += "<p><a class='btn btn-blue' href='/capture_batch'>Volver</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // Process queue
  String scheduleBaseMat = chosenMateria;
  for (auto &uid : q) {
    uid.trim();
    if (uid.length() == 0) continue;
    String rec = findAnyUserByUID(uid);
    if (rec.length() > 0) {
      auto c = parseQuotedCSVLine(rec);
      String name = (c.size() > 1 ? c[1] : "");
      String account = (c.size() > 2 ? c[2] : "");
      String userMat = (c.size() > 3 ? c[3] : "");
      String chosenMat = userMat;
      if (scheduleBaseMat.length() > 0) chosenMat = scheduleBaseMat;
      if (chosenMat.length() == 0) chosenMat = userMat;

      String att = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMat + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, att);
    } else {
      String note = "Batch capture: UID no registrada: " + uid;
      addNotification(uid, String(""), String(""), note);
      appendLineToFile(DENIED_FILE, String("\"") + nowISO() + String("\",\"") + uid + String("\",\"NO REGISTRADA_BATCH\""));
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
