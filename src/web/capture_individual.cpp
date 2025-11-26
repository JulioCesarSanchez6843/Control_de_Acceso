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

// JSON escape
static String jsonEscapeLocal(const String &s) {
  String o = s;
  o.replace("\\","\\\\");
  o.replace("\"","\\\"");
  o.replace("\n","\\n");
  o.replace("\r","\\r");
  return o;
}

// HTML escape
static String escapeHTML(const String &s) {
  String o;
  o.reserve(s.length());
  for (size_t i = 0; i < (size_t)s.length(); ++i) {
    char c = s.charAt(i);
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '\"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c; break;
    }
  }
  return o;
}

// Infer professor if materia has exactly 1 professor
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

// Local: get professors for materia (independiente de otros files)
static std::vector<String> getProfessorsForMateriaLocal(const String &materia) {
  std::vector<String> out;
  if (materia.length() == 0) return out;
  auto courses = loadCourses();
  for (auto &c : courses) {
    if (c.materia == materia) {
      bool ok = true;
      for (auto &p : out) if (p == c.profesor) { ok = false; break; }
      if (ok) out.push_back(c.profesor);
    }
  }
  return out;
}

// Comprueba existencia exacta uid+materia en USERS_FILE
static bool userExistsUidMateriaExact(const String &uid, const String &materia) {
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length() == 0) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 4) {
      String uidc = c[0];
      String matc = c[3];
      if (uidc == uid && matc == materia) { f.close(); return true; }
    }
  }
  f.close();
  return false;
}

// Busca por cuenta; devuelve pair(uid,source) o ("","") si no existe.
// source = "users" o "teachers"
static std::pair<String,String> findByAccount(const String &account) {
  if (account.length() == 0) return std::make_pair(String(""), String(""));
  // buscar users
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (f) {
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim(); if (l.length()==0) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 3) {
        String uid = c[0];
        String acc = c[2];
        if (acc == account) { f.close(); return std::make_pair(uid, String("users")); }
      }
    }
    f.close();
  }
  // buscar teachers
  File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (ft) {
    while (ft.available()) {
      String l = ft.readStringUntil('\n'); l.trim(); if (l.length()==0) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 3) {
        String uid = c[0];
        String acc = c[2];
        if (acc == account) { ft.close(); return std::make_pair(uid, String("teachers")); }
      }
    }
    ft.close();
  }
  return std::make_pair(String(""), String(""));
}

// reusa findAnyUserByUID de files_utils (devuelve línea completa o "")
static String findAnyUserLineByUID(const String &uid) {
  return findAnyUserByUID(uid);
}

// --------------------------------------------------
// Página: captura individual (formulario)
// --------------------------------------------------
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

  String target = server.hasArg("target") ? server.arg("target") : "students";
  String return_page = (target == "teachers") ? String("/teachers_all") : String("/students_all");

  // preparar lista materias si es students
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
  html += "<input type='hidden' name='target' value='" + escapeHTML(target) + "'>";
  html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input id='name' name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7' pattern='[0-9]{7}'><br>";

  if (target == "students") {
    html += "Materia (opcional):<br><select id='materia' name='materia'>";
    html += "<option value=''>-- Ninguna --</option>";
    for (auto &m : materias) html += "<option value='" + escapeHTML(m) + "'>" + escapeHTML(m) + "</option>";
    html += "</select><br>";

    html += "Profesor:<br><select id='profesor' name='profesor' disabled>";
    html += "<option value=''>-- Ninguno --</option>";
    html += "</select><br>";

    html += "<div id='warn' class='small' style='color:#b00020;display:none;margin-top:6px;'></div>";
  } else {
    html += "<input type='hidden' name='materia' value=''>\n";
    html += "<input type='hidden' name='profesor' value=''>\n";
    html += "<p class='small'>Registrando como <b>maestro</b>. Las materias se gestionan por separado.</p>";
  }

  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button id='submitBtn' type='submit' class='btn btn-green'>Confirmar</button>";
  html += "<a class='btn btn-red' href='" + escapeHTML(return_page) + "' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";
  html += "</form></div>" + htmlFooter();

  // JS
  html += R"rawliteral(
<script>
function pollUID(){
  fetch('/capture_poll')
    .then(r=>r.json())
    .then(j=>{
      if(j.status && j.status === 'found'){
        if(j.uid) document.getElementById('uid').value = j.uid;
        if(j.name) document.getElementById('name').value = j.name;
        if(j.account) document.getElementById('account').value = j.account;
        if(j.materia){
          var selM = document.getElementById('materia');
          if(selM){
            for(var i=0;i<selM.options.length;i++){
              if(selM.options[i].value === j.materia){
                selM.selectedIndex = i;
                selM.dispatchEvent(new Event('change'));
                break;
              }
            }
          }
          setTimeout(function(){
            var selP = document.getElementById('profesor');
            if(selP && j.profesor){
              for(var i=0;i<selP.options.length;i++){
                if(selP.options[i].value === j.profesor){
                  selP.selectedIndex = i;
                  break;
                }
              }
            }
          }, 350);
        }
      }
      setTimeout(pollUID, 700);
    })
    .catch(e => setTimeout(pollUID, 1200));
}

document.addEventListener('DOMContentLoaded', function(){
  var mat = document.getElementById('materia');
  var prof = document.getElementById('profesor');
  var warn = document.getElementById('warn');
  var submitBtn = document.getElementById('submitBtn');

  function setWarn(msg){
    if(!warn) return;
    if(msg){ warn.style.display='block'; warn.textContent = msg; } else { warn.style.display='none'; warn.textContent=''; }
  }

  function clearProf(){
    if(!prof) return;
    prof.innerHTML = '<option value="">-- Ninguno --</option>';
    prof.disabled = true;
  }

  function updateStateAfterProfChange(){
    if(!mat) return;
    if(!mat.value){ // materia vacia -> profesor opcional
      clearProf();
      setWarn('');
      submitBtn.disabled = false;
      return;
    }
    if(!prof){ submitBtn.disabled = true; setWarn('Seleccione un profesor para la materia.'); return; }
    if(prof.options.length <= 1){
      setWarn('La materia seleccionada no tiene profesores registrados. Regístrela primero en Materias.');
      submitBtn.disabled = true;
      return;
    }
    if(prof.options.length === 2){
      prof.selectedIndex = 1; prof.disabled = true; setWarn(''); submitBtn.disabled = false; return;
    }
    prof.disabled = false;
    if(prof.value){ setWarn(''); submitBtn.disabled = false; } else { setWarn('Seleccione un profesor para la materia.'); submitBtn.disabled = true; }
  }

  if(mat){
    mat.addEventListener('change', function(){
      clearProf();
      setWarn('');
      submitBtn.disabled = true;
      var val = mat.value || '';
      if(!val){ clearProf(); setWarn(''); submitBtn.disabled = false; return; }
      fetch('/profesores_for?materia=' + encodeURIComponent(val))
        .then(r => {
          if(!r.ok) throw 0;
          return r.json();
        })
        .then(j => {
          if(!j || !j.profesores) { setWarn('Error al obtener profesores'); return; }
          for(var i=0;i<j.profesores.length;i++){
            var o = document.createElement('option');
            o.value = j.profesores[i];
            o.textContent = j.profesores[i];
            prof.appendChild(o);
          }
          updateStateAfterProfChange();
        })
        .catch(e => { setWarn('Error conectando para obtener profesores'); });
    });
  }

  if(prof){
    prof.addEventListener('change', function(){ updateStateAfterProfChange(); });
  }

  var form = document.getElementById('capForm');
  form.addEventListener('submit', function(ev){
    var name = document.getElementById('name').value.trim();
    var acc = document.getElementById('account').value.trim();
    if(!name){ ev.preventDefault(); setWarn('Por favor escriba el nombre.'); return false; }
    if(!/^[0-9]{7}$/.test(acc)){ ev.preventDefault(); setWarn('Cuenta inválida: debe tener 7 dígitos.'); return false; }
    var matSel = document.getElementById('materia');
    var profSel = document.getElementById('profesor');
    if(matSel && matSel.value){
      if(profSel && !profSel.value){ ev.preventDefault(); setWarn('Seleccione un profesor para la materia.'); return false; }
    }
  });

});

window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e){} });
pollUID();
</script>
)rawliteral";

  server.send(200, "text/html", html);
}

// --------------------------------------------------
// Poll endpoint JSON
// --------------------------------------------------
void capture_individual_poll() {
  if (captureUID.length() == 0) {
    server.send(200, "application/json", "{\"status\":\"waiting\"}");
    return;
  }

  String nameOut = captureName;
  String accountOut = captureAccount;
  String materiaOut = "";
  String profesorOut = "";

  String rec = findAnyUserLineByUID(captureUID);
  if (rec.length() > 0) {
    auto parts = parseQuotedCSVLine(rec);
    if (parts.size() > 1) nameOut = parts[1];
    if (parts.size() > 2) accountOut = parts[2];
    if (parts.size() > 3) materiaOut = parts[3];
    profesorOut = inferProfessorForMateria(materiaOut);
  }

  String j = "{\"status\":\"found\",\"uid\":\"" + jsonEscapeLocal(captureUID) + "\"";
  if (nameOut.length()) j += ",\"name\":\"" + jsonEscapeLocal(nameOut) + "\"";
  if (accountOut.length()) j += ",\"account\":\"" + jsonEscapeLocal(accountOut) + "\"";
  if (materiaOut.length()) j += ",\"materia\":\"" + jsonEscapeLocal(materiaOut) + "\"";
  if (profesorOut.length()) j += ",\"profesor\":\"" + jsonEscapeLocal(profesorOut) + "\"";
  j += "}";
  server.send(200, "application/json", j);
}

// --------------------------------------------------
// Confirm (POST)
// --------------------------------------------------
void capture_individual_confirm() {
  if (!server.hasArg("uid") || !server.hasArg("name") ||
      !server.hasArg("account") || !server.hasArg("target")) {
    server.send(400, "text/plain", "Faltan parámetros");
    return;
  }

  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.hasArg("materia") ? server.arg("materia") : String();
  materia.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  profesor.trim();
  String target = server.arg("target"); target.trim();

  if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }
  if (name.length() == 0) {
    server.send(400, "text/plain", "Nombre vacío"); return;
  }

  if (account.length() != 7) {
    server.send(400, "text/plain", "Cuenta inválida"); return;
  }
  for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  // check account uniqueness (across users and teachers)
  auto accFound = findByAccount(account);
  if (accFound.first.length() && accFound.first != uid) {
    // account exists for different UID -> deny
    String html = htmlHeader("Error - Cuenta duplicada");
    html += "<div class='card'><h3 style='color:red;'>Cuenta ya registrada</h3>";
    html += "<p class='small'>La cuenta <b>" + escapeHTML(account) + "</b> ya está registrada con UID <b>" + escapeHTML(accFound.first) + "</b> en <b>" + escapeHTML(accFound.second) + "</b>. No se puede usar la misma cuenta para otro usuario.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/'>Inicio</a> <a class='btn btn-green' href='/capture_individual'>Volver a Captura</a> ";
    if (accFound.second == "users") html += "<a class='btn btn-orange' href='/capture_edit?uid=" + escapeHTML(accFound.first) + "&return_to=/students_all'>Ver usuario</a>";
    else html += "<a class='btn btn-orange' href='/capture_edit?uid=" + escapeHTML(accFound.first) + "&return_to=/teachers_all'>Ver maestro</a>";
    html += "</p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // FLOW: students
  if (target == "students") {
    // materia optional: if provided, verify it exists
    if (materia.length() > 0) {
      if (!courseExists(materia)) {
        String html = htmlHeader("Error - Materia");
        html += "<div class='card'><h3>Materia no encontrada</h3><p class='small'>La materia seleccionada no existe. Regístrela primero en Materias.</p>";
        html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Ir a Materias</a> <a class='btn btn-red' href='/capture_individual'>Volver</a></p></div>" + htmlFooter();
        server.send(200, "text/html", html);
        return;
      }

      // if profesor empty, try infer
      if (profesor.length() == 0) {
        String inf = inferProfessorForMateria(materia);
        if (inf.length() > 0) profesor = inf;
      }

      // profesor required when materia specified
      auto profs = getProfessorsForMateriaLocal(materia);
      bool profOk = false;
      for (auto &p : profs) if (p == profesor) { profOk = true; break; }
      if (!profOk) {
        String html = htmlHeader("Error - Profesor");
        html += "<div class='card'><h3>Profesor inválido para la materia</h3><p class='small'>Seleccione un profesor válido para la materia o regístrelo en Materias/Maestros.</p>";
        html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Ir a Materias</a> <a class='btn btn-red' href='/capture_individual'>Volver</a></p></div>" + htmlFooter();
        server.send(200, "text/html", html);
        return;
      }
    }

    // if uid already registered in this materia -> dedicated page (do not accept)
    if (materia.length() > 0 && userExistsUidMateriaExact(uid, materia)) {
      String html = htmlHeader("Duplicado - Ya registrado");
      html += "<div class='card'><h3 style='color:red;'>El alumno ya está registrado en esa materia</h3>";
      html += "<p class='small'>El UID <b>" + escapeHTML(uid) + "</b> ya figura registrado en la materia <b>" + escapeHTML(materia) + "</b>.</p>";
      html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/'>Inicio</a> <a class='btn btn-green' href='/students?materia=" + escapeHTML(materia) + "'>Ver lista de alumnos</a> <a class='btn btn-red' href='/capture_individual'>Volver a captura</a></p>";
      html += "</div>" + htmlFooter();
      server.send(200, "text/html", html);
      return;
    }

    // OK: persist user record (materia puede estar vacía)
    String created = nowISO();
    String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"" + created + "\"";
    if (!appendLineToFile(USERS_FILE, line)) { server.send(500, "text/plain", "Error guardando usuario"); return; }

    // attendance
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura\"";
    if (!appendLineToFile(ATT_FILE, rec)) { server.send(500, "text/plain", "Error guardando attendance"); return; }

    // reset capture
    captureMode = false; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

    String html = htmlHeader("Registrado correctamente");
    html += "<div class='card'><h3>✅ Usuario registrado correctamente.</h3>";
    html += "<p class='small'>Nombre: " + escapeHTML(name) + " — Cuenta: " + escapeHTML(account) + "</p>";
    html += "<p class='small'>Materia: " + (materia.length() ? escapeHTML(materia) : String("-")) + " — Profesor: " + (profesor.length() ? escapeHTML(profesor) : String("-")) + "</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/students_all'>Volver a Alumnos</a> <a class='btn btn-green' href='/capture_individual?target=students'>Capturar otro</a></p>";
    html += "</div>" + htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  // FLOW: teachers
  if (target == "teachers") {
    // account uniqueness checked above
    String created = nowISO();
    String teacherLine = "\"" + uid + "\",\"" + name + "\",\"" + account + "\",\"\",\"" + created + "\"";
    if (!appendLineToFile(TEACHERS_FILE, teacherLine)) { server.send(500, "text/plain", "Error guardando maestro"); return; }

    captureMode = false; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

    String html = htmlHeader("Maestro registrado");
    html += "<div class='card'><h3>✅ Maestro registrado correctamente.</h3>";
    html += "<p class='small'>Nombre: " + escapeHTML(name) + " — Cuenta: " + escapeHTML(account) + "</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/teachers_all'>Volver a Maestros</a> <a class='btn btn-green' href='/capture_individual?target=teachers'>Capturar otro</a></p>";
    html += "</div>" + htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  server.send(400, "text/plain", "target inválido");
}

// --------------------------------------------------
// start/stop helpers
// --------------------------------------------------
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

// --------------------------------------------------
// EDIT page/post (mantengo lo existente, igual funcionalidad)
// --------------------------------------------------
static String sanitizeReturnToLocal(const String &rt) {
  if (rt.length() > 0 && rt[0] == '/') return rt;
  return String("/students_all");
}

void capture_individual_editPage() {
  if (!server.hasArg("uid")) { server.send(400, "text/plain", "uid required"); return; }
  String uid = server.arg("uid");
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : "/students_all";
  return_to = sanitizeReturnToLocal(return_to);

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

  auto courses2 = loadCourses();
  std::vector<String> materias;
  for (auto &c : courses2) {
    bool ok = true;
    for (auto &m : materias) if (m == c.materia) { ok = false; break; }
    if (ok) materias.push_back(c.materia);
  }

  String foundProfesor = inferProfessorForMateria(foundMateria);

  String html = htmlHeader("Editar Usuario");
  html += "<div class='card'><h2>Editar Usuario</h2>";
  html += "<form method='POST' action='/capture_edit_post'>";
  html += "<input type='hidden' name='uid' value='" + escapeHTML(uid) + "'>";
  html += "<input type='hidden' name='return_to' value='" + escapeHTML(return_to) + "'>";
  html += "<input type='hidden' name='source' value='" + escapeHTML(source) + "'>";
  html += "<label>UID (no editable):</label><br>";
  html += "<input readonly style='background:#eee;' value='" + escapeHTML(uid) + "'><br><br>";
  html += "<label>Nombre:</label><br>";
  html += "<input name='name' required value='" + escapeHTML(foundName) + "'><br><br>";
  html += "<label>Cuenta:</label><br>";
  html += "<input name='account' required maxlength='7' minlength='7' value='" + escapeHTML(foundAccount) + "'><br><br>";

  if (source == "users") {
    html += "<label>Materia:</label><br><select id='edit_materia' name='materia'>";
    html += "<option value=''>-- Ninguna --</option>";
    for (auto &m : materias) {
      String sel = (m == foundMateria) ? " selected" : "";
      html += "<option value='" + escapeHTML(m) + "'" + sel + ">" + escapeHTML(m) + "</option>";
    }
    html += "</select><br><br>";

    html += "<label>Profesor:</label><br>";
    html += "<select id='edit_profesor' name='profesor'><option value=''>-- Ninguno --</option>";
    if (foundProfesor.length()) {
      html += "<option value='" + escapeHTML(foundProfesor) + "' selected>" + escapeHTML(foundProfesor) + "</option>";
    }
    html += "</select><br><br>";
  } else {
    html += "<input type='hidden' name='materia' value=''>\n";
    html += "<input type='hidden' name='profesor' value=''>\n";
    html += "<p class='small'>Este usuario es un maestro; las materias se gestionan por separado.</p>";
  }

  html += "<label>Registrado:</label><br>";
  html += "<div style='padding:6px;background:#f5f7f5;border-radius:4px;'>" + escapeHTML(foundCreated) + "</div><br>";
  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Guardar</button>";
  html += "<a class='btn btn-red' href='" + escapeHTML(return_to) + "'>Cancelar</a>";
  html += "</div></form></div>" + htmlFooter();

  html += R"rawliteral(
<script>
document.addEventListener('DOMContentLoaded', function(){
  var mat = document.getElementById('edit_materia');
  var prof = document.getElementById('edit_profesor');
  function clearProf(){ if(prof) { prof.innerHTML = '<option value="">-- Ninguno --</option>'; } }
  if(mat){
    mat.addEventListener('change', function(){
      var val = mat.value || '';
      clearProf();
      if(!val) return;
      fetch('/profesores_for?materia=' + encodeURIComponent(val))
        .then(r=>r.json())
        .then(j=>{
          if(!j || !j.profesores) return;
          for(var i=0;i<j.profesores.length;i++){
            var o = document.createElement('option');
            o.value = j.profesores[i];
            o.textContent = j.profesores[i];
            prof.appendChild(o);
          }
        }).catch(e=>{});
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
  String materia = server.hasArg("materia") ? server.arg("materia") : String();
  materia.trim();
  String return_to = sanitizeReturnToLocal(server.arg("return_to"));
  String source = server.arg("source");

  if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }
  if (name.length() == 0) { server.send(400, "text/plain", "Nombre vacío"); return; }
  if (account.length() != 7) { server.send(400, "text/plain", "Cuenta inválida"); return; }
  for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  // si cambias la cuenta asegúrate que no exista en otro UID
  auto accFound = findByAccount(account);
  if (accFound.first.length() && accFound.first != uid) {
    server.send(400, "text/plain", "Cuenta duplicada con otro usuario");
    return;
  }

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
