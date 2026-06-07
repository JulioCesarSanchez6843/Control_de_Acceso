// src/web/capture_individual.cpp
#include <Arduino.h>
#include <FS.h>
#include <ctype.h>
#include <vector>
#include <utility>

#include "capture.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include "display.h"
#include "edit.h"
#include "db_sync.h"
#include "time_utils.h"

#include <ArduinoJson.h>

// JSON escape
static String jsonEscapeLocal(const String &s) {
  String o = s;
  o.replace(String("\\"), String("\\\\"));
  o.replace(String("\""), String("\\\""));
  o.replace(String("\n"), String("\\n"));
  o.replace(String("\r"), String("\\r"));
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

// --------------------------------------------------
// Helpers DB: materias / alumnos
// --------------------------------------------------
struct MateriaDbRow {
  String materia;
  String profesor;
};

struct AlumnoDbRow {
  String id;
  String uid;
  String name;
  String account;
  String materia;
  String created;
};

static String jsonGetAnyString(const JsonObjectConst &obj, const char* const* keys, size_t keyCount) {
  for (size_t i = 0; i < keyCount; ++i) {
    const char* k = keys[i];
    if (!obj.containsKey(k)) continue;
    JsonVariantConst v = obj[k];
    if (v.isNull()) continue;
    String s = v.as<String>();
    s.trim();
    if (s.length()) return s;
  }
  return "";
}

static bool parseMateriasResponse(const String &body, std::vector<MateriaDbRow> &out) {
  out.clear();
  if (!body.length()) return false;

  size_t cap = body.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("WARN: error parseando listMaterias(): ");
    Serial.println(err.c_str());
    return false;
  }

  auto handleObj = [&](JsonObjectConst obj) {
    const char* materiaKeys[] = {"materia", "subject", "name"};
    const char* profesorKeys[] = {"profesor", "teacher", "maestro", "nombre_profesor"};
    MateriaDbRow r;
    r.materia  = jsonGetAnyString(obj, materiaKeys,  3);
    r.profesor = jsonGetAnyString(obj, profesorKeys, 4);
    if (r.materia.length()) out.push_back(r);
  };

  if (doc.is<JsonArray>()) {
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
      if (!v.is<JsonObjectConst>()) continue;
      handleObj(v.as<JsonObjectConst>());
    }
    return true;
  }

  if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "materias", "rows", "result", "items"};
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArray>()) {
        JsonArrayConst arr = root[key].as<JsonArrayConst>();
        for (JsonVariantConst v : arr) {
          if (!v.is<JsonObjectConst>()) continue;
          handleObj(v.as<JsonObjectConst>());
        }
        return true;
      }
    }
    handleObj(root);
    return true;
  }

  return false;
}

static bool parseAlumnosResponse(const String &body, std::vector<AlumnoDbRow> &out) {
  out.clear();
  if (!body.length()) return false;

  size_t cap = body.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("WARN: error parseando listAlumnos(): ");
    Serial.println(err.c_str());
    return false;
  }

  auto handleObj = [&](JsonObjectConst obj) {
    const char* idKeys[]      = {"id", "ID"};
    const char* uidKeys[]     = {"rfid_uid", "uid", "alumno_uid", "user_uid"};
    const char* nameKeys[]    = {"name", "nombre", "full_name"};
    const char* accountKeys[] = {"account", "cuenta", "matricula", "account_number"};
    const char* materiaKeys[] = {"materia", "subject"};
    const char* createdKeys[] = {"created", "created_at", "createdAt", "fecha", "timestamp"};

    AlumnoDbRow r;
    r.id      = jsonGetAnyString(obj, idKeys,      2);
    r.uid     = jsonGetAnyString(obj, uidKeys,     4);
    r.name    = jsonGetAnyString(obj, nameKeys,    3);
    r.account = jsonGetAnyString(obj, accountKeys, 4);
    r.materia = jsonGetAnyString(obj, materiaKeys, 2);
    r.created = jsonGetAnyString(obj, createdKeys, 5);

    if (r.uid.length()) out.push_back(r);
  };

  if (doc.is<JsonArray>()) {
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
      if (!v.is<JsonObjectConst>()) continue;
      handleObj(v.as<JsonObjectConst>());
    }
    return true;
  }

  if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "alumnos", "rows", "result", "items"};
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArray>()) {
        JsonArrayConst arr = root[key].as<JsonArrayConst>();
        for (JsonVariantConst v : arr) {
          if (!v.is<JsonObjectConst>()) continue;
          handleObj(v.as<JsonObjectConst>());
        }
        return true;
      }
    }
    handleObj(root);
    return true;
  }

  return false;
}

static std::vector<MateriaDbRow> loadMateriasFromDb() {
  std::vector<MateriaDbRow> out;
  String body = listMaterias();
  if (!body.length()) return out;
  parseMateriasResponse(body, out);
  return out;
}

static std::vector<AlumnoDbRow> loadAlumnosFromDb() {
  std::vector<AlumnoDbRow> out;
  // listAlumnos(materia="") trae todos
  String body = listAlumnos(String());
  if (!body.length()) return out;
  parseAlumnosResponse(body, out);
  return out;
}

static std::vector<AlumnoDbRow> loadAlumnosByUidDb(const String &uid) {
  std::vector<AlumnoDbRow> all = loadAlumnosFromDb();
  std::vector<AlumnoDbRow> out;
  for (auto &r : all) {
    if (r.uid == uid) out.push_back(r);
  }
  return out;
}

// FIX: renombrada de uidExistsInUsersDb -> alumnoExistsByUidDb (consistente con el resto del archivo)
static bool alumnoExistsByUidDb(const String &uid) {
  if (!uid.length()) return false;
  std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
  return !rows.empty();
}

static bool alumnoExistsByUidMateriaDb(const String &uid, const String &materia) {
  if (!uid.length() || !materia.length()) return false;
  std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
  for (auto &r : rows) {
    if (r.materia == materia) return true;
  }
  return false;
}

static String getUserNameForUidMateriaDb(const String &uid, const String &materia) {
  String fallback = "";
  if (!uid.length()) return fallback;

  std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
  for (auto &r : rows) {
    if (r.name.length() && fallback.length() == 0) fallback = r.name;
    if (materia.length() && r.materia == materia && r.name.length()) return r.name;
  }
  return fallback;
}

static std::pair<String, String> findByAccountDb(const String &account) {
  if (account.length() == 0) return std::make_pair(String(""), String(""));

  // Buscar en alumnos (DB)
  std::vector<AlumnoDbRow> rows = loadAlumnosFromDb();
  for (auto &r : rows) {
    if (r.account == account) {
      return std::make_pair(r.uid, String("users"));
    }
  }

  // Buscar en profesores (DB)
  String profPayload = listProfesores();
  if (profPayload.length()) {
    DynamicJsonDocument doc(16 * 1024);
    if (!deserializeJson(doc, profPayload)) {
      JsonVariantConst root = doc.as<JsonVariantConst>();
      auto checkProf = [&](JsonObjectConst obj) -> std::pair<String,String> {
        const char* uidKeys[]     = {"uid", "UID", "rfid_uid"};
        const char* accountKeys[] = {"cuenta", "account", "matricula"};
        String profUid = jsonGetAnyString(obj, uidKeys,     3);
        String profAcc = jsonGetAnyString(obj, accountKeys, 3);
        if (profAcc == account && profUid.length()) {
          return std::make_pair(profUid, String("teachers"));
        }
        return std::make_pair(String(""), String(""));
      };

      if (root.is<JsonArrayConst>()) {
        for (JsonVariantConst v : root.as<JsonArrayConst>()) {
          if (!v.is<JsonObjectConst>()) continue;
          auto res = checkProf(v.as<JsonObjectConst>());
          if (res.first.length()) return res;
        }
      } else if (root.is<JsonObjectConst>()) {
        JsonObjectConst o = root.as<JsonObjectConst>();
        const char* wrappers[] = {"data", "profesores", "rows", "result", "items"};
        for (const char* key : wrappers) {
          if (o.containsKey(key) && o[key].is<JsonArrayConst>()) {
            for (JsonVariantConst v : o[key].as<JsonArrayConst>()) {
              if (!v.is<JsonObjectConst>()) continue;
              auto res = checkProf(v.as<JsonObjectConst>());
              if (res.first.length()) return res;
            }
            break;
          }
        }
      }
    }
  }

  return std::make_pair(String(""), String(""));
}

static String findAnyUserLineByUID(const String &uid) {
  if (!uid.length()) return String();

  std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
  if (rows.empty()) return String();

  const auto &r = rows[0];
  String line = "\"" + r.uid + "\",\"" + r.name + "\",\"" + r.account + "\",\"" + r.materia + "\",\"" + r.created + "\"";
  return line;
}

static std::vector<String> getProfessorsForMateriaDb(const String &materia) {
  std::vector<String> out;
  if (materia.length() == 0) return out;

  std::vector<MateriaDbRow> materias = loadMateriasFromDb();
  for (auto &m : materias) {
    if (m.materia == materia && m.profesor.length()) {
      bool ok = true;
      for (auto &p : out) if (p == m.profesor) { ok = false; break; }
      if (ok) out.push_back(m.profesor);
    }
  }
  return out;
}

static String inferProfessorForMateria(const String &materia) {
  if (materia.length() == 0) return String();
  std::vector<String> profs = getProfessorsForMateriaDb(materia);
  if (profs.size() == 1) return profs[0];
  return String();
}

static bool materiaExistsDb(const String &materia) {
  if (materia.length() == 0) return false;
  std::vector<MateriaDbRow> materias = loadMateriasFromDb();
  for (auto &m : materias) {
    if (m.materia == materia) return true;
  }
  return false;
}

// FIX: uidExistsInTeachers ahora consulta la DB en lugar de SPIFFS
static bool uidExistsInTeachers(const String &uid) {
  if (uid.length() == 0) return false;

  String payload = getProfesorByUid(uid);
  if (!payload.length()) return false;

  DynamicJsonDocument doc(8 * 1024);
  if (deserializeJson(doc, payload)) return false;

  // Si la respuesta tiene contenido con un uid válido, el profesor existe
  JsonVariantConst root = doc.as<JsonVariantConst>();

  auto checkUid = [&](JsonObjectConst obj) -> bool {
    const char* uidKeys[] = {"uid", "UID", "rfid_uid"};
    String u = jsonGetAnyString(obj, uidKeys, 3);
    return u.length() > 0;
  };

  if (root.is<JsonObjectConst>()) {
    JsonObjectConst o = root.as<JsonObjectConst>();
    // Puede venir envuelto en data/result/etc
    const char* wrappers[] = {"data", "profesores", "rows", "result", "items"};
    for (const char* key : wrappers) {
      if (o.containsKey(key) && o[key].is<JsonArrayConst>()) {
        for (JsonVariantConst v : o[key].as<JsonArrayConst>()) {
          if (v.is<JsonObjectConst>() && checkUid(v.as<JsonObjectConst>())) return true;
        }
        return false;
      }
    }
    return checkUid(o);
  }

  if (root.is<JsonArrayConst>()) {
    for (JsonVariantConst v : root.as<JsonArrayConst>()) {
      if (v.is<JsonObjectConst>() && checkUid(v.as<JsonObjectConst>())) return true;
    }
  }

  return false;
}

// --------------------------------------------------
// Sincronización remota usando db_sync.cpp
// --------------------------------------------------
static void syncAlumnoToOracle(const String &uid, const String &name,
                                const String &account, const String &materia,
                                const String &createdAt) {
  if (!sendAlumnoRegistro(uid, name, account, materia, createdAt)) {
    Serial.println("WARN: no se pudo sincronizar alumno con Oracle");
  } else {
    Serial.println("DB_SYNC: alumno sincronizado correctamente");
  }

  if (!sendAsistencia(createdAt, uid, name, account, materia, "captura")) {
    Serial.println("WARN: no se pudo sincronizar asistencia de captura");
  } else {
    Serial.println("DB_SYNC: asistencia de captura sincronizada correctamente");
  }
}

static void syncTeacherToOracle(const String &uid, const String &name,
                                 const String &account, const String &createdAt) {
  if (!sendProfesorRegistro(uid, name, account, createdAt)) {
    Serial.println("WARN: no se pudo sincronizar profesor con Oracle");
  } else {
    Serial.println("DB_SYNC: profesor sincronizado correctamente");
  }
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

  String target      = server.hasArg("target") ? server.arg("target") : "students";
  String return_page = (target == "teachers") ? String("/teachers_all") : String("/students_all");

  if (server.hasArg("start") && server.arg("start") == "1") {
    bool isTeacher = (target == "teachers");
    #ifdef USE_DISPLAY
    Serial.println("capture_individual_page: start=1 -> showCaptureEntered()");
    showCaptureEntered(isTeacher);
    showCaptureMode(isTeacher, false);
    #endif
  }

  std::vector<String> materias;
  if (target == "students") {
    auto materiasDb = loadMateriasFromDb();
    for (auto &c : materiasDb) {
      bool ok = true;
      for (auto &m : materias) if (m == c.materia) { ok = false; break; }
      if (ok) materias.push_back(c.materia);
    }
  }

  String html = htmlHeader("Capturar - Individual");
  html += R"rawliteral(
<style>
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

  html += "<div class='card capture-card'>";
  html += "<h2>Captura Individual</h2>";

  if (target == "teachers") {
    html += "<p class='lead small'>Acerca la tarjeta del maestro. El sistema verificará automáticamente si la tarjeta está disponible.</p>";
  } else {
    html += "<p class='lead small'>Acerca la tarjeta del alumno. UID autocompletará nombre y cuenta si existe; seleccione materia si desea asignar/añadir una materia.</p>";
  }

  html += "<div style='background:#f1f5f9;border:1px solid #e2e8f0;padding:10px;border-radius:8px;margin-bottom:12px;color:#0f172a;font-weight:600;'>";
  if (target == "teachers") html += "Modo: Maestro — Las materias se gestionan por separado.";
  else html += "Modo: Alumno — Puede asignar materia y profesor al guardarlo.";
  html += "</div>";

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
    html += "<input type='hidden' name='materia' value=''>\n";
    html += "<input type='hidden' name='profesor' value=''>\n";
  }

  html += "</div>";
  html += "<div id='warn' class='warn'></div>";

  html += "<div class='form-actions'>";
  html += "<button id='submitBtn' type='submit' class='btn btn-green' disabled>Confirmar</button>";
  html += "<a class='btn btn-red' href='" + escapeHTML(return_page) + "' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";

  html += "</form></div>";

  html += R"rawliteral(
<script>
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
    }, 5000);
  } else {
    if(__warnTimer){ clearTimeout(__warnTimer); __warnTimer = null; }
    warn.style.display='none';
    warn.textContent='';
  }
}

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
        isExistingStudent = false;
        updateMateriaRequirement();
      } else if(j.status === 'blocked'){
        setWarn(j.blocked_message || 'Acción no permitida');
        if(submitBtn) submitBtn.disabled = true;
        if(j.restart_after_ms){
          setTimeout(function(){
            try { navigator.sendBeacon('/capture_stop'); } catch(e){}
          }, j.restart_after_ms);
        }
      } else if(j.status === 'found'){
        if(j.uid && (!uidField.value || uidField.value.trim().length === 0)) uidField.value = j.uid;

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

let isExistingStudent = false;

function updateMateriaRequirement() {
  var matField = document.getElementById('materia');
  if (!matField) return;
  if (isExistingStudent) {
    matField.required = true;
    setWarn('Este alumno ya está registrado. Debe asignar una materia.');
  } else {
    matField.required = false;
    var warn = document.getElementById('warn');
    if(warn && warn.textContent === 'Este alumno ya está registrado. Debe asignar una materia.') setWarn('');
  }
}

document.addEventListener('DOMContentLoaded', function(){
  var mat = document.getElementById('materia');
  var prof = document.getElementById('profesor');
  var submitBtn = document.getElementById('submitBtn');

  var nameField = document.getElementById('name');
  var accField = document.getElementById('account');
  if(nameField){
    nameField.addEventListener('input', function(){ nameField.dataset.userEdited = '1'; });
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
    if(!mat.value){
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

    if (isExistingStudent && (!matSel || !matSel.value)) {
      ev.preventDefault();
      setWarn('Este alumno ya está registrado. Debe asignar una materia.');
      return false;
    }

    if(matSel && matSel.value){
      if(profSel && !profSel.value){ ev.preventDefault(); setWarn('Seleccione un profesor para la materia.'); return false; }
    }

    try {
      var nf = document.getElementById('name');
      var af = document.getElementById('account');
      if(nf) delete nf.dataset.userEdited;
      if(af) delete af.dataset.userEdited;
    } catch(e){}
  });

});

window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e){} });
pollUID();
</script>
)rawliteral";

  html += htmlFooter();
  server.send(200, "text/html", html);
}

void capture_individual_poll() {
  if (captureUID.length() == 0) {
    server.send(200, "application/json", "{\"status\":\"waiting\"}");
    return;
  }

  String nameOut    = captureName;
  String accountOut = captureAccount;
  String uidSnapshot = captureUID;

  String storedLine = findAnyUserLineByUID(uidSnapshot);
  if (storedLine.length() > 0) {
    auto parts = parseQuotedCSVLine(storedLine);
    if (parts.size() > 1) nameOut    = parts[1];
    if (parts.size() > 2) accountOut = parts[2];
  } else {
    std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uidSnapshot);
    if (!rows.empty()) {
      if (rows[0].name.length())    nameOut    = rows[0].name;
      if (rows[0].account.length()) accountOut = rows[0].account;
    }
  }

  String target = "";
  if (server.hasArg("target")) {
    target = server.arg("target");
    target.trim();
  }

  if (target == "students" && uidExistsInTeachers(uidSnapshot)) {
    captureUID = "";
    captureDetectedAt = 0;
    String msg = "Esta tarjeta ya está registrada como maestro. No puede registrarse como alumno.";
    String j = "{\"status\":\"blocked\",\"blocked_message\":\"" + jsonEscapeLocal(msg) + "\"";
    j += ",\"restart_after_ms\":5000}";
    server.send(200, "application/json", j);
    return;
  }

  if (target == "teachers" && alumnoExistsByUidDb(uidSnapshot)) {
    captureUID = "";
    captureDetectedAt = 0;
    String msg = "Esta tarjeta pertenece a un alumno. No puede registrarse como maestro.";
    String j = "{\"status\":\"blocked\",\"blocked_message\":\"" + jsonEscapeLocal(msg) + "\"";
    j += ",\"restart_after_ms\":5000}";
    server.send(200, "application/json", j);
    return;
  }

  if (target == "teachers" && uidExistsInTeachers(uidSnapshot)) {
    captureUID = "";
    captureDetectedAt = 0;
    String msg = "Este maestro ya está registrado, no puede capturarse de nuevo.";
    String j = "{\"status\":\"blocked\",\"blocked_message\":\"" + jsonEscapeLocal(msg) + "\"";
    j += ",\"restart_after_ms\":5000}";
    server.send(200, "application/json", j);
    return;
  }

  String j = "{\"status\":\"found\",\"uid\":\"" + jsonEscapeLocal(uidSnapshot) + "\"";
  if (nameOut.length())    j += ",\"name\":\""    + jsonEscapeLocal(nameOut)    + "\"";
  if (accountOut.length()) j += ",\"account\":\"" + jsonEscapeLocal(accountOut) + "\"";

  if (target == "students") {
    bool existing = alumnoExistsByUidDb(uidSnapshot);
    j += ",\"existing\":" + String(existing ? "true" : "false");
  }

  if (accountOut.length()) {
    auto accFound = findByAccountDb(accountOut);
    if (accFound.first.length() && accFound.first != uidSnapshot) {
      String msg = "La cuenta " + accountOut + " ya está asociada a otra tarjeta (UID " + accFound.first + ").";
      j += ",\"blocked\":true";
      j += ",\"blocked_message\":\"" + jsonEscapeLocal(msg) + "\"";
    }
  }

  j += "}";
  server.send(200, "application/json", j);
}

void capture_individual_confirm() {
  if (!server.hasArg("uid") || !server.hasArg("name") ||
      !server.hasArg("account") || !server.hasArg("target")) {
    server.send(400, "text/plain", "Faltan parámetros");
    return;
  }

  String uid     = server.arg("uid");     uid.trim();
  String name    = server.arg("name");    name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.hasArg("materia")  ? server.arg("materia")  : String(); materia.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String(); profesor.trim();
  String target  = server.arg("target");  target.trim();

  if (uid.length() == 0)     { server.send(400, "text/plain", "UID vacío");      return; }
  if (name.length() == 0)    { server.send(400, "text/plain", "Nombre vacío");   return; }
  if (account.length() != 7) { server.send(400, "text/plain", "Cuenta inválida"); return; }
  for (size_t i = 0; i < account.length(); i++)
    if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  if (target == "students" && alumnoExistsByUidDb(uid) && materia.length() == 0) {
    String html = htmlHeader("Error - Materia obligatoria");
    html += "<div class='card'><h3 style='color:#b00020;'>Materia obligatoria para alumno existente</h3>";
    html += "<p class='small'>Este alumno ya está registrado en el sistema. Debe asignar una materia cuando se vuelve a capturar.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/capture_individual?target=students'>Volver a captura</a> <a class='btn btn-green' href='/students_all'>Ver alumnos</a></p>";
    html += "</div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  auto accFound = findByAccountDb(account);
  if (accFound.first.length() && accFound.first != uid) {
    String foundUID    = accFound.first;
    String foundSource = accFound.second;

    String foundName = "";
    std::vector<String> materiasList;
    if (foundSource == "users") {
      std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(foundUID);
      for (auto &r : rows) {
        if (r.name.length()) foundName = r.name;
        if (r.materia.length()) {
          bool exists = false;
          for (auto &m : materiasList) if (m == r.materia) { exists = true; break; }
          if (!exists) materiasList.push_back(r.materia);
        }
      }
    } else {
      // Buscar nombre del profesor en DB
      String payload = getProfesorByUid(foundUID);
      if (payload.length()) {
        DynamicJsonDocument doc(8 * 1024);
        if (!deserializeJson(doc, payload)) {
          JsonVariantConst root = doc.as<JsonVariantConst>();
          auto extractName = [&](JsonObjectConst obj) {
            const char* nameKeys[] = {"nombre", "name", "full_name"};
            String n = jsonGetAnyString(obj, nameKeys, 3);
            if (n.length()) foundName = n;
          };
          if (root.is<JsonObjectConst>()) extractName(root.as<JsonObjectConst>());
          else if (root.is<JsonArrayConst>()) {
            for (JsonVariantConst v : root.as<JsonArrayConst>()) {
              if (v.is<JsonObjectConst>()) { extractName(v.as<JsonObjectConst>()); break; }
            }
          }
        }
      }
    }

    String btnStyle = "padding:8px 12px;border-radius:6px;text-decoration:none;min-width:140px;display:inline-block;text-align:center;";

    String html = htmlHeader("Error - Cuenta duplicada");
    html += "<div class='card'>";
    html += "<h3 style='color:#b00020;'>Cuenta ya registrada</h3>";
    html += "<p class='small'>La cuenta <b>" + escapeHTML(account) + "</b> ya está registrada con UID <b>" + escapeHTML(foundUID) + "</b> en <b>" + escapeHTML(foundSource) + "</b>. No se puede usar la misma cuenta para otro usuario.</p>";
    if (foundName.length()) html += "<p><b>Nombre:</b> " + escapeHTML(foundName) + "</p>";

    if (materiasList.size() > 0) {
      String mats = "";
      for (size_t i = 0; i < materiasList.size(); ++i) { if (i) mats += "; "; mats += materiasList[i]; }
      html += "<p><b>Materias registradas:</b> " + escapeHTML(mats) + "</p>";
    } else {
      html += "<p><i>No tiene materias asociadas.</i></p>";
    }

    html += "<p class='small' style='margin-top:10px;color:#333;'>Si desea editar este usuario pulse el botón Editar usuario. También puede cancelar la captura con el botón rojo.</p>";
    html += "<div style='display:flex;gap:12px;justify-content:center;margin-top:12px;'>";
    html += "<a class='btn btn-green' href='/capture_individual?target=" + escapeHTML(target) + "' style='background:#27ae60;color:#fff;" + btnStyle + "'>Volver a Captura</a>";
    if (foundUID.length()) {
      if (foundSource == "users") html += "<a class='btn btn-yellow' href='/capture_edit?uid=" + escapeHTML(foundUID) + "&return_to=/students_all' style='background:#f1c40f;color:#000;" + btnStyle + "'>Editar usuario</a>";
      else                        html += "<a class='btn btn-yellow' href='/capture_edit?uid=" + escapeHTML(foundUID) + "&return_to=/teachers_all' style='background:#f1c40f;color:#000;" + btnStyle + "'>Editar usuario</a>";
    }
    html += "<a class='btn btn-blue' href='/' style='background:#3498db;color:#fff;" + btnStyle + "'>Inicio</a>";
    html += "<form method='POST' action='/cancel_capture' style='display:inline;margin:0;'><input type='hidden' name='return_to' value='/' /><button type='submit' class='btn btn-red' style='background:#d9534f;color:#fff;" + btnStyle + "border:none;cursor:pointer;'>Cancelar registro</button></form>";
    html += "</div></div>" + htmlFooter();

    server.send(200, "text/html", html);
    return;
  }

  if (target == "teachers") {
    // FIX: reemplazado uidExistsInUsersDb por alumnoExistsByUidDb
    if (alumnoExistsByUidDb(uid)) {
      String foundName = "", foundAccount = "";
      std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
      std::vector<String> materiasList;
      for (auto &r : rows) {
        if (r.name.length())    foundName    = r.name;
        if (r.account.length()) foundAccount = r.account;
        if (r.materia.length()) {
          bool exists = false;
          for (auto &m : materiasList) if (m == r.materia) { exists = true; break; }
          if (!exists) materiasList.push_back(r.materia);
        }
      }

      String html = htmlHeader("No permitido - Tarjeta en uso");
      html += "<div class='card'>";
      html += "<h3 style='color:#b00020;'>Tarjeta ya registrada como alumno</h3>";
      html += "<p class='small'>El UID <b>" + escapeHTML(uid) + "</b> ya está registrado como <b>alumno</b> en el sistema. No puede registrarse como maestro con la misma tarjeta.</p>";
      if (foundName.length())    html += "<p><b>Nombre:</b> "  + escapeHTML(foundName)    + "</p>";
      if (foundAccount.length()) html += "<p><b>Cuenta:</b> "  + escapeHTML(foundAccount) + "</p>";
      if (materiasList.size()) {
        String mats = "";
        for (size_t i = 0; i < materiasList.size(); ++i) { if (i) mats += "; "; mats += materiasList[i]; }
        html += "<p><b>Materias:</b> " + escapeHTML(mats) + "</p>";
      }
      html += "<p class='small' style='margin-top:10px;'>Si desea editar ese registro pulse el botón amarillo, o cancele la captura con el botón rojo.</p>";
      html += "<div style='display:flex;gap:12px;justify-content:center;margin-top:12px;'>";
      html += "<a class='btn btn-green' href='/capture_individual?target=teachers' style='background:#27ae60;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;'>Volver a Captura</a>";
      html += "<a class='btn btn-yellow' href='/capture_edit?uid=" + escapeHTML(uid) + "&return_to=/students_all' style='background:#f1c40f;color:#000;min-width:140px;padding:8px 12px;border-radius:6px;'>Editar usuario</a>";
      html += "<a class='btn btn-blue' href='/' style='background:#3498db;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;'>Inicio</a>";
      html += "<form method='POST' action='/cancel_capture' style='display:inline;margin:0;'><input type='hidden' name='return_to' value='/' /><button type='submit' class='btn btn-red' style='background:#d9534f;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;border:none;cursor:pointer;'>Cancelar registro</button></form>";
      html += "</div></div>" + htmlFooter();
      server.send(200, "text/html", html);
      return;
    }
  } else if (target == "students") {
    if (uidExistsInTeachers(uid)) {
      String foundName = "", foundAccount = "";
      // Buscar datos del profesor en DB
      String payload = getProfesorByUid(uid);
      if (payload.length()) {
        DynamicJsonDocument doc(8 * 1024);
        if (!deserializeJson(doc, payload)) {
          JsonVariantConst root = doc.as<JsonVariantConst>();
          auto extractFields = [&](JsonObjectConst obj) {
            const char* nameKeys[]    = {"nombre", "name", "full_name"};
            const char* accountKeys[] = {"cuenta", "account", "matricula"};
            String n = jsonGetAnyString(obj, nameKeys,    3);
            String a = jsonGetAnyString(obj, accountKeys, 3);
            if (n.length()) foundName    = n;
            if (a.length()) foundAccount = a;
          };
          if (root.is<JsonObjectConst>()) extractFields(root.as<JsonObjectConst>());
          else if (root.is<JsonArrayConst>()) {
            for (JsonVariantConst v : root.as<JsonArrayConst>()) {
              if (v.is<JsonObjectConst>()) { extractFields(v.as<JsonObjectConst>()); break; }
            }
          }
        }
      }

      String html = htmlHeader("No permitido - Tarjeta en uso");
      html += "<div class='card'>";
      html += "<h3 style='color:#b00020;'>Tarjeta ya registrada como maestro</h3>";
      html += "<p class='small'>El UID <b>" + escapeHTML(uid) + "</b> ya está registrado como <b>maestro</b> en el sistema. No puede registrarse como alumno con la misma tarjeta.</p>";
      if (foundName.length())    html += "<p><b>Nombre:</b> " + escapeHTML(foundName)    + "</p>";
      if (foundAccount.length()) html += "<p><b>Cuenta:</b> " + escapeHTML(foundAccount) + "</p>";
      html += "<p class='small' style='margin-top:10px;'>Si desea editar ese registro pulse el botón amarillo, o cancele la captura con el botón rojo.</p>";
      html += "<div style='display:flex;gap:12px;justify-content:center;margin-top:12px;'>";
      html += "<a class='btn btn-green' href='/capture_individual?target=students' style='background:#27ae60;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;'>Volver a Captura</a>";
      html += "<a class='btn btn-yellow' href='/capture_edit?uid=" + escapeHTML(uid) + "&return_to=/teachers_all' style='background:#f1c40f;color:#000;min-width:140px;padding:8px 12px;border-radius:6px;'>Editar maestro</a>";
      html += "<a class='btn btn-blue' href='/' style='background:#3498db;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;'>Inicio</a>";
      html += "<form method='POST' action='/cancel_capture' style='display:inline;margin:0;'><input type='hidden' name='return_to' value='/' /><button type='submit' class='btn btn-red' style='background:#d9534f;color:#fff;min-width:140px;padding:8px 12px;border-radius:6px;border:none;cursor:pointer;'>Cancelar registro</button></form>";
      html += "</div></div>" + htmlFooter();
      server.send(200, "text/html", html);
      return;
    }
  }

  if (target == "students") {
    if (materia.length() > 0) {
      if (!materiaExistsDb(materia)) {
        String html = htmlHeader("Error - Materia");
        html += "<div class='card'><h3>Materia no encontrada</h3><p class='small'>La materia seleccionada no existe. Regístrela primero en Materias.</p>";
        html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Ir a Materias</a> <a class='btn btn-red' href='/capture_individual'>Volver</a></p></div>" + htmlFooter();
        server.send(200, "text/html", html);
        return;
      }

      if (profesor.length() == 0) {
        String inf = inferProfessorForMateria(materia);
        if (inf.length() > 0) profesor = inf;
      }

      auto profs = getProfessorsForMateriaDb(materia);
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

    if (materia.length() > 0 && alumnoExistsByUidMateriaDb(uid, materia)) {
      String studentName = getUserNameForUidMateriaDb(uid, materia);
      String html = htmlHeader("Duplicado - Ya registrado");
      html += "<div class='card'><h3 style='color:red;'>El alumno ya está registrado en esa materia</h3>";
      html += "<p class='small'>El UID <b>" + escapeHTML(uid) + "</b> ya figura registrado en la materia <b>" + escapeHTML(materia) + "</b>.</p>";
      if (studentName.length()) html += "<p><b>Nombre:</b> " + escapeHTML(studentName) + "</p>";
      html += "<p style='margin-top:8px'><a class='btn btn-green' href='/students?materia=" + escapeHTML(materia) + "'>Ver lista de alumnos</a> <a class='btn btn-blue' href='/' style='margin-left:8px;'>Inicio</a> <a class='btn btn-red' href='/capture_individual' style='margin-left:8px;'>Volver a captura</a></p>";
      html += "</div>" + htmlFooter();
      server.send(200, "text/html", html);
      return;
    }

    #ifdef USE_DISPLAY
    showTemporaryRedMessage("Administrador capturando, por favor espere...", 1500UL);
    delay(1600);
    #endif

    String created = nowISO();

    // Guardar en DB (ya no en SPIFFS)
    syncAlumnoToOracle(uid, name, account, materia, created);

    #ifdef USE_DISPLAY
    showAccessGranted(name, (materia.length() ? materia : String("Alumno")), uid);
    #endif

    captureMode = false; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

    String html = htmlHeader("Registrado correctamente");
    html += "<div class='card'><h3>Usuario registrado correctamente.</h3>";
    html += "<p class='small'>Nombre: " + escapeHTML(name) + " — Cuenta: " + escapeHTML(account) + "</p>";
    html += "<p class='small'>Materia: " + (materia.length() ? escapeHTML(materia) : String("-")) + " — Profesor: " + (profesor.length() ? escapeHTML(profesor) : String("-")) + "</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/students_all'>Volver a Alumnos</a> <a class='btn btn-green' href='/capture_individual?target=students'>Capturar otro</a></p>";
    html += "</div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  if (target == "teachers") {
    #ifdef USE_DISPLAY
    showTemporaryRedMessage("Administrador capturando, por favor espere...", 1500UL);
    delay(1600);
    #endif

    String created = nowISO();

    // Guardar en DB (ya no en SPIFFS)
    syncTeacherToOracle(uid, name, account, created);

    #ifdef USE_DISPLAY
    showAccessGranted(name, String("Maestro"), uid);
    #endif

    captureMode = false; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

    String html = htmlHeader("Maestro registrado");
    html += "<div class='card'><h3>Maestro registrado correctamente.</h3>";
    html += "<p class='small'>Nombre: " + escapeHTML(name) + " — Cuenta: " + escapeHTML(account) + "</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/teachers_all'>Volver a Maestros</a> <a class='btn btn-green' href='/capture_individual?target=teachers'>Capturar otro</a></p>";
    html += "</div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  server.send(400, "text/plain", "target inválido");
}

void capture_individual_startPOST() {
  String target = server.hasArg("target") ? server.arg("target") : String("students");
  bool isTeacher = (target == "teachers");

  captureMode = true; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  #ifdef USE_DISPLAY
  showCaptureInProgress(false, String());
  showCaptureMode(isTeacher, false);
  #endif

  String loc = "/capture_individual?target=" + target;
  server.sendHeader("Location", loc);
  server.send(303, "text/plain", "capture started");
}

void capture_individual_stopGET() {
  captureMode = false; captureBatchMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  cancelCaptureAndReturnToNormal();
  server.sendHeader("Location", "/");
  server.send(303, "text/plain", "stopped");
}

void capture_individual_editPage() {
  handleEditGet();
}

void capture_individual_editPost() {
  handleEditPost();
}