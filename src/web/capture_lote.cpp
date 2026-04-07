// src/web/capture_lote.cpp
#include "capture_lote.h"
#include "capture_common.h"
#include "web_common.h"
#include "globals.h"
#include "files_utils.h"
#include "db_sync.h"

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
  if (idx < 0) {
    scheduleBaseMat = scheduleOwner;
    scheduleBaseMat.trim();
  } else {
    scheduleBaseMat = scheduleOwner.substring(0, idx);
    scheduleBaseMat.trim();
  }
  return scheduleBaseMat;
}

// Función para verificar si un alumno ya está registrado en una materia específica
static bool studentExistsInMateria(const String &uid, const String &materia) {
  if (uid.length() == 0 || materia.length() == 0) return false;

  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) return false;

  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length() == 0) continue;

    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 4) {
      String uidc = c[0];
      String matc = c[3];
      if (uidc == uid && matc == materia) {
        f.close();
        return true;
      }
    }
  }
  f.close();
  return false;
}

// uidExistsInTeachers/uidExistsInUsers
static bool uidExistsInTeachers(const String &uid) {
  if (uid.length() == 0) return false;
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 1 && c[0] == uid) { f.close(); return true; }
  }
  f.close();
  return false;
}

static bool uidExistsInUsers(const String &uid) {
  if (uid.length() == 0) return false;
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 1 && c[0] == uid) { f.close(); return true; }
  }
  f.close();
  return false;
}

// Helper: elimina UIDs de maestro desde el vector y devuelve lista de maestros eliminados (detalles)
static std::vector<String> filterOutTeachersFromList(std::vector<String> &list) {
  std::vector<String> removed;
  for (int i = (int)list.size() - 1; i >= 0; --i) {
    String uid = list[i];
    if (uidExistsInTeachers(uid)) {
      String teacherName = "";
      File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
      if (ft) {
        while (ft.available()) {
          String l = ft.readStringUntil('\n'); l.trim();
          if (!l.length()) continue;
          auto c = parseQuotedCSVLine(l);
          if (c.size() >= 2 && c[0] == uid) { teacherName = c[1]; break; }
        }
        ft.close();
      }
      String entry = uid + (teacherName.length() ? String(" - ") + teacherName : "");
      removed.push_back(entry);
      list.erase(list.begin() + i);
    }
  }
  return removed;
}

// Sync helpers
static void syncBatchAlumnoToOracle(const String &uid, const String &name, const String &account, const String &materia, const String &createdAt) {
  if (name.length() == 0 || account.length() == 0) return;
  if (!sendAlumnoRegistro(uid, name, account, materia, createdAt)) {
    Serial.println("WARN: no se pudo sincronizar alumno del lote con Oracle");
  } else {
    Serial.println("DB_SYNC: alumno del lote sincronizado correctamente");
  }

  if (!sendAsistencia(createdAt, uid, name, account, materia, "captura")) {
    Serial.println("WARN: no se pudo sincronizar asistencia del lote");
  } else {
    Serial.println("DB_SYNC: asistencia del lote sincronizada correctamente");
  }
}

// -------------------- Page --------------------
void capture_lote_page() {
  String return_to = "/students";
  if (server.hasArg("return_to")) {
    String rt = server.arg("return_to");
    rt.trim();
    if (rt.length() && rt[0] == '/') return_to = rt;
  }

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
  html += "<p class='small' style='color:#b00020;'><strong>Nota:</strong> Las tarjetas de maestros serán rechazadas automáticamente.</p>";

  html += "<div style='display:flex;gap:12px;align-items:flex-start;'>";
  html += "<div style='width:300px;display:flex;flex-direction:column;gap:8px;'>";

  html += "<form method='POST' action='/capture_remove_last' style='display:inline;margin-bottom:6px;'><button class='btn btn-yellow' type='submit'>Borrar última</button></form>";

  html += "<form method='POST' action='/cancel_capture' style='display:inline;margin-bottom:6px;' onsubmit='return confirm(\"Cancelar y limpiar cola? Esto borrará los UIDs en la cola.\")'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  html += "<button class='btn btn-red' type='submit'>Cancelar / Limpiar Cola</button></form>";

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

  html += "</div>";

  html += "<div style='flex:1;min-width:340px;'>";
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

    function removeUid(uid) {
      if (!uid) return;
      fetch('/capture_remove_uid', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: 'uid=' + encodeURIComponent(uid)
      }).then(function(r){
        setTimeout(pollQueue, 250);
      }).catch(function(){ setTimeout(pollQueue, 500); });
    }

    var teacherBannerTimer = null;
    function pollQueue() {
      fetch('/capture_batch_poll')
        .then(r=>r.json())
        .then(j=>{
          var cntEl = document.getElementById('queue_count');
          if (cntEl) cntEl.textContent = j.uids ? j.uids.length : 0;

          var bc = document.getElementById('banners_container');
          bc.innerHTML = '';

          if (j.awaiting) {
            var y = document.createElement('div');
            y.style.marginBottom='8px'; y.style.padding='8px'; y.style.borderRadius='6px'; y.style.background='#fff8d6';
            y.style.color='#111'; y.style.fontWeight='700'; y.style.textAlign='center';
            y.innerHTML='Atención: Registrando nuevo usuario (UID: ' + (j.awaiting_uid||'') + '). No pasar tarjeta hasta terminar el registro.';
            bc.appendChild(y);
          }

          if (j.wrong_card) {
            var red = document.createElement('div');
            red.style.marginBottom='8px'; red.style.padding='8px'; red.style.borderRadius='6px'; red.style.background='#ef4444';
            red.style.color='#fff'; red.style.fontWeight='700'; red.style.textAlign='center';
            red.textContent = 'Espere su turno: registro en curso';
            bc.appendChild(red);
          }

          if (j.teacher_blocked) {
            var existing = document.getElementById('teacher_blocked_banner');
            if (!existing) {
              var tb = document.createElement('div');
              tb.id = 'teacher_blocked_banner';
              tb.style.marginBottom='8px'; tb.style.padding='8px'; tb.style.borderRadius='6px'; tb.style.background='#ffeaa7';
              tb.style.color='#111'; tb.style.fontWeight='700'; tb.style.textAlign='center';
              tb.textContent = j.teacher_blocked_message || 'Tarjeta de maestro rechazada';
              bc.appendChild(tb);
            } else {
              bc.appendChild(existing);
            }

            if (teacherBannerTimer) clearTimeout(teacherBannerTimer);
            teacherBannerTimer = setTimeout(function(){
              var el = document.getElementById('teacher_blocked_banner');
              if (el && el.parentNode) el.parentNode.removeChild(el);
              teacherBannerTimer = null;
            }, 5000);
          } else {
            var el = document.getElementById('teacher_blocked_banner');
            if (el && el.parentNode) el.parentNode.removeChild(el);
            if (teacherBannerTimer) { clearTimeout(teacherBannerTimer); teacherBannerTimer = null; }
          }

          var list = document.getElementById('queue_list');
          if (!j.uids || j.uids.length==0) {
            list.innerHTML = 'No hay UIDs capturadas aún.';
          } else {
            var html = '<table style="width:100%;border-collapse:collapse;"><tr><th style="text-align:left;padding:6px">UID</th><th style="text-align:left;padding:6px">Reg</th><th style="text-align:left;padding:6px">Nombre</th><th style="text-align:left;padding:6px">Cuenta</th><th style="text-align:left;padding:6px">Materia</th><th style="text-align:center;padding:6px">Acción</th></tr>';
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
              html += '<td style="padding:6px;border-top:1px solid #ddd;">' + (mat||'') + '</td>';
              html += '<td style="padding:6px;border-top:1px solid #ddd;text-align:center;">';
              html += '<button style="padding:4px 8px;border-radius:4px;border:none;background:#ef4444;color:#fff;cursor:pointer;" onclick="removeUid(\'' + (u.uid||'').replace(/'/g,'\\\'') + '\')">Eliminar</button>';
              html += '</td></tr>';
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
            if (!scheduleHasMateria) {
              if (!selectedMateria || selectedMateria.trim()=='') disable = true;
            }
            finishBtn.disabled = disable;
            if (disable) finishBtn.classList.remove('btn-green');
            else finishBtn.classList.add('btn-green');
          }
        }).catch(e=>{
        });
      setTimeout(pollQueue, 900);
    }
    document.addEventListener('DOMContentLoaded', function(){
      var g = document.getElementById('global_materia_select');
      if (g) g.addEventListener('change', function(){ setGlobalMateriaValue(this.value); });
      var hidden = document.getElementById('batch_materia');
      if (hidden && hidden.value && hidden.value.trim()!='') {
        selectedMateria = hidden.value;
      }
      pollQueue();
    });
    </script>
  )rawliteral";

  server.send(200, "text/html", html);
}

// Batch poll - BLOQUEO TOTAL INMEDIATO: NO PROCESA NADA DE MAESTROS
void capture_lote_batchPollGET() {
  auto u = readCaptureQueue();
  if (u.size() == 1 && u[0].length() == 0) u.clear();

  String scheduleBaseMat = computeScheduleBaseMat();

  std::vector<String> removedTeachers = filterOutTeachersFromList(u);
  if (!removedTeachers.empty()) {
    writeCaptureQueue(u);
    for (auto &t : removedTeachers) {
      String recDenied = "\"" + nowISO() + "\"," + "\"" + t + "\"," + "\"MAESTRO_OMITIDO_EN_COLA\"";
      appendLineToFile(DENIED_FILE, recDenied);
      addNotification(t, String(""), String(""), String("Tarjeta de maestro detectada y omitida en captura por lote: ") + t);
    }
  }

  bool cardTriggered = false;
  if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
    for (size_t i = 0; i < u.size(); ++i) {
      if (u[i] == currentSelfRegUID) { cardTriggered = true; break; }
    }
  }

  static unsigned long wrongCardStartTime = 0;
  static unsigned long lastShowWrongRedMs = 0;
  static unsigned long teacherBlockedTime = 0;

  bool wrongCard = false;
  bool teacherBlocked = false;
  String teacherBlockedMessage = "";

  if (captureUID.length() > 0) {
    if (uidExistsInTeachers(captureUID)) {
      if (teacherBlockedTime == 0 || (millis() - teacherBlockedTime) > 5000UL) {
        teacherBlockedTime = millis();
        String recDenied = "\"" + nowISO() + "\"," + "\"" + captureUID + "\"," + "\"MAESTRO_BLOQUEADO_LOTE\"";
        appendLineToFile(DENIED_FILE, recDenied);

        String teacherName = "";
        File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
        if (ft) {
          while (ft.available()) {
            String l = ft.readStringUntil('\n');
            l.trim();
            if (!l.length()) continue;
            auto c = parseQuotedCSVLine(l);
            if (c.size() >= 2 && c[0] == captureUID) {
              teacherName = c[1];
              break;
            }
          }
          ft.close();
        }

        String notificationMsg = "Tarjeta de maestro BLOQUEADA en captura por lote: " +
                                (teacherName.length() ? teacherName : "Sin nombre") +
                                " (UID: " + captureUID + ")";
        addNotification(captureUID, String(""), String(""), notificationMsg);
      }

      teacherBlocked = true;
      teacherBlockedMessage = "Esta tarjeta está registrada como maestro. No puede registrarse en captura por lote.";
      goto send_response;
    }
  }

  if (captureUID.length() > 0 && !uidExistsInTeachers(captureUID)) {
    if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
      if (captureUID != currentSelfRegUID) {
        wrongCard = true;
        if (wrongCardStartTime == 0) wrongCardStartTime = millis();
        #ifdef USE_DISPLAY
        if (lastShowWrongRedMs == 0 || (millis() - lastShowWrongRedMs) > 2000) {
          showTemporaryRedMessage("Espere su turno: registro en curso", 2000);
          lastShowWrongRedMs = millis();
        }
        #endif
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      } else {
        if (appendUidToCaptureQueue(captureUID)) {
          captureUID = "";
          captureName = "";
          captureAccount = "";
          captureDetectedAt = 0;
        }
      }
    } else if (!awaitingSelfRegister) {
      if (appendUidToCaptureQueue(captureUID)) {
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      }
    }
  }

  if (wrongCardStartTime > 0 && (millis() - wrongCardStartTime) < 2500) {
    wrongCard = true;
  } else {
    wrongCardStartTime = 0;
  }

  if (teacherBlockedTime > 0 && (millis() - teacherBlockedTime) < 5000UL) {
    teacherBlocked = true;
  } else {
    teacherBlockedTime = 0;
  }

  if (lastShowWrongRedMs != 0 && (millis() - lastShowWrongRedMs) > 2500) {
    lastShowWrongRedMs = 0;
  }

send_response:
  u = readCaptureQueue();

  String j = "{\"uids\":[";
  bool first = true;
  for (size_t i = 0; i < u.size(); ++i) {
    String uid = u[i];

    if (uidExistsInTeachers(uid)) {
      appendLineToFile(DENIED_FILE, String("\"") + nowISO() + String("\",\"") + uid + String("\",\"MAESTRO_OMITIDO_RESPUESTA\""));
      continue;
    }

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

    if (!first) j += ",";
    first = false;
    j += "{\"uid\":\"" + jsonEscape(uid) + "\",";
    j += "\"registered\":" + String(reg ? "true" : "false") + ",";
    j += "\"name\":\"" + jsonEscape(name) + "\",";
    j += "\"account\":\"" + jsonEscape(account) + "\",";
    j += "\"materia\":\"" + jsonEscape(materia) + "\"}";
  }
  j += "],";
  j += "\"awaiting\":" + String(awaitingSelfRegister ? "true" : "false") + ",";
  j += "\"awaiting_uid\":\"" + jsonEscape(currentSelfRegUID) + "\",";
  j += "\"card_triggered\":" + String(cardTriggered ? "true" : "false") + ",";
  j += "\"wrong_card\":" + String(wrongCard ? "true" : "false") + ",";
  j += "\"teacher_blocked\":" + String(teacherBlocked ? "true" : "false") + ",";
  if (teacherBlocked) {
    j += "\"teacher_blocked_message\":\"" + jsonEscape(teacherBlockedMessage) + "\"";
  } else {
    j += "\"teacher_blocked_message\":\"\"";
  }
  j += "}";

  server.send(200, "application/json", j);
}

void capture_lote_pausePOST() {
  if (captureBatchMode && captureMode) {
    captureMode = false;
    #ifdef USE_DISPLAY
    showCaptureMode(true, true);
    #endif
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "paused");
    return;
  }
  if (captureBatchMode && !captureMode) {
    captureMode = true;
    #ifdef USE_DISPLAY
    showCaptureMode(true, false);
    #endif
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "resumed");
    return;
  }
  captureMode = true;
  captureBatchMode = true;
  #ifdef USE_DISPLAY
  showCaptureMode(true, false);
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
  String rt = "/capture_batch";
  if (server.hasArg("return_to")) rt = server.arg("return_to");
  server.sendHeader("Location", rt.length() ? rt : String("/capture_batch"));
  server.send(303, "text/plain", "removed last");
}

void capture_lote_removeUidPOST() {
  if (!server.hasArg("uid")) {
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "missing uid");
    return;
  }
  String uid = server.arg("uid");
  uid.trim();
  if (uid.length() == 0) {
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "empty uid");
    return;
  }
  auto q = readCaptureQueue();
  bool found = false;
  for (int i = 0; i < (int)q.size(); ++i) {
    if (q[i] == uid) {
      q.erase(q.begin() + i);
      found = true;
      break;
    }
  }
  if (found) writeCaptureQueue(q);
  String rt = "/capture_batch";
  if (server.hasArg("return_to")) rt = server.arg("return_to");
  server.sendHeader("Location", rt.length() ? rt : String("/capture_batch"));
  server.send(303, "text/plain", "removed uid");
}

void capture_lote_generateLinksPOST() {
  auto lines = readCaptureQueue();
  if (lines.size() == 0) {
    server.sendHeader("Location", "/capture");
    server.send(303, "text/plain", "queue empty");
    return;
  }

  std::vector<String> teacherList;
  for (int i = (int)lines.size() - 1; i >= 0; --i) {
    String uid = lines[i];
    if (uidExistsInTeachers(uid)) {
      String teacherName = "";
      File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
      if (ft) {
        while (ft.available()) {
          String l = ft.readStringUntil('\n');
          l.trim();
          if (!l.length()) continue;
          auto c = parseQuotedCSVLine(l);
          if (c.size() >= 2 && c[0] == uid) { teacherName = c[1]; break; }
        }
        ft.close();
      }
      teacherList.push_back(uid + (teacherName.length() ? String(" - ") + teacherName : ""));
      lines.erase(lines.begin() + i);
      appendLineToFile(DENIED_FILE, String("\"") + nowISO() + String("\",\"") + uid + String("\",\"MAESTRO_OMITIDO_GENERAR_LINKS\""));
    }
  }

  if (lines.size() == 0) {
    clearCaptureQueueFile();
    String html = htmlHeader("Error - No se generaron links");
    html += "<div class='card'><h2>No se generaron links</h2>";
    html += "<p class='small'>No se pudieron generar links porque todas las tarjetas en la cola son de maestros (no permitidos en captura por lote).</p>";
    html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture_batch'>Volver a Captura por Lote</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  std::vector<String> urls;
  for (auto &ln : lines) {
    String uid = ln;
    uid.trim();
    if (uid.length() == 0) continue;

    if (uidExistsInTeachers(uid)) {
      continue;
    }

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

  if (urls.size() == 0) {
    String html = htmlHeader("Error - No se generaron links");
    html += "<div class='card'><h2>No se generaron links</h2>";
    html += "<p class='small'>No se pudieron generar links. Revise la cola o intente nuevamente.</p>";
    html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture_batch'>Volver a Captura por Lote</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  String html = htmlHeader("Links de Auto-registro");
  html += "<div class='card'><h2>Links generados</h2>";
  html += "<p class='small'>Estos enlaces expirarán en 5 minutos. Entregue el QR o enlace al alumno para que complete su registro.</p>";
  html += "<ul>";
  for (auto &u : urls) html += "<li><a href='" + u + "'>" + u + "</a></li>";
  html += "</ul>";

  if (teacherList.size() > 0) {
    html += "<div style='margin-top:12px;padding:10px;background:#fff3cd;border:1px solid #ffeaa7;border-radius:6px;'>";
    html += "<strong>Se omitieron las siguientes tarjetas (maestros):</strong><ul>";
    for (auto &t : teacherList) html += "<li>" + htmlEscape(t) + "</li>";
    html += "</ul></div>";
  }

  html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture_batch'>Volver</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

void capture_lote_finishPOST() {
  auto q = readCaptureQueue();
  if (q.size() == 0) {
    captureMode = false;
    captureBatchMode = false;
    #ifdef USE_DISPLAY
    showCaptureMode(false, false);
    #endif
    String rt = server.hasArg("return_to") ? server.arg("return_to") : String("/capture_batch");
    server.sendHeader("Location", rt);
    server.send(303, "text/plain", "nothing");
    return;
  }

  String chosenMateria;
  if (server.hasArg("materia")) {
    chosenMateria = server.arg("materia");
    chosenMateria.trim();
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

  std::vector<String> successList;
  std::vector<String> duplicateList;

  for (auto &uid : q) {
    uid.trim();
    if (uid.length() == 0) continue;

    if (uidExistsInTeachers(uid)) {
      String recDenied = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"MAESTRO_BLOQUEADO_FINISH\"";
      appendLineToFile(DENIED_FILE, recDenied);
      continue;
    }

    if (studentExistsInMateria(uid, chosenMateria)) {
      String studentName = "";
      File fu = SPIFFS.open(USERS_FILE, FILE_READ);
      if (fu) {
        while (fu.available()) {
          String l = fu.readStringUntil('\n');
          l.trim();
          if (!l.length()) continue;
          auto c = parseQuotedCSVLine(l);
          if (c.size() >= 2 && c[0] == uid) {
            studentName = c[1];
            break;
          }
        }
        fu.close();
      }
      duplicateList.push_back(uid + " - " + (studentName.length() ? studentName : "Sin nombre"));
      continue;
    }

    String rec = findAnyUserByUID(uid);

    if (rec.length() > 0) {
      auto c = parseQuotedCSVLine(rec);
      String name = (c.size() > 1 ? c[1] : "");
      String account = (c.size() > 2 ? c[2] : "");

      String userRow = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMateria + "\"," + "\"" + nowISO() + "\"";
      appendLineToFile(USERS_FILE, userRow);

      successList.push_back(uid + " - " + (name.length() ? name : "Sin nombre"));

      String att = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMateria + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, att);

      syncBatchAlumnoToOracle(uid, name, account, chosenMateria, nowISO());
    } else {
      String userRow = "\"" + uid + "\"," + "\"\"," + "\"\"," + "\"" + chosenMateria + "\"," + "\"" + nowISO() + "\"";
      appendLineToFile(USERS_FILE, userRow);

      successList.push_back(uid + " - Sin nombre");

      String att = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"\"," + "\"\"," + "\"" + chosenMateria + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, att);

      addNotification(uid, String(""), String(""), String("Batch capture: usuario no registrado. Se creó fila nueva (sin nombre/cuenta)."));
    }
  }

  clearCaptureQueueFile();
  captureMode = false;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false, false);
  #endif

  String html = htmlHeader("Informe de Captura por Lote");
  html += "<div class='card' style='max-width:900px;margin:0 auto;'>";
  html += "<h2 style='margin-top:0;'>Informe de Captura por Lote</h2>";
  html += "<p class='small'>Materia: <strong>" + htmlEscape(chosenMateria) + "</strong></p>";
  html += "<div style='display:flex;gap:16px;align-items:center;margin-top:8px;'>";
  html += "<div style='background:#e6ffed;padding:12px;border-radius:8px;flex:1;border:1px solid #c7f0d4;'>";
  html += "<div style='font-size:18px;font-weight:700;'>✅ Registros exitosos</div>";
  html += "<div style='font-size:22px;margin-top:6px;'>" + String(successList.size()) + "</div>";
  html += "</div>";
  html += "<div style='background:#fff7e6;padding:12px;border-radius:8px;flex:1;border:1px solid #ffe6b8;'>";
  html += "<div style='font-size:18px;font-weight:700;'>⚠️ Duplicados</div>";
  html += "<div style='font-size:22px;margin-top:6px;'>" + String(duplicateList.size()) + "</div>";
  html += "</div>";
  html += "</div>";

  if (duplicateList.size() > 0) {
    html += "<div style='margin-top:16px;padding:12px;background:#fff3cd;border-radius:8px;border:1px solid #ffeaa7;'>";
    html += "<h4 style='margin-top:0;color:#856404;'>Alumnos ya registrados en esta materia</h4>";
    html += "<ul style='margin:0 0 0 18px;'>";
    for (auto &dup : duplicateList) {
      html += "<li>" + htmlEscape(dup) + "</li>";
    }
    html += "</ul>";
    html += "<p class='small' style='margin:8px 0 0 0;color:#856404;'>Estos alumnos no fueron registrados nuevamente.</p>";
    html += "</div>";
  }

  if (successList.size() > 0) {
    html += "<div style='margin-top:16px;padding:12px;background:#e6ffed;border-radius:8px;border:1px solid #c7f0d4;'>";
    html += "<h4 style='margin-top:0;color:#0a7020;'>Registros guardados </h4>";
    html += "<ul style='margin:0 0 0 18px;'>";
    int shown = 0;
    for (auto &ok : successList) {
      if (shown++ >= 10) break;
      html += "<li>" + htmlEscape(ok) + "</li>";
    }
    if (successList.size() > 10) html += "<li>... y " + String(successList.size() - 10) + " más</li>";
    html += "</ul>";
    html += "</div>";
  }

  String rt = server.hasArg("return_to") ? server.arg("return_to") : String("/students");
  html += "<div style='margin-top:20px;display:flex;gap:10px;'>";
  html += "<a class='btn btn-blue' href='" + htmlEscape(rt) + "'>Volver a Alumnos</a>";
  html += "<a class='btn btn-green' href='/capture_batch'>Nueva Captura por Lote</a>";
  html += "</div>";
  html += "</div>" + htmlFooter();

  server.send(200, "text/html", html);
}

void capture_lote_cancelPOST() {
  String oldWaitingUID = currentSelfRegUID;

  clearCaptureQueueFile();
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

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

  captureMode = false;
  captureBatchMode = false;
  #ifdef USE_DISPLAY
  showCaptureMode(false, false);
  #endif

  String rt = "/students";
  if (server.hasArg("return_to")) rt = server.arg("return_to");
  server.sendHeader("Location", rt.length() ? rt : String("/students"));
  server.send(303, "text/plain", "cancelled");
}