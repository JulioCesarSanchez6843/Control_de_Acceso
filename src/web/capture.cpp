// src/web/capture.cpp
// Captura: modo individual (original) + modo batch (cola de UIDs) + edición desde Students.

#include "capture.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>

// Variables globales (declaradas en globals.h)
extern volatile bool captureMode;
extern volatile bool captureBatchMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// Usar la constante de globals.h para el archivo de cola.
// Asegúrate en globals.h que CAPTURE_QUEUE_FILE esté declarado extern y definido en globals.cpp
// Si por alguna razón prefieres usar un archivo local, reemplaza CAPTURE_QUEUE_FILE por la ruta fija.
#ifndef CAPTURE_QUEUE_FILE
static const char *CAPTURE_QUEUE_FILE_LOCAL = "/capture_queue.csv";
#define CAPTURE_QUEUE_FILE CAPTURE_QUEUE_FILE_LOCAL
#endif

// -------------------- Helpers de cola --------------------
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

// Evita duplicados simples (compara texto exacto).
static bool appendUidToCaptureQueue(const String &uid) {
  if (uid.length() == 0) return false;
  // evitar duplicados
  auto q = readCaptureQueue();
  for (auto &u : q) if (u == uid) return false;
  // guardar la UID como línea simple (sin comillas)
  return appendLineToFile(CAPTURE_QUEUE_FILE, uid);
}

static bool clearCaptureQueueFile() {
  if (SPIFFS.exists(CAPTURE_QUEUE_FILE)) {
    return SPIFFS.remove(CAPTURE_QUEUE_FILE);
  }
  return true;
}

// -------------------- Página de captura --------------------
void handleCapturePage() {
  // No forzamos captureMode completo aquí: lo iniciará el usuario (individual o batch).
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  String html = htmlHeader("Capturar Tarjeta");
  html += "<div class='card'><h2>Capturar Tarjeta</h2>";
  html += "<p class='small'>Acerca la tarjeta. Si ya existe en otra materia se autocompletan los campos. Selecciona Materia y confirma.</p>";

  // Botones para iniciar modos (formularios POST/GET según preferencia)
  html += "<div style='display:flex;gap:8px;align-items:center;margin-bottom:12px'>";
  html += "<form method='POST' action='/capture_start' style='display:inline'><button class='btn btn-blue' type='submit'>Individual</button></form>";
  // web_routes usa GET para iniciar batch en la versión que mostraste; hacemos ambos seguros:
  html += "<form method='GET' action='/capture_batch_start' style='display:inline'><button class='btn btn-blue' type='submit'>Batch (varias tarjetas)</button></form>";
  html += "</div>";

  // --- Individual form (original) ---
  html += "<div id='individual_div'>";
  html += "<form id='capForm' method='POST' action='/capture_confirm'>";
  html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input id='name' name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7'><br>";

  auto courses = loadCourses();
  html += "Materia (seleccionar):<br><select id='materia' name='materia'>";
  html += "<option value=''>-- Seleccionar materia --</option>";
  for (auto &c : courses) html += "<option value='" + c.materia + "'>" + c.materia + " (" + c.profesor + ")</option>";
  html += "</select><br>";

  html += "<div id='msg' style='color:red;font-weight:bold;margin-top:10px;'></div>";

  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Confirmar</button>";
  html += "<a class='btn btn-red' href='/' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";
  html += "</form>";
  html += "</div>"; // individual_div

  // --- Batch UI ---
  html += "<div id='batch_div' style='margin-top:16px'>";
  html += "<h3>Batch capture</h3>";
  html += "<p class='small'>Acerca varias tarjetas; las UIDs quedarán en una cola. Luego puede limpiar la cola o generar enlaces de auto-registro.</p>";

  html += "<div style='display:flex;gap:8px;margin-bottom:8px;'>";
  html += "<form method='GET' action='/capture_batch_start' style='display:inline'><button class='btn btn-green' type='submit'>Iniciar Batch</button></form>";
  html += "<form method='POST' action='/capture_batch_stop' style='display:inline'><button class='btn btn-red' type='submit'>Detener Batch</button></form>";
  html += "</div>";

  html += "<p>Cola actual: <span id='queue_count'>" + String(readCaptureQueue().size()) + "</span> UID(s).</p>";
  html += "<div style='display:flex;gap:8px;margin-top:8px'>";
  html += "<form method='POST' action='/capture_clear_queue' onsubmit='return confirm(\"Limpiar cola? Esta acción borrará la cola.\")' style='display:inline'><button class='btn btn-red' type='submit'>Limpiar cola</button></form>";
  html += "<form method='POST' action='/capture_generate_links' style='display:inline'><button class='btn btn-blue' type='submit'>Generar links de auto-registro</button></form>";
  html += "</div>";

  html += "<div id='queue_list' style='background:#f5f7fb;padding:8px;border-radius:8px;min-height:80px;margin-top:8px;'>No hay UIDs capturadas aún.</div>";
  html += "</div>"; // batch_div

  // JS: polling para individual (autocompleta) y refresco de cola
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

  // Poll queue count & list
  function pollQueue(){
    fetch('/capture_batch_poll')
      .then(r=>r.json())
      .then(j=>{
        var cntEl = document.getElementById('queue_count');
        if(cntEl) cntEl.textContent = j.uids ? j.uids.length : 0;
        var list = document.getElementById('queue_list');
        if(!j.uids || j.uids.length==0) list.innerHTML = 'No hay UIDs capturadas aún.';
        else {
          var html = '<ul>';
          for(var i=0;i<j.uids.length;i++) html += '<li>' + j.uids[i] + '</li>';
          html += '</ul>';
          list.innerHTML = html;
        }
      })
      .catch(e=>{});
    setTimeout(pollQueue,1200);
  }
  pollQueue();

  // stop capture on unload
  window.addEventListener('beforeunload', function(){
    try { navigator.sendBeacon('/capture_stop'); } catch(e){}
  });
  </script>
  )rawliteral";

  html += "</div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// -------------------- Polling individual --------------------
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

// -------------------- Confirmación individual (POST) --------------------
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

  // Evitar duplicados en misma materia.
  if (existsUserUidMateria(uid, materia) || existsUserAccountMateria(account, materia)) {
    captureMode = false;
    captureBatchMode = false;
    String html = htmlHeader("Duplicado detectado");
    html += "<div class='card'><h3 style='color:red;'>⚠️ El estudiante ya está registrado en esta materia.</h3>";
    html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
    html += "<a href='/' class='btn btn-blue'>Inicio</a> ";
    html += "<a href='/capture' class='btn btn-green'>Capturar otro</a>";
    html += "</div></div>";
    html += htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  // Guardar nuevo usuario y attendance
  String created = nowISO();
  String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," +
                "\"" + materia + "\"," + "\"" + created + "\"";
  if (!appendLineToFile(USERS_FILE, line)) { server.send(500, "text/plain", "Error guardando usuario"); return; }

  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," +
               "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura\"";
  if (!appendLineToFile(ATT_FILE, rec)) { server.send(500, "text/plain", "Error guardando attendance"); return; }

  // Reset modo captura
  captureMode = false;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  String html = htmlHeader("Registrado correctamente");
  html += "<div class='card'><h3>✅ Usuario registrado correctamente.</h3>";
  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<a href='/' class='btn btn-blue'>Inicio</a> ";
  html += "<a href='/capture' class='btn btn-green'>Capturar otro</a>";
  html += "</div></div>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

// -------------------- Start/Stop capture (individual & batch) --------------------
void handleCaptureStartPOST() {
  captureMode = true;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "capture started");
}

// GET /capture_batch_start -> activa batch y limpia cola
void handleCaptureBatchStartGET() {
  captureMode = true;
  captureBatchMode = true;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;
  // limpiar cola antes de empezar (si quieres conservar colas, cambia esto)
  clearCaptureQueueFile();
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "batch started");
}

void handleCaptureBatchStopPOST() {
  captureMode = false;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "batch stopped");
}

void handleCaptureStopGET() {
  captureMode = false;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "stopped");
}

// -------------------- Queue endpoints --------------------
// GET /capture_batch_poll -> JSON { uids: [...] }
void handleCaptureBatchPollGET() {
  auto u = readCaptureQueue();
  String j = "{\"uids\":[";
  for (size_t i = 0; i < u.size(); ++i) {
    if (i) j += ",";
    j += "\"" + u[i] + "\"";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// POST /capture_clear_queue
void handleCaptureBatchClearPOST() {
  clearCaptureQueueFile();
  captureMode = false;
  captureBatchMode = false;
  server.sendHeader("Location", "/capture");
  server.send(303, "text/plain", "cleared");
}

// POST /capture_generate_links
// Lee la cola, genera sesiones (selfRegSessions) y devuelve una página con enlaces.
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
    // generar token
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

  // limpiar cola
  clearCaptureQueueFile();

  // mostrar página con enlaces
  String html = htmlHeader("Links de Auto-registro");
  html += "<div class='card'><h2>Links generados</h2>";
  html += "<p class='small'>Estos enlaces expirarán en 5 minutos. Entregue el QR o enlace al alumno para que complete su registro.</p>";
  html += "<ul>";
  for (auto &u : urls) html += "<li><a href='" + u + "'>" + u + "</a></li>";
  html += "</ul>";
  html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture'>Volver</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// -------------------- Edición de alumno desde Students (GET/POST) --------------------
// sanitize return_to
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
