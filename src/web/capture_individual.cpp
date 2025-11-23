// src/web/capture_individual.cpp
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>
#include <vector>

#include "capture.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include "display.h"

// Globals from globals.h already available:
// captureMode, captureBatchMode, captureUID, captureName, captureAccount, captureDetectedAt
extern volatile bool awaitingSelfRegister;
extern String currentSelfRegUID;
extern std::vector<SelfRegSession> selfRegSessions;

static String jsonEscapeLocal(const String &s) {
  String o = s; o.replace("\\","\\\\"); o.replace("\"","\\\"");
  return o;
}

void capture_individual_page() {
  captureMode = true;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  #ifdef USE_DISPLAY
  showCaptureMode(false, false);
  #endif

  // target: "students" (default) or "teachers"
  String target = server.hasArg("target") ? server.arg("target") : "students";
  String return_page = (target == "teachers") ? String("/teachers_all") : String("/students_all");

  String html = htmlHeader("Capturar - Individual");
  html += "<div class='card'><h2>Captura Individual</h2>";
  html += "<p class='small'>Acerca la tarjeta. UID autocompletará los campos si existe.</p>";

  html += "<form id='capForm' method='POST' action='/capture_confirm'>";
  html += "<input type='hidden' name='target' value='" + target + "'>";
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
  // Cancelar: ir a la página correspondiente y notificar al servidor
  html += "<a class='btn btn-red' href='" + return_page + "' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";
  html += "</form></div>" + htmlFooter();

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

void capture_individual_poll() {
  if (captureUID.length() == 0) {
    server.send(200, "application/json", "{\"status\":\"waiting\"}");
    return;
  }
  String j = "{\"status\":\"found\",\"uid\":\"" + jsonEscapeLocal(captureUID) + "\"";
  if (captureName.length() > 0) j += ",\"name\":\"" + jsonEscapeLocal(captureName) + "\"";
  if (captureAccount.length() > 0) j += ",\"account\":\"" + jsonEscapeLocal(captureAccount) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

void capture_individual_confirm() {
  if (!server.hasArg("uid") || !server.hasArg("name") ||
      !server.hasArg("account") || !server.hasArg("materia") || !server.hasArg("target")) {
    server.send(400, "text/plain", "Faltan parámetros");
    return;
  }

  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.arg("materia"); materia.trim();
  String target = server.arg("target"); target.trim();

  if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }

  bool ok = true;
  if (account.length() != 7) ok = false;
  for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { ok = false; break; }
  if (!ok) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  if (target == "students") {
    if (!courseExists(materia)) {
      server.send(400, "text/plain", "La materia especificada no existe. Regístrela primero en Materias.");
      return;
    }
    if (existsUserUidMateria(uid, materia) || existsUserAccountMateria(account, materia)) {
      captureMode = false; captureBatchMode = false;
      String html = htmlHeader("Duplicado detectado");
      html += "<div class='card'><h3 style='color:red;'>⚠️ El estudiante ya está registrado en esta materia.</h3>";
      html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
      html += "<a href='/' class='btn btn-blue'>Inicio</a>";
      html += "<a href='/students_all' class='btn btn-green'>Volver a Alumnos</a>";
      html += "</div></div>";
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
    html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
    html += "<a href='/students_all' class='btn btn-blue'>Volver a Alumnos</a>";
    html += "<a href='/capture_individual?target=students' class='btn btn-green'>Capturar otro</a>";
    html += "</div></div>";
    html += htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  if (target == "teachers") {
    // no comprobamos materia existence here (could be empty)
    String created = nowISO();
    String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"" + created + "\"";
    if (!appendLineToFile(TEACHERS_FILE, line)) { server.send(500, "text/plain", "Error guardando maestro"); return; }

    captureMode = false; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

    String html = htmlHeader("Maestro registrado");
    html += "<div class='card'><h3>✅ Maestro registrado correctamente.</h3>";
    html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
    html += "<a href='/teachers_all' class='btn btn-blue'>Volver a Maestros</a>";
    html += "<a href='/capture_individual?target=teachers' class='btn btn-green'>Capturar otro</a>";
    html += "</div></div>";
    html += htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  server.send(400, "text/plain", "target inválido");
}

void capture_individual_startPOST() {
  captureMode = true; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false,false);
  #endif
  server.sendHeader("Location", "/capture_individual");
  server.send(303, "text/plain", "capture started");
}

void capture_individual_stopGET() {
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  cancelCaptureAndReturnToNormal();
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "stopped");
}

// EDIT handlers: support editing both USERS_FILE and TEACHERS_FILE (detect file)
static String sanitizeReturnToLocal(const String &rt) {
  if (rt.length() > 0 && rt[0] == '/') return rt;
  return String("/students_all");
}

void capture_individual_editPage() {
  if (!server.hasArg("uid")) { server.send(400, "text/plain", "uid required"); return; }
  String uid = server.arg("uid");
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : "/students_all";
  return_to = sanitizeReturnToLocal(return_to);

  // Try find in USERS_FILE first
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  bool found = false;
  String foundName="", foundAccount="", foundMateria="", foundCreated="";
  String source = "users";
  if (f) {
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim(); if (l.length()==0) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 3 && c[0] == uid) {
        foundName = (c.size() > 1 ? c[1] : "");
        foundAccount = (c.size() > 2 ? c[2] : "");
        foundMateria = (c.size() > 3 ? c[3] : "");
        foundCreated = (c.size() > 4 ? c[4] : nowISO());
        found = true;
        source = "users";
        break;
      }
    }
    f.close();
  }

  if (!found) {
    File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
    if (ft) {
      while (ft.available()) {
        String l = ft.readStringUntil('\n'); l.trim(); if (l.length()==0) continue;
        auto c = parseQuotedCSVLine(l);
        if (c.size() >= 3 && c[0] == uid) {
          foundName = (c.size() > 1 ? c[1] : "");
          foundAccount = (c.size() > 2 ? c[2] : "");
          foundMateria = (c.size() > 3 ? c[3] : "");
          foundCreated = (c.size() > 4 ? c[4] : nowISO());
          found = true;
          source = "teachers";
          break;
        }
      }
      ft.close();
    }
  }

  if (!found) { server.send(404, "text/plain", "Usuario no encontrado"); return; }

  String html = htmlHeader("Editar Usuario");
  html += "<div class='card'><h2>Editar Usuario</h2>";
  html += "<form method='POST' action='/capture_edit_post'>";
  html += "<input type='hidden' name='uid' value='" + uid + "'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  html += "<input type='hidden' name='source' value='" + source + "'>";
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
  html += "<div style='padding:6px;background:#f5f7f5;border-radius:4px;'>" + foundCreated + "</div><br>";
  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Guardar</button>";
  html += "<a class='btn btn-red' href='" + return_to + "'>Cancelar</a>";
  html += "</div></form></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

void capture_individual_editPost() {
  if (!server.hasArg("uid") || !server.hasArg("name") || !server.hasArg("account") || !server.hasArg("return_to") || !server.hasArg("source")) {
    server.send(400, "text/plain", "Faltan parámetros");
    return;
  }
  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.hasArg("materia") ? server.arg("materia") : "";
  materia.trim();
  String return_to = sanitizeReturnToLocal(server.arg("return_to"));
  String source = server.arg("source");

  if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }
  if (name.length() == 0) { server.send(400, "text/plain", "Nombre vacío"); return; }
  if (account.length() != 7) { server.send(400, "text/plain", "Cuenta inválida"); return; }
  for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  const char* targetFile = (source == "teachers") ? TEACHERS_FILE : USERS_FILE;

  File f = SPIFFS.open(targetFile, FILE_READ);
  if (!f) { server.send(500, "text/plain", "No se pudo abrir archivo"); return; }

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

  if (!updated) { server.send(404, "text/plain", "Usuario no encontrado"); return; }

  if (!writeAllLines(targetFile, lines)) { server.send(500, "text/plain", "Error guardando"); return; }

  server.sendHeader("Location", return_to);
  server.send(303, "text/plain", "Updated");
}
