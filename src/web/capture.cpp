// src/web/capture.cpp
// Captura: landing (dos botones) -> individual o batch (cada uno en su propia página)

#include "capture.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>
#include "self_register.h" // para SelfRegSession y selfRegSessions

// Globals (de globals.h)
extern volatile bool captureMode;
extern volatile bool captureBatchMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// Archivo de cola (usa la constante global si la tienes, si no se usa esta ruta)
#ifndef CAPTURE_QUEUE_FILE
static const char *CAPTURE_QUEUE_FILE_LOCAL = "/capture_queue.csv";
#define CAPTURE_QUEUE_FILE CAPTURE_QUEUE_FILE_LOCAL
#endif

static String jsonEscape(const String &s) {
  String o = s;
  o.replace("\\", "\\\\");
  o.replace("\"", "\\\"");
  return o;
}

// ---------------- Helpers de cola ----------------
static std::vector<String> readCaptureQueue() {
  std::vector<String> out;
  if (!SPIFFS.exists(CAPTURE_QUEUE_FILE)) return out;
  File f = SPIFFS.open(CAPTURE_QUEUE_FILE, FILE_READ);
  if (!f) return out;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length()) out.push_back(l);
  }
  f.close();
  return out;
}

static bool appendUidToCaptureQueue(const String &uid) {
  if (uid.length() == 0) return false;
  auto q = readCaptureQueue();
  for (auto &u : q) if (u == uid) return false;
  return appendLineToFile(CAPTURE_QUEUE_FILE, uid);
}

static bool clearCaptureQueueFile() {
  if (SPIFFS.exists(CAPTURE_QUEUE_FILE)) return SPIFFS.remove(CAPTURE_QUEUE_FILE);
  return true;
}

static bool writeCaptureQueue(const std::vector<String> &q) {
  if (SPIFFS.exists(CAPTURE_QUEUE_FILE)) SPIFFS.remove(CAPTURE_QUEUE_FILE);
  if (q.size() == 0) return true;
  for (auto &u : q) {
    if (!appendLineToFile(CAPTURE_QUEUE_FILE, u)) return false;
  }
  return true;
}

// ---------------- Landing: elegir modo ----------------
void handleCapturePage() {
  String html = htmlHeader("Capturar Tarjeta");
  html += "<div class='card'><h2>Capturar Tarjeta</h2>";
  html += "<p class='small'>Acerca la tarjeta. Si ya existe en otra materia se autocompletan los campos. Seleccione un modo:</p>";
  html += "<div style='display:flex;gap:12px;justify-content:center;margin-top:18px;'>";
  html += "<a class='btn btn-blue' href='/capture_individual'>Individual</a>";
  html += "<a class='btn btn-blue' href='/capture_batch'>Batch (varias tarjetas)</a>";
  html += "</div></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// ---------------- Individual page ----------------
void handleCaptureIndividualPage() {
  captureMode = true;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  // Indicador en pantalla físico (si tienes display)
  #ifdef USE_DISPLAY
  showCaptureMode(false, false);
  #endif

  String html = htmlHeader("Capturar - Individual");
  html += "<div class='card'><h2>Captura Individual</h2>";
  html += "<p class='small'>Acerca la tarjeta. UID autocompletará los campos si existe.</p>";

  // Formulario individual
  html += "<form id='capForm' method='POST' action='/capture_confirm'>";
  html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input id='name' name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7'><br>";

  auto courses = loadCourses();
  html += "Materia (seleccionar):<br><select id='materia' name='materia'>";
  html += "<option value=''>-- Seleccionar materia --</option>";
  for (auto &c : courses) html += "<option value='" + c.materia + "'>" + c.materia + " (" + c.profesor + ")</option>";
  html += "</select><br>";

  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Confirmar</button>";
  html += "<a class='btn btn-red' href='/' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";
  html += "</form></div>" + htmlFooter();

  // JS polling para autocompletar UID
  html += R"rawliteral(
    <script>
    function pollUID(){
      fetch('/capture_poll')
        .then(r=>r.json())
        .then(j=>{
          if(j.status=='found'){
            document.getElementById('uid').value = j.uid || '';
            if(j.name) document.getElementById('name').value = j.name;
            if(j.account) document.getElementById('account').value = j.account;
          }
          setTimeout(pollUID,700);
        })
        .catch(e=>setTimeout(pollUID,1200));
    }
    pollUID();
    window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e){} });
    </script>
  )rawliteral";

  server.send(200, "text/html", html);
}

// ---------------- Batch page ----------------
void handleCaptureBatchPage() {
  // Al iniciar un nuevo batch siempre limpiar la cola
  clearCaptureQueueFile();

  // Arrancar modo batch
  captureMode = true;
  captureBatchMode = true;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  #ifdef USE_DISPLAY
  showCaptureMode(true, false);
  #endif

  String html = htmlHeader("Capturar - Batch");
  html += "<div class='card'><h2>Batch capture</h2>";
  html += "<p class='small'>Acerca varias tarjetas; las UIDs quedarán en una cola. Revise los datos a la derecha y luego 'Terminar y Guardar'.</p>";

  // layout: dos columnas
  html += "<div style='display:flex;gap:12px;align-items:flex-start;'>";

  // columna izquierda: botones (vertical)
  html += "<div style='width:220px;display:flex;flex-direction:column;gap:8px;'>";
  // Eliminamos pausa por UI, pero mantenemos el endpoint para compatibilidad
  html += "<form method='POST' action='/capture_remove_last' style='display:inline'><button class='btn btn-yellow' type='submit'>Borrar última</button></form>";
  html += "<form method='POST' action='/capture_cancel' style='display:inline' onsubmit='return confirm(\"Cancelar y limpiar cola? Esto borrará los UIDs en la cola.\")'><button class='btn btn-red' type='submit'>Cancelar / Limpiar Cola</button></form>";
  html += "<form method='POST' action='/capture_finish' style='display:inline' onsubmit='return confirm(\"Terminar y guardar las entradas para los UIDs en la cola?\")'><button class='btn btn-green' type='submit'>Terminar y Guardar</button></form>";
  html += "<div style='margin-top:16px'><a class='btn btn-blue' href='/'>Inicio</a></div>";
  html += "</div>";

  // columna derecha: lista / tabla de UIDs con info
  html += "<div style='flex:1;min-width:260px;'>";
  html += "<p>Cola actual: <span id='queue_count'>0</span> UID(s).</p>";
  html += "<div id='queue_list' style='background:#f5f7fb;padding:8px;border-radius:8px;min-height:120px;margin-top:8px;color:#0b1220;'>Cargando...</div>";
  html += "</div>";

  html += "</div>"; // fin layout
  html += "</div>" + htmlFooter();

  // JS para poll de cola (ahora espera JSON con objetos)
  html += R"rawliteral(
    <script>
    function pollQueue(){
      fetch('/capture_batch_poll')
        .then(r=>r.json())
        .then(j=>{
          var cntEl = document.getElementById('queue_count');
          var list = document.getElementById('queue_list');
          if(cntEl) cntEl.textContent = j.uids ? j.uids.length : 0;

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

          // mostrar mensaje si hay un self-register activo (QR en display)
          var awaiting = j.awaiting;
          if(awaiting){
            var msg = '<div style="margin-top:8px;padding:8px;background:#fff8d6;border-radius:6px;color:#111">Atención: Se está capturando un alumno (UID: ' + (j.awaiting_uid||'') + '). Escanee el QR para completar el registro.</div>';
            list.innerHTML = msg + list.innerHTML;
          }
        })
        .catch(e=>{ /*console.log(e)*/; });
      setTimeout(pollQueue,1000);
    }
    pollQueue();
    // evitar que la pestaña se cierre sin avisar
    window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e){} });
    </script>
  )rawliteral";

  server.send(200, "text/html", html);
}

// ---------------- Polling individual ----------------
void handleCapturePoll() {
  if (captureUID.length() == 0) {
    server.send(200, "application/json", "{\"status\":\"waiting\"}");
    return;
  }
  String j = "{\"status\":\"found\",\"uid\":\"" + captureUID + "\"";
  if (captureName.length() > 0) j += ",\"name\":\"" + jsonEscape(captureName) + "\"";
  if (captureAccount.length() > 0) j += ",\"account\":\"" + jsonEscape(captureAccount) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

// ---------------- Confirm individual ----------------
void handleCaptureConfirm() {
  if (!server.hasArg("uid") || !server.hasArg("name") ||
      !server.hasArg("account") || !server.hasArg("materia")) {
    server.send(400, "text/plain", "Faltan parámetros");
    return;
  }

  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.arg("materia"); materia.trim();

  if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }

  bool ok = true;
  if (account.length() != 7) ok = false;
  for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { ok = false; break; }
  if (!ok) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  if (!courseExists(materia)) {
    server.send(400, "text/plain", "La materia especificada no existe. Regístrela primero en Materias.");
    return;
  }

  if (existsUserUidMateria(uid, materia) || existsUserAccountMateria(account, materia)) {
    captureMode = false; captureBatchMode = false;
    String html = htmlHeader("Duplicado detectado");
    html += "<div class='card'><h3 style='color:red;'>⚠️ El estudiante ya está registrado en esta materia.</h3>";
    html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'><a href='/' class='btn btn-blue'>Inicio</a> <a href='/capture' class='btn btn-green'>Capturar otro</a></div></div>";
    html += htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  String created = nowISO();
  String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"" + created + "\"";
  if (!appendLineToFile(USERS_FILE, line)) { server.send(500, "text/plain", "Error guardando usuario"); return; }

  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura\"";
  if (!appendLineToFile(ATT_FILE, rec)) { server.send(500, "text/plain", "Error guardando attendance"); return; }

  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  String html = htmlHeader("Registrado correctamente");
  html += "<div class='card'><h3>✅ Usuario registrado correctamente.</h3>";
  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'><a href='/' class='btn btn-blue'>Inicio</a> <a href='/capture' class='btn btn-green'>Capturar otro</a></div></div>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

// ---------------- Start/Stop compatibility ----------------
void handleCaptureStartPOST() {
  captureMode = true; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif
  server.sendHeader("Location", "/capture_individual");
  server.send(303, "text/plain", "capture started");
}

void handleCaptureBatchStopPOST() {
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "batch stopped");
}

void handleCaptureStopGET() {
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "stopped");
}

// ---------------- Batch endpoints ----------------
void handleCaptureBatchPollGET() {
  auto u = readCaptureQueue();
  // Clean trailing empties
  if (u.size() == 1 && u[0].length() == 0) u.clear();

  // Build JSON array with info
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
  j += "\"awaiting_uid\":\"" + jsonEscape(currentSelfRegUID) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

// Limpiar cola (POST) -> detiene batch y limpia
void handleCaptureBatchClearPOST() {
  clearCaptureQueueFile();
  captureMode = false; captureBatchMode = false;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "cleared");
}

// Pause / Resume (toggle) - POST (lo dejamos por compatibilidad)
void handleCaptureBatchPausePOST() {
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

// Borrar la última UID agregada (POST)
void handleCaptureRemoveLastPOST() {
  auto q = readCaptureQueue();
  if (q.size() == 0) {
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "queue empty");
    return;
  }
  q.pop_back();
  writeCaptureQueue(q);
  server.sendHeader("Location", "/capture_batch");
  server.send(303, "text/plain", "removed last");
}

// Cancelar batch (Volver): limpiar cola y volver a landing (no guardar nada)
void handleCaptureCancelPOST() {
  clearCaptureQueueFile();
  captureMode = false;
  captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "cancelled");
}

// Generar links de auto-registro desde la cola (POST) - mantenido por compatibilidad
void handleCaptureGenerateLinksPOST() {
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

// ---------------- Finish batch: procesar y guardar attendance ----------------
void handleCaptureFinishPOST() {
  auto q = readCaptureQueue();
  if (q.size() == 0) {
    // nothing to do
    captureMode = false; captureBatchMode = false;
    #ifdef USE_DISPLAY
    showCaptureMode(false,false);
    #endif
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "nothing");
    return;
  }

  // Determinar materia en curso (si existe)
  String scheduleOwner = currentScheduledMateria();
  String scheduleBaseMat;
  // helper inline to extract base
  {
    int idx = scheduleOwner.indexOf("||");
    if (idx < 0) { scheduleBaseMat = scheduleOwner; scheduleBaseMat.trim(); }
    else { scheduleBaseMat = scheduleOwner.substring(0, idx); scheduleBaseMat.trim(); }
  }

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
      // crear notificación para admin
      String note = "Batch capture: UID no registrada: " + uid;
      addNotification(uid, String(""), String(""), note);
      // opcional: registrar denied
      appendLineToFile(DENIED_FILE, String("\"") + nowISO() + String("\",\"") + uid + String("\",\"NO REGISTRADA_BATCH\""));
    }
  }

  // limpiar cola y salir de modo capture
  clearCaptureQueueFile();
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif

  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "finished");
}

// ---------------- Edición desde Students (igual que antes) ----------------
// (mantengo tu implementación previa)
static String sanitizeReturnTo(const String &rt) {
  if (rt.length() > 0 && rt[0] == '/') return rt;
  return String("/students_all");
}

void handleCaptureEditPage() {
  if (!server.hasArg("uid")) { server.send(400, "text/plain", "uid required"); return; }
  String uid = server.arg("uid");
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : "/students_all";
  return_to = sanitizeReturnTo(return_to);

  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500, "text/plain", "No se pudo abrir archivo de usuarios"); return; }

  String foundName = "", foundAccount = "", foundMateria = "", foundCreated = "";
  bool found = false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length() == 0) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 3 && c[0] == uid) {
      foundName = (c.size() > 1 ? c[1] : "");
      foundAccount = (c.size() > 2 ? c[2] : "");
      foundMateria = (c.size() > 3 ? c[3] : "");
      foundCreated = (c.size() > 4 ? c[4] : nowISO());
      found = true;
      break;
    }
  }
  f.close();
  if (!found) { server.send(404, "text/plain", "Alumno no encontrado"); return; }

  String html = htmlHeader("Editar Alumno");
  html += "<div class='card'><h2>Editar Usuario</h2>";
  html += "<form method='POST' action='/capture_edit_post'>";
  html += "<input type='hidden' name='uid' value='" + uid + "'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  html += "<label>UID (no editable):</label><br>";
  html += "<input readonly style='background:#eee;' value='" + uid + "'><br><br>";
  html += "<label>Nombre:</label><br>";
  html += "<input name='name' required value='" + foundName + "'><br><br>";
  html += "<label>Cuenta:</label><br>";
  html += "<input name='account' required maxlength='7' minlength='7' value='" + foundAccount + "'><br><br>";
  auto courses2 = loadCourses();
  html += "<label>Materia:</label><br><select name='materia'>";
  html += "<option value=''>-- Ninguna --</option>";
  for (auto &c : courses2) {
    String sel = (c.materia == foundMateria) ? " selected" : "";
    html += "<option value='" + c.materia + "'" + sel + ">" + c.materia + " (" + c.profesor + ")</option>";
  }
  html += "</select><br><br>";
  html += "<label>Registrado:</label><br>";
  html += "<div style='padding:6px;background:#f5f5f5;border-radius:4px;'>" + foundCreated + "</div><br>";
  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Guardar</button>";
  html += "<a class='btn btn-red' href='" + return_to + "'>Cancelar</a>";
  html += "</div></form></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

void handleCaptureEditPost() {
  if (!server.hasArg("uid") || !server.hasArg("name") || !server.hasArg("account") || !server.hasArg("return_to")) {
    server.send(400, "text/plain", "Faltan parámetros");
    return;
  }
  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.hasArg("materia") ? server.arg("materia") : "";
  materia.trim();
  String return_to = sanitizeReturnTo(server.arg("return_to"));

  if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }
  if (name.length() == 0) { server.send(400, "text/plain", "Nombre vacío"); return; }
  if (account.length() != 7) { server.send(400, "text/plain", "Cuenta inválida"); return; }
  for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  if (materia.length() > 0 && !courseExists(materia)) { server.send(400, "text/plain", "La materia seleccionada no existe"); return; }

  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500, "text/plain", "No se pudo abrir archivo de usuarios"); return; }

  std::vector<String> lines;
  String header = f.readStringUntil('\n');
  lines.push_back(header);
  bool updated = false;

  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length() == 0) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 1 && c[0] == uid) {
      String created = (c.size() > 4 ? c[4] : nowISO());
      String mat = (materia.length() ? materia : (c.size() > 3 ? c[3] : ""));
      String newline = "\"" + uid + "\",\"" + name + "\",\"" + account + "\",\"" + mat + "\",\"" + created + "\"";
      lines.push_back(newline);
      updated = true;
    } else lines.push_back(l);
  }
  f.close();

  if (!updated) { server.send(404, "text/plain", "Alumno no encontrado"); return; }

  if (!writeAllLines(USERS_FILE, lines)) { server.send(500, "text/plain", "Error guardando usuarios"); return; }

  server.sendHeader("Location", return_to);
  server.send(303, "text/plain", "Updated");
}
