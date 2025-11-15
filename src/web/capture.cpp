#include "capture.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>

// Variables externas
extern volatile bool captureMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// Página de captura manual: activa modo captura y sirve el formulario.
void handleCapturePage() {
  captureMode = true;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  String html = htmlHeader("Capturar Tarjeta (modo manual)");
  html += "<div class='card'><h2>Capturar Tarjeta</h2>";
  html += "<p class='small'>Acerca la tarjeta. Si ya existe en otra materia, se autocompletan los campos. "
          "Selecciona Materia (obligatorio) y revisa la información antes de confirmar.</p>";

  // Formulario principal
  html += "<form id='capForm' method='POST' action='/capture_confirm'>";
  html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input id='name' name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7'><br>";

  auto courses = loadCourses();
  html += "Materia (seleccionar):<br><select id='materia' name='materia'>";
  html += "<option value=''>-- Seleccionar materia --</option>";
  for (auto &c : courses)
    html += "<option value='" + c.materia + "'>" + c.materia + " (" + c.profesor + ")</option>";
  html += "</select><br>";

  html += "<div id='msg' style='color:red;font-weight:bold;margin-top:10px;'></div>";

  // Botones
  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Confirmar</button>";
  html += "<a class='btn btn-red' href='/' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";

  html += "</form>";

  // Script de autocompletado y polling para UID detectado.
  html += R"rawliteral(
  <script>
  function isAccountValid(s){ return /^[0-9]{7}$/.test(s); }

  // Polling para detectar tarjeta
  function poll(){
    fetch('/capture_poll')
      .then(r=>r.json())
      .then(j=>{
        if(j.status=='waiting'){ setTimeout(poll,700); }
        else if(j.status=='found'){
          document.getElementById('uid').value = j.uid || '';
          if(j.name) document.getElementById('name').value = j.name;
          if(j.account) document.getElementById('account').value = j.account;
          setTimeout(poll,700);
        } else { setTimeout(poll,700); }
      })
      .catch(e=>setTimeout(poll,1200));
  }

  poll();

  // Detiene modo captura al salir
  window.addEventListener('beforeunload', function(){
    try { navigator.sendBeacon('/capture_stop'); } catch(e){}
  });
  </script>
  )rawliteral";

  html += "</div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// Polling: responde si hay UID detectado y datos autocompletos.
void handleCapturePoll() {
  if (captureUID.length() == 0) {
    server.send(200, "application/json", "{\"status\":\"waiting\"}");
    return;
  }
  // Construir JSON básico
  String j = "{\"status\":\"found\",\"uid\":\"" + captureUID + "\"";
  if (captureName.length() > 0) {
    j += ",\"name\":\"" + captureName + "\"";
  }
  if (captureAccount.length() > 0) {
    j += ",\"account\":\"" + captureAccount + "\"";
  }
  j += "}";
  server.send(200, "application/json", j);
}

// Confirmación manual (POST): valida campos y registra usuario y attendance.
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
  for (size_t i = 0; i < account.length(); i++) {
    if (!isDigit(account[i])) { ok = false; break; }
  }
  if (!ok) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  if (!courseExists(materia)) {
    server.send(400, "text/plain", "La materia especificada no existe. Regístrela primero en Materias.");
    return;
  }

  // Evitar duplicados en misma materia.
  if (existsUserUidMateria(uid, materia) || existsUserAccountMateria(account, materia)) {
    captureMode = false;
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

  // Guardar nuevo usuario y registrar captura en attendance.
  String created = nowISO();
  String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," +
                "\"" + materia + "\"," + "\"" + created + "\"";
  if (!appendLineToFile(USERS_FILE, line)) {
    server.send(500, "text/plain", "Error guardando usuario");
    return;
  }

  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," +
               "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura\"";
  if (!appendLineToFile(ATT_FILE, rec)) {
    server.send(500, "text/plain", "Error guardando attendance");
    return;
  }

  // Reset modo captura y mostrar confirmación.
  captureMode = false;
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

// Detiene modo captura y redirige a inicio.
void handleCaptureStopGET() {
  captureMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "stopped");
}

// ---------------- Edición de alumno desde Students ----------------

// Sanitiza return_to: solo rutas internas que empiecen por '/'
static String sanitizeReturnTo(const String &rt) {
  if (rt.length() > 0 && rt[0] == '/') return rt;
  return String("/students_all");
}

// Muestra formulario de edición para un UID dado.
void handleCaptureEditPage() {
  if (!server.hasArg("uid")) { server.send(400, "text/plain", "uid required"); return; }
  String uid = server.arg("uid");
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : "/students_all";
  return_to = sanitizeReturnTo(return_to);

  // Buscar alumno en USERS_FILE
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

  // Construir formulario de edición prellenado.
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

  // Materia: select con la materia actual seleccionada.
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
  html += "</div>";

  html += "</form></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// Procesa POST de edición: valida, reescribe USERS_FILE y redirige.
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
  for (size_t i = 0; i < account.length(); i++) {
    if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }
  }

  // Si se seleccionó materia, verificar que exista.
  if (materia.length() > 0 && !courseExists(materia)) {
    server.send(400, "text/plain", "La materia seleccionada no existe");
    return;
  }

  // Leer USERS_FILE y construir nuevo contenido con la fila actualizada.
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
      // Reconstruir linea con valores actualizados
      String created = (c.size() > 4 ? c[4] : nowISO());
      String mat = (materia.length() ? materia : (c.size() > 3 ? c[3] : ""));
      String newline = "\"" + uid + "\",\"" + name + "\",\"" + account + "\",\"" + mat + "\",\"" + created + "\"";
      lines.push_back(newline);
      updated = true;
    } else {
      lines.push_back(l);
    }
  }
  f.close();

  if (!updated) {
    server.send(404, "text/plain", "Alumno no encontrado");
    return;
  }

  // Guardar cambios y redirigir.
  if (!writeAllLines(USERS_FILE, lines)) {
    server.send(500, "text/plain", "Error guardando usuarios");
    return;
  }

  server.sendHeader("Location", return_to);
  server.send(303, "text/plain", "Updated");
}
