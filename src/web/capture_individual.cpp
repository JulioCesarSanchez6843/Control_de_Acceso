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
  // Evita problemas de sobrecarga en algunos toolchains pasando String(...) a replace
  String o = s;
  o.replace(String("\\"), String("\\\\"));
  o.replace(String("\""), String("\\\""));
  o.replace(String("\n"), String("\\n"));
  o.replace(String("\r"), String("\\r"));
  return o;
}

// HTML escape (utilizada por esta unidad)
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

// Comprueba si uid existe en USERS_FILE
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

// Comprueba si uid existe en TEACHERS_FILE
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

// Devuelve el nombre del usuario para uid+materia si existe, si no el primer nombre encontrado para uid, si no vacio
static String getUserNameForUidMateria(const String &uid, const String &materia) {
  String name = "";
  if (uid.length() == 0) return name;
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) return name;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 1 && c[0] == uid) {
      if (c.size() > 1 && c[1].length()) {
        // prefer exact materia match if provided
        if (materia.length() > 0 && c.size() > 3 && c[3] == materia) { name = c[1]; break; }
        if (name.length() == 0) name = c[1]; // keep first found as fallback
      }
    }
  }
  f.close();
  return name;
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

  // Usar el header global de web_common
  String html = htmlHeader("Capturar - Individual");

  // --- Estilos LOCALES y discretos para mejorar organización del formulario ---
  // No sobreescriben el header global; solo afectan a esta página.
  html += R"rawliteral(
<style>
/* Scoped small form improvements */
.capture-card { max-width: 900px; margin: 6px auto 20px auto; padding: 18px; }
.form-grid { display:grid; grid-template-columns: 1fr 1fr; gap:12px; align-items:start; }
.form-row{ display:flex; flex-direction:column; }
.form-row.full{ grid-column: 1 / -1; }
.form-row label{ font-size:0.95rem; color:#1f2937; margin-bottom:6px; font-weight:600; }
input[type="text"], input[type="tel"], select, input[readonly] { padding:10px 12px; border-radius:8px; border:1px solid #e6eef6; background:#fff; font-size:0.95rem; }
input[readonly]{ background:#f8fafc; color:#274151; }
.lead.small { margin-top:4px; color:#475569; font-size:0.95rem; }
.warn { display:none; border-radius:8px; padding:10px; margin-top:10px; font-weight:600; max-width:100%; box-sizing:border-box; }
.form-actions { display:flex; gap:12px; justify-content:center; margin-top:14px; flex-wrap:wrap; }
@media (max-width:720px) { .form-grid { grid-template-columns:1fr; } .capture-card{ margin:10px; } }
</style>
)rawliteral";

  // Contenido (hereda el .card global)
  html += "<div class='card capture-card'>";
  html += "<h2>Captura Individual</h2>";

  // Mensaje específico según el target
  if (target == "teachers") {
    html += "<p class='lead small'>Acerca la tarjeta del maestro. El sistema verificará automáticamente si la tarjeta está disponible.</p>";
  } else {
    html += "<p class='lead small'>Acerca la tarjeta del alumno. UID autocompletará nombre y cuenta si existe; seleccione materia si desea asignar/añadir una materia.</p>";
  }

  // Modo info
  html += "<div style='background:#f1f5f9;border:1px solid #e2e8f0;padding:10px;border-radius:8px;margin-bottom:12px;color:#0f172a;font-weight:600;'>";
  if (target == "teachers") html += "Modo: Maestro — Las materias se gestionan por separado.";
  else html += "Modo: Alumno — Puede asignar materia y profesor al guardarlo.";
  html += "</div>";

  // Formulario: mantener ids y nombres exactamente iguales
  html += "<form id='capForm' method='POST' action='/capture_confirm' novalidate>";
  html += "<input type='hidden' name='target' value='" + escapeHTML(target) + "'>";

  html += "<div class='form-grid'>";

  html += "<div class='form-row full'><label for='uid'>UID (autocompleta):</label>";
  html += "<input id='uid' name='uid' readonly></div>";

  html += "<div class='form-row'><label for='name'>Nombre:</label>";
  html += "<input id='name' name='name' required></div>";

  html += "<div class='form-row'><label for='account'>Cuenta (7 dígitos):</label>";
  html += "<input id='account' name='account' required maxlength='7' minlength='7' pattern='[0-9]{7}'></div>";

  if (target == "students") {
    html += "<div class='form-row'><label for='materia'>Materia:</label>";
    html += "<select id='materia' name='materia'>";
    html += "<option value=''>-- Ninguna --</option>";
    for (auto &m : materias) html += "<option value='" + escapeHTML(m) + "'>" + escapeHTML(m) + "</option>";
    html += "</select></div>";

    html += "<div class='form-row'><label for='profesor'>Profesor:</label>";
    html += "<select id='profesor' name='profesor' disabled>";
    html += "<option value=''>-- Ninguno --</option>";
    html += "</select></div>";
  } else {
    // ocultos para maestros (la lógica requiere esos campos)
    html += "<input type='hidden' name='materia' value=''>\n";
    html += "<input type='hidden' name='profesor' value=''>\n";
  }

  html += "</div>"; // cierre form-grid

  // Div de advertencias (usar id warn para control centralizado)
  html += "<div id='warn' class='warn'></div>";

  // Botones
  html += "<div class='form-actions'>";
  html += "<button id='submitBtn' type='submit' class='btn btn-green' disabled>Confirmar</button>";
  html += "<a class='btn btn-red' href='" + escapeHTML(return_page) + "' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";

  html += "</form></div>"; // cierre card + form

  // JS: mantener lógica original, pero centralizar setWarn() with auto-hide 5s
  // Also: avoid overwriting inputs the user has already typed into.
  html += R"rawliteral(
<script>
/* Central warning helper: muestra mensaje y lo oculta automáticamente tras 5s */
var __warnTimer = null;
function setWarn(msg){
  var warn = document.getElementById('warn');
  if(!warn) return;
  if(msg){
    warn.textContent = msg;
    warn.style.display = 'block';
    warn.style.background = '#fff7ed';
    warn.style.border = '1px solid #ffd8a8';
    warn.style.color = '#7b2e00';
    warn.style.padding = '10px';
    warn.style.borderRadius = '6px';
    if(__warnTimer) { clearTimeout(__warnTimer); __warnTimer = null; }
    __warnTimer = setTimeout(function(){
      try{
        warn.style.transition = 'opacity 0.25s';
        warn.style.opacity = '0';
        setTimeout(function(){ warn.style.display='none'; warn.style.opacity='1'; warn.textContent=''; }, 260);
      }catch(e){}
    }, 5000); // 5000 ms = 5s
  } else {
    if(__warnTimer){ clearTimeout(__warnTimer); __warnTimer = null; }
    warn.style.display='none';
    warn.textContent='';
  }
}

/* pollUID mantiene la lógica: pide al servidor y actualiza campos
   IMPORTANT: no sobreescribe campos si el usuario ya está editándolos.
*/
function pollUID(){
  var targetInput = document.querySelector("input[name='target']");
  var target = targetInput ? targetInput.value : '';
  fetch('/capture_poll?target=' + encodeURIComponent(target))
    .then(r=>r.json())
    .then(j=>{
      var uidField = document.getElementById('uid');
      var nameField = document.getElementById('name');
      var accField = document.getElementById('account');
      var submitBtn = document.getElementById('submitBtn');
      var matField = document.getElementById('materia');
      var profField = document.getElementById('profesor');

      if(!uidField || !nameField || !accField) return;

      if(j.status === 'waiting'){
        // nothing to do
        isExistingStudent = false;
        updateMateriaRequirement();
      } else if(j.status === 'blocked'){
        // blocked: show warning and disable submit, but DO NOT clear user inputs
        setWarn(j.blocked_message || 'Acción no permitida');
        if(submitBtn) submitBtn.disabled = true;

        // IMPORTANT CHANGE: don't clear fields and don't remove user-edited markers.
        // Preserve whatever the user has typed.

        // Respect server hint to stop capture, but avoid reloading which would clear fields:
        if(j.restart_after_ms){
          setTimeout(function(){
            try { navigator.sendBeacon('/capture_stop'); } catch(e){}
            // do NOT redirect the page - keep user inputs intact
          }, j.restart_after_ms);
        }
      } else if(j.status === 'found'){
        // Fill UID if empty (readonly usually) - safe to overwrite
        if(j.uid && (!uidField.value || uidField.value.trim().length === 0)) uidField.value = j.uid;

        // Only fill name/account IF the user has NOT started editing them.
        if(j.name) {
          if(!nameField.dataset.userEdited && (!nameField.value || nameField.value.trim().length === 0)) {
            nameField.value = j.name;
          }
        }
        if(j.account) {
          if(!accField.dataset.userEdited && (!accField.value || accField.value.trim().length === 0)) {
            accField.value = j.account;
          }
        }

        if(target === 'students') {
          isExistingStudent = j.existing || false;
          updateMateriaRequirement();
        }

        // Clear any warning messages (this only clears the visual message — inputs are preserved)
        setWarn('');

        if(j.blocked && j.blocked_message){
          setWarn(j.blocked_message);
          if(submitBtn) submitBtn.disabled = true;
        } else {
          if(submitBtn) submitBtn.disabled = false;
        }
      }

      setTimeout(pollUID, 700);
    })
    .catch(e => setTimeout(pollUID, 1200));
}

// Variable de control usada por validaciones
let isExistingStudent = false;

// Función para actualizar el requerimiento del campo materia
function updateMateriaRequirement() {
  var matField = document.getElementById('materia');
  if (!matField) return;
  if (isExistingStudent) {
    matField.required = true;
    setWarn('Este alumno ya está registrado. Debe asignar una materia.');
  } else {
    matField.required = false;
    // clear only if warning shows this exact message
    var warn = document.getElementById('warn');
    if(warn && warn.textContent === 'Este alumno ya está registrado. Debe asignar una materia.') setWarn('');
  }
}

document.addEventListener('DOMContentLoaded', function(){
  var mat = document.getElementById('materia');
  var prof = document.getElementById('profesor');
  var submitBtn = document.getElementById('submitBtn');

  // mark fields as user-edited when the user types so pollUID won't overwrite them
  var nameField = document.getElementById('name');
  var accField = document.getElementById('account');
  if(nameField){
    nameField.addEventListener('input', function(){ nameField.dataset.userEdited = '1'; });
    // if user submits or resets, marker will be cleared on submit handler
  }
  if(accField){
    accField.addEventListener('input', function(){ accField.dataset.userEdited = '1'; });
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
      if(submitBtn) submitBtn.disabled = false;
      return;
    }
    if(!prof){ if(submitBtn) submitBtn.disabled = true; setWarn('Seleccione un profesor para la materia.'); return; }
    if(prof.options.length <= 1){
      setWarn('La materia seleccionada no tiene profesores registrados. Regístrela primero en Materias.');
      if(submitBtn) submitBtn.disabled = true;
      return;
    }
    if(prof.options.length === 2){
      prof.selectedIndex = 1; prof.disabled = true; setWarn(''); if(submitBtn) submitBtn.disabled = false; return;
    }
    prof.disabled = false;
    if(prof.value){ setWarn(''); if(submitBtn) submitBtn.disabled = false; } else { setWarn('Seleccione un profesor para la materia.'); if(submitBtn) submitBtn.disabled = true; }
  }

  if(mat){
    mat.addEventListener('change', function(){
      clearProf();
      setWarn('');
      if(submitBtn) submitBtn.disabled = true;
      var val = mat.value || '';
      if(!val){ clearProf(); setWarn(''); if(submitBtn) submitBtn.disabled = false; return; }
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
    
    // Validación específica: si el alumno ya existe, materia es obligatoria
    if (isExistingStudent && (!matSel || !matSel.value)) {
      ev.preventDefault(); 
      setWarn('Este alumno ya está registrado. Debe asignar una materia.'); 
      return false;
    }
    
    if(matSel && matSel.value){
      if(profSel && !profSel.value){ ev.preventDefault(); setWarn('Seleccione un profesor para la materia.'); return false; }
    }

    // On successful submit we can clear user-edited markers to avoid stale state in next capture
    try {
      var nf = document.getElementById('name');
      var af = document.getElementById('account');
      if(nf) delete nf.dataset.userEdited;
      if(af) delete af.dataset.userEdited;
    } catch(e){}
  });

});


// Enviar stop si se cierra la pestaña
window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e){} });
pollUID();
</script>
)rawliteral";

  // Footer global (heredado)
  html += htmlFooter();

  server.send(200, "text/html", html);
}

// --------------------------------------------------
// Poll endpoint JSON - MODIFICADO para incluir información de alumno existente
// --------------------------------------------------
void capture_individual_poll() {
  if (captureUID.length() == 0) {
    server.send(200, "application/json", "{\"status\":\"waiting\"}");
    return;
  }

  // copy local values (but note: for blocked cases we will NOT return UID/name/account)
  String nameOut = captureName;
  String accountOut = captureAccount;
  String uidSnapshot = captureUID; // snapshot if needed internally

  // Try to get stored name/account (from either file)
  String storedLine = findAnyUserLineByUID(uidSnapshot);
  if (storedLine.length() > 0) {
    auto parts = parseQuotedCSVLine(storedLine);
    if (parts.size() > 1) nameOut = parts[1];
    if (parts.size() > 2) accountOut = parts[2];
    // intentionally do NOT set materia/profesor here to allow re-capture assignment
  }

  // get optional target param
  String target = "";
  if (server.hasArg("target")) {
    target = server.arg("target");
    target.trim();
  }

  // ----------------------
  // BLOCK CASES (server will NOT include UID/name/account in response)
  // ----------------------

  // CASO 1: Si target==students pero UID existe en teachers -> block & restart
  if (target == "students" && uidExistsInTeachers(uidSnapshot)) {
    // Clear the capture so the system does not keep the UID / show it on any display
    captureUID = "";
    captureDetectedAt = 0;

    String msg = "Esta tarjeta ya está registrada como maestro. No puede registrarse como alumno.";
    String j = "{\"status\":\"blocked\",\"blocked_message\":\"" + jsonEscapeLocal(msg) + "\"";
    j += ",\"restart_after_ms\":5000}";
    server.send(200, "application/json", j);
    return;
  }

  // CASO 2: Si target==teachers y UID existe en users -> block & restart
  if (target == "teachers" && uidExistsInUsers(uidSnapshot)) {
    captureUID = "";
    captureDetectedAt = 0;

    String msg = "Esta tarjeta pertenece a un alumno. No puede registrarse como maestro.";
    String j = "{\"status\":\"blocked\",\"blocked_message\":\"" + jsonEscapeLocal(msg) + "\"";
    j += ",\"restart_after_ms\":5000}";
    server.send(200, "application/json", j);
    return;
  }

  // CASO 3: Si target==teachers y UID ya existe en teachers -> block & restart
  if (target == "teachers" && uidExistsInTeachers(uidSnapshot)) {
    captureUID = "";
    captureDetectedAt = 0;

    String msg = "Este maestro ya está registrado, no puede capturarse de nuevo.";
    String j = "{\"status\":\"blocked\",\"blocked_message\":\"" + jsonEscapeLocal(msg) + "\"";
    j += ",\"restart_after_ms\":5000}";
    server.send(200, "application/json", j);
    return;
  }

  // ----------------------
  // NORMAL FOUND CASE - TARJETA LIBRE/NUEVA
  // ----------------------
  String j = "{\"status\":\"found\",\"uid\":\"" + jsonEscapeLocal(uidSnapshot) + "\"";
  if (nameOut.length()) j += ",\"name\":\"" + jsonEscapeLocal(nameOut) + "\"";
  if (accountOut.length()) j += ",\"account\":\"" + jsonEscapeLocal(accountOut) + "\"";

  // AÑADIDO: Información sobre si el alumno ya existe (solo para target students)
  if (target == "students") {
    bool existing = uidExistsInUsers(uidSnapshot);
    j += ",\"existing\":" + String(existing ? "true" : "false");
  }

  // also check if the account (number) is already used by other UID (across files)
  if (accountOut.length()) {
    auto accFound = findByAccount(accountOut);
    if (accFound.first.length() && accFound.first != uidSnapshot) {
      String msg = "La cuenta " + accountOut + " ya está asociada a otra tarjeta (UID " + accFound.first + ").";
      // Add as blocked indicator so UI will show message and disable submit
      j += ",\"blocked\":true";
      j += ",\"blocked_message\":\"" + jsonEscapeLocal(msg) + "\"";
    }
  }

  j += "}";
  server.send(200, "application/json", j);
}

// --------------------------------------------------
// Confirm (POST) - MODIFICADO para validar materia obligatoria en alumnos existentes
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

  // AÑADIDO: Validación específica - Si el alumno ya existe, materia es obligatoria
  if (target == "students" && uidExistsInUsers(uid) && materia.length() == 0) {
    String html = htmlHeader("Error - Materia obligatoria");
    html += "<div class='card'><h3 style='color:#b00020;'>Materia obligatoria para alumno existente</h3>";
    html += "<p class='small'>Este alumno ya está registrado en el sistema. Debe asignar una materia cuando se vuelve a capturar.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/capture_individual?target=students'>Volver a captura</a> <a class='btn btn-green' href='/students_all'>Ver alumnos</a></p>";
    html += "</div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // server-side: check account uniqueness (across users and teachers)
  auto accFound = findByAccount(account);
  if (accFound.first.length() && accFound.first != uid) {
    String foundUID = accFound.first;
    String foundSource = accFound.second;

    // gather name & materias for foundUID
    String foundName = "";
    std::vector<String> materiasList;
    if (foundSource == "users") {
      File fu = SPIFFS.open(USERS_FILE, FILE_READ);
      if (fu) {
        while (fu.available()) {
          String l = fu.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
          auto p = parseQuotedCSVLine(l);
          if (p.size() >= 1 && p[0] == foundUID) {
            if (p.size() > 1) foundName = p[1];
            if (p.size() > 3 && p[3].length()) {
              bool exists = false;
              for (auto &m : materiasList) if (m == p[3]) { exists = true; break; }
              if (!exists) materiasList.push_back(p[3]);
            }
          }
        }
        fu.close();
      }
    } else { // teachers
      File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
      if (ft) {
        while (ft.available()) {
          String l = ft.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
          auto p = parseQuotedCSVLine(l);
          if (p.size() >= 1 && p[0] == foundUID) {
            if (p.size() > 1) foundName = p[1];
            if (p.size() > 3 && p[3].length()) {
              bool exists = false;
              for (auto &m : materiasList) if (m == p[3]) { exists = true; break; }
              if (!exists) materiasList.push_back(p[3]);
            }
          }
        }
        ft.close();
      }
    }

    // build HTML informative page (sin iconos)
    String btnStyle = "padding:8px 12px;border-radius:6px;text-decoration:none;min-width:140px;display:inline-block;text-align:center;";

    String html = htmlHeader("Error - Cuenta duplicada");
    html += "<div class='card'>";
    html += "<h3 style='color:#b00020;'>Cuenta ya registrada</h3>";
    html += "<p class='small'>La cuenta <b>" + escapeHTML(account) + "</b> ya está registrada con UID <b>" + escapeHTML(foundUID) + "</b> en <b>" + escapeHTML(foundSource) + "</b>. No se puede usar la misma cuenta para otro usuario.</p>";

    if (foundName.length()) html += "<p><b>Nombre:</b> " + escapeHTML(foundName) + "</p>";

    if (materiasList.size() > 0) {
      String mats = "";
      for (size_t i=0;i<materiasList.size();++i) {
        if (i) mats += "; ";
        mats += materiasList[i];
      }
      html += "<p><b>Materias registradas:</b> " + escapeHTML(mats) + "</p>";
    } else {
      html += "<p><i>No tiene materias asociadas.</i></p>";
    }

    html += "<p class='small' style='margin-top:10px;color:#333;'>Si desea editar este usuario pulse el botón Editar usuario. También puede cancelar la captura con el botón rojo.</p>";

    html += "<div style='display:flex;gap:12px;justify-content:center;margin-top:12px;'>";
    // ORDER CHANGED: Volver a Captura (verde) in place of previous Inicio
    html += "<a class='btn btn-green' href='/capture_individual?target=" + escapeHTML(target) + "' style='background:#27ae60;color:#fff;" + btnStyle + "'>Volver a Captura</a>";

    // Editar usuario (amarillo)
    if (foundUID.length()) {
      if (foundSource == "users") html += "<a class='btn btn-yellow' href='/capture_edit?uid=" + escapeHTML(foundUID) + "&return_to=/students_all' style='background:#f1c40f;color:#000;" + btnStyle + "'>Editar usuario</a>";
      else html += "<a class='btn btn-yellow' href='/capture_edit?uid=" + escapeHTML(foundUID) + "&return_to=/teachers_all' style='background:#f1c40f;color:#000;" + btnStyle + "'>Editar usuario</a>";
    } else {
      html += "<a class='btn btn-yellow' href='/' style='background:#f1c40f;color:#000;" + btnStyle + "'>Editar usuario</a>";
    }

    // Now put Inicio where Volver used to be (blue)
    html += "<a class='btn btn-blue' href='/' style='background:#3498db;color:#fff;" + btnStyle + "'>Inicio</a>";

    // Cancelar registro (rojo) - como formulario para POST
    html += "<form method='POST' action='/cancel_capture' style='display:inline;margin:0;'>"
            "<input type='hidden' name='return_to' value='/' />"
            "<button type='submit' class='btn btn-red' style='background:#d9534f;color:#fff;" + btnStyle + "border:none;cursor:pointer;'>Cancelar registro</button>"
            "</form>";
    html += "</div></div>" + htmlFooter();

    server.send(200, "text/html", html);
    return;
  }

  // PROHIBIR que una tarjeta registrada como alumno sea registrada como maestro y viceversa (POST side)
  if (target == "teachers") {
    if (uidExistsInUsers(uid)) {
      // tarjeta ya usada como alumno -> denegar (this is a double-check, poll already blocks earlier)
      String foundName="", foundAccount="";
      std::vector<String> materiasList;
      File fu = SPIFFS.open(USERS_FILE, FILE_READ);
      if (fu) {
        while (fu.available()) {
          String l = fu.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
          auto p = parseQuotedCSVLine(l);
          if (p.size() >= 1 && p[0] == uid) {
            if (p.size() > 1) foundName = p[1];
            if (p.size() > 2) foundAccount = p[2];
            if (p.size() > 3 && p[3].length()) {
              bool exists = false;
              for (auto &m : materiasList) if (m == p[3]) { exists = true; break; }
              if (!exists) materiasList.push_back(p[3]);
            }
          }
        }
        fu.close();
      }

      String html = htmlHeader("No permitido - Tarjeta en uso");
      html += "<div class='card'>";
      html += "<h3 style='color:#b00020;'>Tarjeta ya registrada como alumno</h3>";
      html += "<p class='small'>El UID <b>" + escapeHTML(uid) + "</b> ya está registrado como <b>alumno</b> en el sistema. No puede registrarse como maestro con la misma tarjeta.</p>";
      if (foundName.length()) html += "<p><b>Nombre:</b> " + escapeHTML(foundName) + "</p>";
      if (foundAccount.length()) html += "<p><b>Cuenta:</b> " + escapeHTML(foundAccount) + "</p>";
      if (materiasList.size()) {
        String mats="";
        for (size_t i=0;i<materiasList.size();++i){ if(i) mats += "; "; mats += materiasList[i]; }
        html += "<p><b>Materias:</b> " + escapeHTML(mats) + "</p>";
      }
      html += "<p class='small' style='margin-top:10px;'>Si desea editar ese registro pulse el botón amarillo, o cancele la captura con el botón rojo.</p>";
      html += "<div style='display:flex;gap:12px;justify-content:center;margin-top:12px;'>";
      html += "<a class='btn btn-green' href='/capture_individual?target=teachers' style='background:#27ae60;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;'>Volver a Captura</a>";
      html += "<a class='btn btn-yellow' href='/capture_edit?uid=" + escapeHTML(uid) + "&return_to=/students_all' style='background:#f1c40f;color:#000;min-width:140px;padding:8px 12px;border-radius:6px;'>Editar usuario</a>";
      html += "<a class='btn btn-blue' href='/' style='background:#3498db;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;'>Inicio</a>";
      html += "<form method='POST' action='/cancel_capture' style='display:inline;margin:0;'>"
              "<input type='hidden' name='return_to' value='/' />"
              "<button type='submit' class='btn btn-red' style='background:#d9534f;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;border:none;cursor:pointer;'>Cancelar registro</button>"
              "</form>";
      html += "</div></div>" + htmlFooter();
      server.send(200, "text/html", html);
      return;
    }
  } else if (target == "students") {
    if (uidExistsInTeachers(uid)) {
      // tarjeta ya usada como maestro -> denegar (double-check)
      String foundName="", foundAccount="";
      File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
      if (ft) {
        while (ft.available()) {
          String l = ft.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
          auto p = parseQuotedCSVLine(l);
          if (p.size() >= 1 && p[0] == uid) {
            if (p.size() > 1) foundName = p[1];
            if (p.size() > 2) foundAccount = p[2];
          }
        }
        ft.close();
      }

      String html = htmlHeader("No permitido - Tarjeta en uso");
      html += "<div class='card'>";
      html += "<h3 style='color:#b00020;'>Tarjeta ya registrada como maestro</h3>";
      html += "<p class='small'>El UID <b>" + escapeHTML(uid) + "</b> ya está registrado como <b>maestro</b> en el sistema. No puede registrarse como alumno con la misma tarjeta.</p>";
      if (foundName.length()) html += "<p><b>Nombre:</b> " + escapeHTML(foundName) + "</p>";
      if (foundAccount.length()) html += "<p><b>Cuenta:</b> " + escapeHTML(foundAccount) + "</p>";
      html += "<p class='small' style='margin-top:10px;'>Si desea editar ese registro pulse el botón amarillo, o cancele la captura con el botón rojo.</p>";
      html += "<div style='display:flex;gap:12px;justify-content:center;margin-top:12px;'>";
      html += "<a class='btn btn-green' href='/capture_individual?target=students' style='background:#27ae60;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;'>Volver a Captura</a>";
      html += "<a class='btn btn-yellow' href='/capture_edit?uid=" + escapeHTML(uid) + "&return_to=/teachers_all' style='background:#f1c40f;color:#000;min-width:140px;padding:8px 12px;border-radius:6px;'>Editar maestro</a>";
      html += "<a class='btn btn-blue' href='/' style='background:#3498db;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;'>Inicio</a>";
      html += "<form method='POST' action='/cancel_capture' style='display:inline;margin:0;'>"
              "<input type='hidden' name='return_to' value='/' />"
              "<button type='submit' class='btn btn-red' style='background:#d9534f;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;border:none;cursor:pointer;'>Cancelar registro</button>"
              "</form>";
      html += "</div></div>" + htmlFooter();
      server.send(200, "text/html", html);
      return;
    }
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
      // get student name for uid+materia (prefer exact materia match)
      String studentName = getUserNameForUidMateria(uid, materia);
      String html = htmlHeader("Duplicado - Ya registrado");
      html += "<div class='card'><h3 style='color:red;'>El alumno ya está registrado en esa materia</h3>";
      html += "<p class='small'>El UID <b>" + escapeHTML(uid) + "</b> ya figura registrado en la materia <b>" + escapeHTML(materia) + "</b>.</p>";
      if (studentName.length()) html += "<p><b>Nombre:</b> " + escapeHTML(studentName) + "</p>";
      html += "<p style='margin-top:8px'><a class='btn btn-green' href='/students?materia=" + escapeHTML(materia) + "'>Ver lista de alumnos</a> <a class='btn btn-blue' href='/' style='margin-left:8px;'>Inicio</a> <a class='btn btn-red' href='/capture_individual' style='margin-left:8px;'>Volver a captura</a></p>";
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
    html += "<div class='card'><h3>Usuario registrado correctamente.</h3>";
    html += "<p class='small'>Nombre: " + escapeHTML(name) + " — Cuenta: " + escapeHTML(account) + "</p>";
    html += "<p class='small'>Materia: " + (materia.length() ? escapeHTML(materia) : String("-")) + " — Profesor: " + (profesor.length() ? escapeHTML(profesor) : String("-")) + "</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/students_all'>Volver a Alumnos</a> <a class='btn btn-green' href='/capture_individual?target=students'>Capturar otro</a></p>";
    html += "</div>" + htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  // FLOW: teachers
  if (target == "teachers") {
    // account uniqueness already checked above
    String created = nowISO();
    String teacherLine = "\"" + uid + "\",\"" + name + "\",\"" + account + "\",\"\",\"" + created + "\"";
    if (!appendLineToFile(TEACHERS_FILE, teacherLine)) { server.send(500, "text/plain", "Error guardando maestro"); return; }

    captureMode = false; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

    String html = htmlHeader("Maestro registrado");
    html += "<div class='card'><h3>Maestro registrado correctamente.</h3>";
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

  // En editPage no hay warn dinámico en general, pero si quieres que aparezca, podemos agregar similar ayuda JS.
  html += R"rawliteral(
<script>
/* Si se mostrara un warn dinámico aquí, setWarn podría reutilizarse.
   Mantengo solo la lógica de carga de profesores (sin cambiar comportamiento). */
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
