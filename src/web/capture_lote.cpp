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
extern String currentSelfRegToken;

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

static String computeScheduleBaseMat() {
  String scheduleOwner = currentScheduledMateria();
  String scheduleBaseMat;
  int idx = scheduleOwner.indexOf("||");
  if (idx < 0) { scheduleBaseMat = scheduleOwner; scheduleBaseMat.trim(); }
  else { scheduleBaseMat = scheduleOwner.substring(0, idx); scheduleBaseMat.trim(); }
  return scheduleBaseMat;
}

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

// Función IDÉNTICA A CAPTURE_INDIVIDUAL: verificar si UID pertenece a un maestro
static bool uidExistsInTeachers(const String &uid) {
  if (uid.length() == 0) return false;
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
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
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 1 && c[0] == uid) { f.close(); return true; }
  }
  f.close();
  return false;
}

// NUEVA FUNCIÓN ROBUSTA: Verificación y bloqueo completo de maestros
static bool isTeacherAndBlockCompletely(const String &uid) {
  if (uid.length() == 0) return false;
  
  if (uidExistsInTeachers(uid)) {
    Serial.println("🚫 BLOQUEO COMPLETO: Maestro detectado - UID: " + uid);
    
    // Log denied
    String recDenied = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"MAESTRO_BLOQUEADO_COMPLETO_LOTE\"";
    appendLineToFile(DENIED_FILE, recDenied);
    
    // Notificación
    String teacherName = "";
    File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
    if (ft) {
      while (ft.available()) {
        String l = ft.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
        auto c = parseQuotedCSVLine(l);
        if (c.size() >= 2 && c[0] == uid) {
          teacherName = c[1];
          break;
        }
      }
      ft.close();
    }
    
    String notificationMsg = "Tarjeta de maestro BLOQUEADA COMPLETAMENTE en captura por lote: " + 
                            (teacherName.length() ? teacherName : "Sin nombre") + 
                            " (UID: " + uid + ")";
    addNotification(uid, String(""), String(""), notificationMsg);
    
    #ifdef USE_DISPLAY
    showTemporaryRedMessage("MAESTRO - BLOQUEADO", 3000);
    #endif
    
    return true;
  }
  return false;
}

// -------------------- Page --------------------
void capture_lote_page() {
  String return_to = "/";
  if (server.hasArg("return_to")) {
    String rt = server.arg("return_to"); rt.trim();
    if (rt.length() && rt[0] == '/') return_to = rt;
  }

  // Clear capture queue file at page load (fresh start)
  clearCaptureQueueFile();

  // RESET CRÍTICO: Limpiar estado de self-register para evitar conflictos
  awaitingSelfRegister = false;
  currentSelfRegUID = "";
  awaitingSinceMs = 0;
  currentSelfRegToken = "";

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
  html += "<p class='small' style='color:#b00020;'><strong>Nota:</strong> Las tarjetas de maestros serán rechazadas automáticamente.</p>";

  html += "<div style='display:flex;gap:12px;align-items:flex-start;'>";
  html += "<div style='width:260px;display:flex;flex-direction:column;gap:8px;'>";

  // Borrar última (amarillo)
  html += "<form method='POST' action='/capture_remove_last' style='display:inline;margin-bottom:6px;'><button class='btn btn-yellow' type='submit'>Borrar última</button></form>";

  // Cancel (POST /cancel_capture)
  html += "<form method='POST' action='/cancel_capture' style='display:inline;margin-bottom:6px;' onsubmit='return confirm(\"Cancelar y limpiar cola? Esto borrará los UIDs en la cola.\")'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  html += "<button class='btn btn-red' type='submit'>Cancelar / Limpiar Cola</button></form>";

  // Terminar y Guardar
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
          
          // Limpiar todos los mensajes previos
          bc.innerHTML = '';

          // Mensaje informativo amarillo durante self-register
          if (j.awaiting) {
            var y = document.createElement('div'); 
            y.style.marginBottom='8px'; y.style.padding='8px'; y.style.borderRadius='6px'; y.style.background='#fff8d6';
            y.style.color='#111'; y.style.fontWeight='700'; y.style.textAlign='center';
            y.innerHTML='Atención: Registrando nuevo usuario (UID: ' + (j.awaiting_uid||'') + '). No pasar tarjeta hasta terminar el registro.';
            bc.appendChild(y);
          }

          // Mensaje rojo sincronizado con display cuando hay tarjeta incorrecta
          if (j.wrong_card) {
            var red = document.createElement('div'); 
            red.style.marginBottom='8px'; red.style.padding='8px'; red.style.borderRadius='6px'; red.style.background='#ef4444';
            red.style.color='#fff'; red.style.fontWeight='700'; red.style.textAlign='center';
            red.textContent = 'Espere su turno: registro en curso';
            bc.appendChild(red);
          }

          // Mensaje para tarjetas de maestro rechazadas - COMPORTAMIENTO IDÉNTICO A CAPTURE_INDIVIDUAL
          if (j.teacher_blocked) {
            var teacherMsg = document.createElement('div'); 
            teacherMsg.style.marginBottom='8px'; teacherMsg.style.padding='8px'; teacherMsg.style.borderRadius='6px'; teacherMsg.style.background='#ffeaa7';
            teacherMsg.style.color='#111'; teacherMsg.style.fontWeight='700'; teacherMsg.style.textAlign='center';
            teacherMsg.textContent = j.teacher_blocked_message || 'Tarjeta de maestro rechazada';
            bc.appendChild(teacherMsg);
          }

          var list = document.getElementById('queue_list');
          if (!j.uids || j.uids.length==0) { 
            list.innerHTML = 'No hay UIDs capturadas aún.'; 
          } else {
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
            if (!scheduleHasMateria) {
              if (!selectedMateria || selectedMateria.trim()=='') disable = true;
            }
            finishBtn.disabled = disable;
            if (disable) finishBtn.classList.remove('btn-green'); 
            else finishBtn.classList.add('btn-green');
          }
        }).catch(e=>{});
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

// NUEVA FUNCIÓN: appendUidToCaptureQueue con bloqueo de maestros
static bool appendUidToCaptureQueueWithTeacherBlock(const String &uid) {
  // BLOQUEO COMPLETO: Si es maestro, NO agregar a la cola
  if (isTeacherAndBlockCompletely(uid)) {
    Serial.println("🚫 BLOQUEO EN appendUidToCaptureQueue: Maestro detectado - NO se agrega a cola");
    return false;
  }
  
  // Solo agregar a la cola si NO es maestro
  return appendUidToCaptureQueue(uid);
}

// Batch poll - BLOQUEO COMPLETO EN EL ORIGEN
void capture_lote_batchPollGET() {
  auto u = readCaptureQueue();
  if (u.size() == 1 && u[0].length() == 0) u.clear();

  String scheduleBaseMat = computeScheduleBaseMat();

  bool cardTriggered = false;
  if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
    for (size_t i = 0; i < u.size(); ++i) {
      if (u[i] == currentSelfRegUID) { cardTriggered = true; break; }
    }
  }

  // Variables de estado
  static unsigned long wrongCardStartTime = 0;
  static unsigned long lastShowWrongRedMs = 0;
  static unsigned long teacherBlockedTime = 0;
  
  bool wrongCard = false;
  bool teacherBlocked = false;
  String teacherBlockedMessage = "";

  // BLOQUEO COMPLETO EN EL ORIGEN - VERIFICACIÓN INMEDIATA
  if (captureUID.length() > 0) {
    // VERIFICACIÓN MÁS TEMPRANA POSIBLE: ¿Es maestro? - BLOQUEO TOTAL
    if (isTeacherAndBlockCompletely(captureUID)) {
      teacherBlocked = true;
      teacherBlockedTime = millis();
      teacherBlockedMessage = "Esta tarjeta ya está registrada como maestro. No puede registrarse en captura por lote.";
      
      // LIMPIEZA INMEDIATA Y TOTAL - NO DEJAR RASTRO
      captureUID = "";
      captureName = "";
      captureAccount = "";
      captureDetectedAt = 0;
      
      // SALIR INMEDIATAMENTE - NO PROCESAR NADA MÁS
      goto send_response;
    }
  }

  // SOLO SI NO ES MAESTRO, CONTINUAR CON EL PROCESAMIENTO NORMAL
  if (captureUID.length() > 0 && !uidExistsInTeachers(captureUID)) {
    // Verificar si estamos en modo self-register
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
        // Tarjeta correcta para self-register
        if (appendUidToCaptureQueueWithTeacherBlock(captureUID)) {
          captureUID = "";
          captureName = "";
          captureAccount = "";
          captureDetectedAt = 0;
        }
      }
    }
    // Procesamiento normal (sin self-register activo)
    else if (!awaitingSelfRegister) {
      // IMPORTANTE: En captura por lote, NO activar self-register automáticamente
      // Solo agregar a la cola para procesamiento posterior
      if (appendUidToCaptureQueueWithTeacherBlock(captureUID)) {
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      }
    }
  }

  // Control de tiempos para mensajes
  if (wrongCardStartTime > 0 && (millis() - wrongCardStartTime) < 2500) {
    wrongCard = true;
  } else {
    wrongCardStartTime = 0;
  }

  if (teacherBlockedTime > 0 && (millis() - teacherBlockedTime) < 3000) {
    teacherBlocked = true;
  } else {
    teacherBlockedTime = 0;
  }

  if (lastShowWrongRedMs != 0 && (millis() - lastShowWrongRedMs) > 2500) {
    lastShowWrongRedMs = 0;
  }

send_response:
  // Leer la cola actualizada y FILTRAR MAESTROS (doble verificación)
  u = readCaptureQueue();
  std::vector<String> filteredUids;
  for (auto &uid : u) {
    if (!uidExistsInTeachers(uid)) {
      filteredUids.push_back(uid);
    } else {
      Serial.println("🚫 FILTRADO DOBLE: Maestro encontrado en cola - UID: " + uid);
    }
  }

  // Construir respuesta JSON - SOLO UIDs que NO son maestros
  String j = "{\"uids\":[";
  bool first = true;
  for (size_t i = 0; i < filteredUids.size(); ++i) {
    String uid = filteredUids[i];
    
    if (!first) j += ",";
    first = false;
    
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
  
  // BLOQUEO DE MAESTROS
  j += "\"teacher_blocked\":" + String(teacherBlocked ? "true" : "false") + ",";
  if (teacherBlocked) {
    j += "\"teacher_blocked_message\":\"" + jsonEscape(teacherBlockedMessage) + "\"";
  } else {
    j += "\"teacher_blocked_message\":\"\"";
  }
  
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

// GENERATE LINKS - BLOQUEO COMPLETO EN EL ORIGEN
void capture_lote_generateLinksPOST() {
  auto lines = readCaptureQueue();
  if (lines.size() == 0) {
    server.sendHeader("Location", "/capture");
    server.send(303, "text/plain", "queue empty");
    return;
  }

  std::vector<String> urls;
  int maestrosBloqueados = 0;
  
  for (auto &ln : lines) {
    String uid = ln; uid.trim();
    if (uid.length() == 0) continue;
    
    // BLOQUEO COMPLETO: No generar links para maestros
    if (isTeacherAndBlockCompletely(uid)) {
      Serial.println("🚫 BLOQUEO EN GENERATE LINKS: UID maestro detectado y omitido - " + uid);
      maestrosBloqueados++;
      continue; // NO generar QR para maestros - SALTEAR COMPLETAMENTE
    }
    
    // Solo generar QR para UIDs que NO son maestros
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

  // Limpiar la cola después de generar links
  clearCaptureQueueFile();

  // Si no se generaron URLs porque todos eran maestros, mostrar mensaje y redirigir
  if (urls.size() == 0) {
    String html = htmlHeader("Error - No se generaron links");
    html += "<div class='card'><h2>No se generaron links</h2>";
    if (maestrosBloqueados > 0) {
      html += "<p class='small'>No se pudieron generar links porque " + String(maestrosBloqueados) + " tarjeta(s) en la cola son de maestros (no permitidos en captura por lote).</p>";
    } else {
      html += "<p class='small'>No se pudieron generar links porque la cola está vacía o contiene UIDs inválidos.</p>";
    }
    html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture_batch'>Volver a Captura por Lote</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  String html = htmlHeader("Links de Auto-registro");
  html += "<div class='card'><h2>Links generados</h2>";
  if (maestrosBloqueados > 0) {
    html += "<p class='small' style='color:#b00020;'>Se omitieron " + String(maestrosBloqueados) + " tarjeta(s) de maestros (no permitidas en captura por lote).</p>";
  }
  html += "<p class='small'>Estos enlaces expirarán en 5 minutos. Entregue el QR o enlace al alumno para que complete su registro.</p>";
  html += "<ul>";
  for (auto &u : urls) html += "<li><a href='" + u + "'>" + u + "</a></li>";
  html += "</ul>";
  html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture'>Volver</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// Finish batch: BLOQUEO COMPLETO EN EL ORIGEN
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

  // Vectores para el informe final
  std::vector<String> successList;    // Registros exitosos
  std::vector<String> duplicateList;  // Ya estaban registrados en la materia
  std::vector<String> teacherList;    // Son maestros (no permitidos)
  std::vector<String> newUserList;    // Nuevos usuarios creados

  // Process queue: for each UID, append ATT and ensure USERS_FILE has a row for chosenMateria
  for (auto &uid : q) {
    uid.trim();
    if (uid.length() == 0) continue;

    // BLOQUEO COMPLETO: Si UID pertenece a un maestro, NO PROCESAR
    if (isTeacherAndBlockCompletely(uid)) {
      teacherList.push_back(uid + " - MAESTRO BLOQUEADO");
      continue;
    }

    // VERIFICAR SI EL ALUMNO YA ESTÁ REGISTRADO EN ESTA MATERIA
    if (studentExistsInMateria(uid, chosenMateria)) {
      String studentName = "";
      File fu = SPIFFS.open(USERS_FILE, FILE_READ);
      if (fu) {
        while (fu.available()) {
          String l = fu.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
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

    // find first user row (if any)
    String rec = findAnyUserByUID(uid);
    
    if (rec.length() > 0) {
      auto c = parseQuotedCSVLine(rec);
      String name = (c.size() > 1 ? c[1] : "");
      String account = (c.size() > 2 ? c[2] : "");
      
      // Añadir fila en USERS_FILE para esta materia
      String userRow = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMateria + "\"," + "\"" + nowISO() + "\"";
      appendLineToFile(USERS_FILE, userRow);
      
      successList.push_back(uid + " - " + (name.length() ? name : "Sin nombre"));
      
      // append attendance
      String att = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMateria + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, att);
    } else {
      // user not registered: create user row with empty name/account and chosenMateria
      String userRow = "\"" + uid + "\"," + "\"\"," + "\"\"," + "\"" + chosenMateria + "\"," + "\"" + nowISO() + "\"";
      appendLineToFile(USERS_FILE, userRow);
      
      newUserList.push_back(uid);
      
      String att = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"\"," + "\"\"," + "\"" + chosenMateria + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, att);
      
      addNotification(uid, String(""), String(""), String("Batch capture: usuario no registrado. Se creó fila nueva (sin nombre/cuenta)."));
    }
  }

  clearCaptureQueueFile();
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif

  // Mostrar informe final
  String html = htmlHeader("Informe de Captura por Lote");
  html += "<div class='card'><h2>Informe de Captura por Lote</h2>";
  html += "<p class='small'>Materia: <strong>" + htmlEscape(chosenMateria) + "</strong></p>";
  html += "<p>Total de UIDs procesados: <strong>" + String(q.size()) + "</strong></p>";
  
  // Mostrar resumen por categorías
  html += "<div style='margin:15px 0; padding:10px; background:#f8f9fa; border-radius:5px;'>";
  html += "<h3 style='margin-top:0;'>Resumen:</h3>";
  html += "<p>✅ Registros exitosos: <strong>" + String(successList.size()) + "</strong></p>";
  html += "<p>🆕 Nuevos usuarios creados: <strong>" + String(newUserList.size()) + "</strong></p>";
  html += "<p>⚠️  Duplicados (ya registrados): <strong>" + String(duplicateList.size()) + "</strong></p>";
  html += "<p>🚫 Maestros (no permitidos): <strong>" + String(teacherList.size()) + "</strong></p>";
  html += "</div>";
  
  // Mostrar detalles de duplicados si los hay
  if (duplicateList.size() > 0) {
    html += "<div style='margin:15px 0; padding:10px; background:#fff3cd; border:1px solid #ffeaa7; border-radius:5px;'>";
    html += "<h4 style='color:#856404; margin-top:0;'>Alumnos ya registrados en esta materia:</h4>";
    html += "<ul style='margin-bottom:0;'>";
    for (auto &dup : duplicateList) {
      html += "<li>" + htmlEscape(dup) + "</li>";
    }
    html += "</ul>";
    html += "<p class='small' style='margin:8px 0 0 0; color:#856404;'>Estos alumnos no fueron registrados nuevamente.</p>";
    html += "</div>";
  }
  
  // Mostrar detalles de maestros si los hay
  if (teacherList.size() > 0) {
    html += "<div style='margin:15px 0; padding:10px; background:#f8d7da; border:1px solid #f5c6cb; border-radius:5px;'>";
    html += "<h4 style='color:#721c24; margin-top:0;'>Maestros detectados (no permitidos en lote):</h4>";
    html += "<ul style='margin-bottom:0;'>";
    for (auto &teacher : teacherList) {
      html += "<li>" + htmlEscape(teacher) + "</li>";
    }
    html += "</ul>";
    html += "</div>";
  }
  
  // Mostrar detalles de nuevos usuarios si los hay
  if (newUserList.size() > 0) {
    html += "<div style='margin:15px 0; padding:10px; background:#d1ecf1; border:1px solid #bee5eb; border-radius:5px;'>";
    html += "<h4 style='color:#0c5460; margin-top:0;'>Nuevos usuarios creados (sin nombre/cuenta):</h4>";
    html += "<ul style='margin-bottom:0;'>";
    for (auto &newUser : newUserList) {
      html += "<li>" + htmlEscape(newUser) + "</li>";
    }
    html += "</ul>";
    html += "<p class='small' style='margin:8px 0 0 0; color:#0c5460;'>Estos usuarios necesitan completar sus datos posteriormente.</p>";
    html += "</div>";
  }
  
  String rt = server.hasArg("return_to") ? server.arg("return_to") : String("/capture");
  html += "<div style='margin-top:20px;'>";
  html += "<a class='btn btn-blue' href='" + htmlEscape(rt) + "'>Volver al Inicio</a>";
  html += "<a class='btn btn-green' href='/capture_batch' style='margin-left:10px;'>Nueva Captura por Lote</a>";
  html += "</div>";
  html += "</div>" + htmlFooter();

  server.send(200, "text/html", html);
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