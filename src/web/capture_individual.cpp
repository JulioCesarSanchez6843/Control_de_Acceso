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

// Globals (definidos en globals.cpp)
extern volatile bool awaitingSelfRegister;
extern String currentSelfRegUID;
extern std::vector<SelfRegSession> selfRegSessions;

static String jsonEscapeLocal(const String &s) {
  String o = s;
  o.replace("\\","\\\\");
  o.replace("\"","\\\"");
  o.replace("\n","\\n");
  o.replace("\r","\\r");
  return o;
}

// Helper: intenta inferir profesor asociado a una materia (si existe exactamente 1 curso con ese nombre)
static String inferProfessorForMateria(const String &materia) {
  if (materia.length() == 0) return String();
  auto courses = loadCourses();
  String found = "";
  int count = 0;
  for (auto &c : courses) {
    if (c.materia == materia) {
      found = c.profesor;
      count++;
      if (count > 1) break;
    }
  }
  if (count == 1) return found;
  return String();
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

  // Si el objetivo es 'students', preparamos lista de materias; si es 'teachers' NO mostrar materia/profesor
  std::vector<String> materias;
  if (target == "students") {
    auto courses = loadCourses();
    for (auto &c : courses) {
      bool ok = true;
      for (auto &m : materias) if (m == c.materia) { ok = false; break; }
      if (ok) materias.push_back(c.materia);
    }
  }

  String html = htmlHeader("Capturar - Individual");
  html += "<div class='card'><h2>Captura Individual</h2>";
  html += "<p class='small'>Acerca la tarjeta. UID autocompletará los campos si existe.</p>";

  html += "<form id='capForm' method='POST' action='/capture_confirm'>";
  html += "<input type='hidden' name='target' value='" + target + "'>";
  html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input id='name' name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7'><br>";

  // Si target == students mostrar materia/profesor; si es teachers NO mostrar
  if (target == "students") {
    // Select Materia
    html += "Materia:<br><select id='materia' name='materia'>";
    html += "<option value=''>-- Seleccionar materia --</option>";
    for (auto &m : materias) html += "<option value='" + m + "'>" + m + "</option>";
    html += "</select><br>";

    // Select Profesor (se llenará dinámicamente con /profesores_for?materia=...)
    html += "Profesor (opcional):<br><select id='profesor' name='profesor'><option value=''>-- Seleccionar profesor --</option></select><br>";
  } else {
    // For teachers, include hidden materia/profesor so server-side code that expects fields keeps working.
    html += "<input type='hidden' name='materia' value=''>\n";
    html += "<input type='hidden' name='profesor' value=''>\n";
    html += "<p class='small'>Registrando como <b>maestro</b>. No se asociará a ninguna materia aquí — las materias se gestionan después.</p>";
  }

  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Confirmar</button>";
  // Cancelar: ir a la página correspondiente y notificar al servidor
  html += "<a class='btn btn-red' href='" + return_page + "' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";
  html += "</form></div>" + htmlFooter();

  // JavaScript: pollUID + when materia changes fetch profesores_for
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
            if(j.materia){
              var selM = document.getElementById('materia');
              if(selM){
                for(var i=0;i<selM.options.length;i++){
                  if(selM.options[i].value === j.materia) { selM.selectedIndex = i; selM.dispatchEvent(new Event('change')); break; }
                }
              }
              setTimeout(function(){
                var selP = document.getElementById('profesor');
                if(selP && j.profesor){
                  for(var i=0;i<selP.options.length;i++){
                    if(selP.options[i].value === j.profesor) { selP.selectedIndex = i; break; }
                  }
                }
              }, 300);
            }
          }
          setTimeout(pollUID,700);
        })
        .catch(e=>setTimeout(pollUID,1200));
    }

    document.addEventListener('DOMContentLoaded', function(){
      var mat = document.getElementById('materia');
      var prof = document.getElementById('profesor');
      function clearProf(){ if(prof){ prof.innerHTML = '<option value=\"\">-- Seleccionar profesor --</option>'; } }
      if(mat){
        mat.addEventListener('change', function(){
          var val = mat.value || '';
          clearProf();
          if(!val) return;
          fetch('/profesores_for?materia=' + encodeURIComponent(val))
            .then(r=>r.json())
            .then(j=>{
              if(!j.profesores) return;
              for(var i=0;i<j.profesores.length;i++){
                var o = document.createElement('option');
                o.value = j.profesores[i];
                o.textContent = j.profesores[i];
                prof.appendChild(o);
              }
              if(j.profesores.length === 1) prof.selectedIndex = 1;
            }).catch(e=>{
              // ignore
            });
        });
      }
    });

    window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e){} });
    pollUID();
    </script>
  )rawliteral";

  server.send(200, "text/html", html);
}

void capture_individual_poll() {
  // Enriquecemos la respuesta con materia/profesor cuando sea posible
  if (captureUID.length() == 0) {
    server.send(200, "application/json", "{\"status\":\"waiting\"}");
    return;
  }

  String nameOut = captureName;
  String accountOut = captureAccount;
  String materiaOut = "";
  String profesorOut = "";

  // Intentar localizar en USERS_FILE o TEACHERS_FILE
  String rec = findAnyUserByUID(captureUID); // devuelve linea CSV del usuario (users o teachers) o "" si no existe
  if (rec.length() > 0) {
    auto parts = parseQuotedCSVLine(rec);
    if (parts.size() > 1) nameOut = parts[1];
    if (parts.size() > 2) accountOut = parts[2];
    if (parts.size() > 3) materiaOut = parts[3];
    // Inferir profesor si hay único course con esa materia
    profesorOut = inferProfessorForMateria(materiaOut);
  } else {
    // Si no está registrado, no ponemos materia/profesor
  }

  String j = "{\"status\":\"found\",\"uid\":\"" + jsonEscapeLocal(captureUID) + "\"";
  if (nameOut.length() > 0) j += ",\"name\":\"" + jsonEscapeLocal(nameOut) + "\"";
  if (accountOut.length() > 0) j += ",\"account\":\"" + jsonEscapeLocal(accountOut) + "\"";
  if (materiaOut.length() > 0) j += ",\"materia\":\"" + jsonEscapeLocal(materiaOut) + "\"";
  if (profesorOut.length() > 0) j += ",\"profesor\":\"" + jsonEscapeLocal(profesorOut) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

void capture_individual_confirm() {
  // Notar: no requerimos materia si target == teachers
  if (!server.hasArg("uid") || !server.hasArg("name") ||
      !server.hasArg("account") || !server.hasArg("target")) {
    server.send(400, "text/plain", "Faltan parámetros");
    return;
  }

  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.hasArg("materia") ? server.arg("materia") : "";
  materia.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  profesor.trim();
  String target = server.arg("target"); target.trim();

  if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }

  bool ok = true;
  if (account.length() != 7) ok = false;
  for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { ok = false; break; }
  if (!ok) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  if (target == "students") {
    if (materia.length() == 0) {
      server.send(400, "text/plain", "La materia especificada no existe. Regístrela primero en Materias.");
      return;
    }
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
    // NOTE: para mantener compatibilidad con USERS_FILE, seguimos guardando (materia) en la columna materia.
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
    // Guardar maestro — no asociamos materia/profesor en registro de teacher aquí
    String created = nowISO();
    // Construimos la línea con la columna materia vacía para mantener compatibilidad
    String teacherLine = "\"" + uid + "\",\"" + name + "\",\"" + account + "\",\"\",\"" + created + "\"";
    if (!appendLineToFile(TEACHERS_FILE, teacherLine)) { server.send(500, "text/plain", "Error guardando maestro"); return; }

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
      if (c.size() >= 1 && c[0] == uid) {
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
        if (c.size() >= 1 && c[0] == uid) {
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

  // Build materia/professor selects similarly to capture_individual_page, but hide for teachers
  auto courses2 = loadCourses();
  std::vector<String> materias;
  for (auto &c : courses2) {
    bool ok = true;
    for (auto &m : materias) if (m == c.materia) { ok = false; break; }
    if (ok) materias.push_back(c.materia);
  }

  // Infer professor if unique
  String foundProfesor = inferProfessorForMateria(foundMateria);

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

  if (source == "users") {
    // Materia select
    html += "<label>Materia:</label><br><select id='edit_materia' name='materia'>";
    html += "<option value=''>-- Ninguna --</option>";
    for (auto &m : materias) {
      String sel = (m == foundMateria) ? " selected" : "";
      html += "<option value='" + m + "'" + sel + ">" + m + "</option>";
    }
    html += "</select><br><br>";

    // Profesor select (llenado vía JS)
    html += "<label>Profesor:</label><br>";
    html += "<select id='edit_profesor' name='profesor'><option value=''>-- Ninguno --</option>";
    if (foundProfesor.length()) {
      html += "<option value='" + foundProfesor + "' selected>" + foundProfesor + "</option>";
    }
    html += "</select><br><br>";
  } else {
    // teacher: do not show materia/profesor inputs (store empty materia in hidden field)
    html += "<input type='hidden' name='materia' value=''>\n";
    html += "<input type='hidden' name='profesor' value=''>\n";
    html += "<p class='small'>Este usuario es un maestro; las materias se gestionan por separado.</p>";
  }

  html += "<label>Registrado:</label><br>";
  html += "<div style='padding:6px;background:#f5f7f5;border-radius:4px;'>" + foundCreated + "</div><br>";
  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Guardar</button>";
  html += "<a class='btn btn-red' href='" + return_to + "'>Cancelar</a>";
  html += "</div></form></div>" + htmlFooter();

  // JS para poblar profesores cuando cambie materia (same endpoint)
  html += R"rawliteral(
    <script>
    document.addEventListener('DOMContentLoaded', function(){
      var mat = document.getElementById('edit_materia');
      var prof = document.getElementById('edit_profesor');
      function clearProf(){ if(prof){ prof.innerHTML = '<option value=\"\">-- Ninguno --</option>'; } }
      if(mat){
        mat.addEventListener('change', function(){
          var val = mat.value || '';
          clearProf();
          if(!val) return;
          fetch('/profesores_for?materia=' + encodeURIComponent(val))
            .then(r=>r.json())
            .then(j=>{
              if(!j.profesores) return;
              for(var i=0;i<j.profesores.length;i++){
                var o = document.createElement('option');
                o.value = j.profesores[i];
                o.textContent = j.profesores[i];
                prof.appendChild(o);
              }
            }).catch(e=>{
              // ignore
            });
        });
      }
    });
    </script>
  )rawliteral";

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
