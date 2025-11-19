// src/web/capture.cpp
// Captura: landing (dos botones) -> individual o batch (cada uno en su propia página)
// Versión: UI batch dividida en 2 columnas; cola derecha muestra detalle por UID.

#include "capture.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>
#include "self_register.h" // para SelfRegSession y selfRegSessions

// Globals (de globals.h) - no redeclarar si ya están en globals.h pero para IntelliSense:
extern volatile bool captureMode;
extern volatile bool captureBatchMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

#ifndef CAPTURE_QUEUE_FILE
static const char *CAPTURE_QUEUE_FILE_LOCAL = "/capture_queue.csv";
#define CAPTURE_QUEUE_FILE CAPTURE_QUEUE_FILE_LOCAL
#endif

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

// ---------------- Individual page (sin cambios relevantes) ----------------
void handleCaptureIndividualPage() {
  captureMode = true;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  String html = htmlHeader("Capturar - Individual");
  html += "<div class='card'><h2>Captura Individual</h2>";
  html += "<p class='small'>Acerca la tarjeta. UID autocompletará los campos si existe.</p>";

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
  html += "</div></form></div>" + htmlFooter();

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

// ---------------- Batch page (NUEVA UI: 2 columnas) ----------------
void handleCaptureBatchPage() {
  captureMode = true;
  captureBatchMode = true;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  // Touch file if not exists
  if (!SPIFFS.exists(CAPTURE_QUEUE_FILE)) appendLineToFile(CAPTURE_QUEUE_FILE, String());

  String html = htmlHeader("Capturar - Batch");
  html += "<div class='card'><h2>Batch capture</h2>";
  html += "<p class='small'>Acerca varias tarjetas; las UIDs quedarán en una cola. Revise los datos a la derecha y luego 'Terminar y guardar'.</p>";

  // contenedor dos columnas
  html += "<div style='display:flex;gap:12px;align-items:flex-start;'>";
  // columna izquierda: botones verticales
  html += "<div style='flex:0 0 180px;display:flex;flex-direction:column;gap:10px;'>";

  // Pausa / Reanudar (azul)
  html += "<form method='POST' action='/capture_batch_pause' style='margin:0;'><button class='btn btn-orange' type='submit'>Pausar / Reanudar</button></form>";

  // Borrar última (amarillo)
  html += "<form method='POST' action='/capture_remove_last' style='margin:0;' onsubmit='return confirm(\"Borrar la última UID capturada?\")'><button class='btn btn-yellow' type='submit'>Borrar última</button></form>";

  // Fusionar Limpiar + Cancelar en un solo botón rojo (Eliminar cola y salir)
  html += "<form method='POST' action='/capture_cancel' style='margin:0;' onsubmit='return confirm(\"Cancelar batch y borrar la cola?\")'><button class='btn btn-red' type='submit'>Cancelar / Limpiar Cola</button></form>";

  // Terminar y guardar (verde oscuro)
  html += "<form method='POST' action='/capture_finish' style='margin:0;' onsubmit='return confirm(\"Terminar y guardar asistencia para las UIDs registradas?\")'><button class='btn btn-green' type='submit'>Terminar y Guardar</button></form>";

  html += "</div>"; // fin columna izquierda

  // columna derecha: lista con detalle
  html += "<div style='flex:1 1 auto;min-height:120px;'>";
  html += "<p>Cola actual: <span id='queue_count'>0</span> UID(s).</p>";
  html += "<div id='queue_list' style='background:#f5f7fb;padding:8px;border-radius:8px;min-height:120px;margin-top:8px;'>Cargando...</div>";
  html += "</div>"; // fin columna derecha

  html += "</div>"; // fin contenedor

  html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/'>Volver al inicio</a></p>";

  html += "</div>" + htmlFooter();

  // JS poll que obtiene lista detallada (uid + registered + name + account)
  html += R"rawliteral(
    <script>
    function pollQueue(){
      fetch('/capture_batch_poll')
        .then(r=>r.json())
        .then(j=>{
          var cntEl = document.getElementById('queue_count');
          var list = document.getElementById('queue_list');
          var u = j.uids || [];
          cntEl.textContent = u.length;
          if(u.length==0){ list.innerHTML = 'No hay UIDs capturadas aún.'; return; }
          var html = '<table style="width:100%;border-collapse:collapse;"><thead><tr><th style="text-align:left">UID</th><th>Registro</th><th>Nombre</th><th>Cuenta</th></tr></thead><tbody>';
          for(var i=0;i<u.length;i++){
            var it = u[i];
            html += '<tr style="border-bottom:1px solid #eee">';
            html += '<td style="padding:6px 8px;">' + it.uid + '</td>';
            html += '<td style="text-align:center">' + (it.registered ? '✅' : '—') + '</td>';
            html += '<td style="padding:6px 8px;">' + (it.name || '') + '</td>';
            html += '<td style="padding:6px 8px;">' + (it.account || '') + '</td>';
            html += '</tr>';
          }
          html += '</tbody></table>';
          list.innerHTML = html;
        })
        .catch(e=>{});
      setTimeout(pollQueue,900);
    }
    pollQueue();
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
  if (captureName.length() > 0) j += ",\"name\":\"" + captureName + "\"";
  if (captureAccount.length() > 0) j += ",\"account\":\"" + captureAccount + "\"";
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
  server.sendHeader("Location", "/capture_individual");
  server.send(303, "text/plain", "capture started");
}

void handleCaptureBatchStopPOST() {
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "batch stopped");
}

void handleCaptureStopGET() {
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "stopped");
}

// ---------------- Batch endpoints ----------------
void handleCaptureBatchPollGET() {
  auto u = readCaptureQueue();
  // filter empty lines
  std::vector<String> out;
  for (auto &s : u) if (s.length()) out.push_back(s);
  // Build JSON array of objects {uid, registered, name, account}
  String j = "{\"uids\":[";
  for (size_t i = 0; i < out.size(); ++i) {
    if (i) j += ",";
    String uid = out[i];
    String found = findAnyUserByUID(uid);
    bool reg = (found.length() > 0);
    String name = "";
    String account = "";
    if (reg) {
      auto c = parseQuotedCSVLine(found);
      if (c.size() > 1) name = c[1];
      if (c.size() > 2) account = c[2];
    }
    j += "{\"uid\":\"" + uid + "\",\"registered\":" + String(reg ? "true" : "false") + ",\"name\":\"" + name + "\",\"account\":\"" + account + "\"}";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// Limpiar cola (POST) - usado por Cancelar + Limpiar
void handleCaptureBatchClearPOST() {
  clearCaptureQueueFile();
  captureMode = false; captureBatchMode = false;
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "cleared");
}

// Pause / Resume (toggle) - POST
void handleCaptureBatchPausePOST() {
  if (captureBatchMode && captureMode) {
    captureMode = false; // paused
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "paused");
    return;
  }
  if (captureBatchMode && !captureMode) {
    captureMode = true; // resume
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "resumed");
    return;
  }
  captureMode = true; captureBatchMode = true;
  server.sendHeader("Location", "/capture_batch");
  server.send(303, "text/plain", "started");
}

// Borrar la última UID agregada (POST)
void handleCaptureRemoveLastPOST() {
  auto q = readCaptureQueue();
  // remove last non-empty entry
  for (int i = (int)q.size() - 1; i >= 0; --i) {
    if (q[i].length() > 0) { q.erase(q.begin() + i); break; }
  }
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
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "cancelled");
}

// Terminar y guardar:
// - Para cada UID en cola que ya tenga registro en users -> escribir attendance (entrada/captura_batch_saved) y eliminar de cola.
// - Para UIDs no registradas se dejan en cola (para que el prof decida).
void handleCaptureFinishPOST() {
  auto q = readCaptureQueue();
  std::vector<String> remaining;
  int savedCount = 0;
  for (auto &uid : q) {
    String u = uid; u.trim();
    if (u.length() == 0) continue;
    String found = findAnyUserByUID(u);
    if (found.length() > 0) {
      auto c = parseQuotedCSVLine(found);
      String name = (c.size() > 1 ? c[1] : "");
      String account = (c.size() > 2 ? c[2] : "");
      String rec = "\"" + nowISO() + "\"," + "\"" + u + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"\",\"captura_batch\"";
      appendLineToFile(ATT_FILE, rec);
      savedCount++;
    } else {
      // keep for later
      remaining.push_back(u);
    }
  }
  // overwrite queue with remaining (unregistered) UIDs
  writeCaptureQueue(remaining);
  // stop capture mode
  captureMode = false; captureBatchMode = false;
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", String("finished, saved: ") + String(savedCount));
}

// (compat) permitir generar links desde cola (si se desea mantener)
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
  html += "<p class='small'>Estos enlaces expirarán en 5 minutos.</p>";
  html += "<ul>";
  for (auto &u : urls) html += "<li><a href='" + u + "'>" + u + "</a></li>";
  html += "</ul>";
  html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture'>Volver</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// ---------------- Edición desde Students (igual que antes) ----------------
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
