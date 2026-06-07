// src/web/edit.cpp
#include <Arduino.h>
#include <ctype.h>
#include <vector>
#include <utility>

#include "globals.h"
#include "web_common.h"
#include "edit.h"
#include "courses.h"    // loadCourses(), writeCourses()
#include "db_sync.h"    // Oracle sync / DB helpers

#include <ArduinoJson.h>

// ---------------- utilidades locales ----------------
static String htmlEscapeLocal(const String &s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

static String sanitizeReturnToLocal(const String &rt) {
  if (rt.length() > 0 && rt[0] == '/') return rt;
  return String("/students_all");
}

static String nowCreatedFallback() {
  return nowISO();
}

static String getJsonString(const JsonObjectConst &obj, const char* const *keys, size_t keyCount) {
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

struct AlumnoDbRow {
  String id;
  String uid;
  String name;
  String account;
  String materia;
  String created;
};

struct ProfesorDbRow {
  String id;
  String uid;
  String name;
  String account;
  String created;
};

static bool parseListAlumnosResponse(const String &body, std::vector<AlumnoDbRow> &rows, const String &uidFilter) {
  rows.clear();
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
    const char* uidKeys[]     = {"rfid_uid", "uid", "alumno_uid", "user_uid"};
    const char* idKeys[]      = {"id", "ID"};
    const char* nameKeys[]    = {"name", "nombre", "full_name"};
    const char* accountKeys[] = {"account", "cuenta", "matricula", "account_number"};
    const char* materiaKeys[] = {"materia", "subject"};
    const char* createdKeys[] = {"created", "created_at", "createdAt", "fecha", "timestamp"};

    AlumnoDbRow row;
    row.id      = getJsonString(obj, idKeys,      2);
    row.uid     = getJsonString(obj, uidKeys,     4);
    row.name    = getJsonString(obj, nameKeys,    3);
    row.account = getJsonString(obj, accountKeys, 4);
    row.materia = getJsonString(obj, materiaKeys, 2);
    row.created = getJsonString(obj, createdKeys, 5);

    if (!row.uid.length()) return;
    if (uidFilter.length() && row.uid != uidFilter) return;
    rows.push_back(row);
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

static bool loadAlumnoRowsByUid(const String &uid, std::vector<AlumnoDbRow> &rows) {
  String body = listAlumnos();
  if (!body.length()) return false;
  return parseListAlumnosResponse(body, rows, uid);
}

static bool loadProfesorByUidDb(const String &uid, ProfesorDbRow &out) {
  out = ProfesorDbRow();

  String body = getProfesorByUid(uid);
  if (!body.length()) return false;

  size_t cap = body.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("WARN: error parseando getProfesorByUid(): ");
    Serial.println(err.c_str());
    return false;
  }

  auto handleObj = [&](JsonObjectConst obj) -> bool {
    const char* idKeys[]      = {"id", "ID"};
    const char* uidKeys[]     = {"rfid_uid", "uid", "profesor_uid", "teacher_uid"};
    const char* nameKeys[]    = {"name", "nombre", "full_name"};
    const char* accountKeys[] = {"account", "cuenta", "matricula", "account_number"};
    const char* createdKeys[] = {"created", "created_at", "createdAt", "fecha", "timestamp"};

    out.id      = getJsonString(obj, idKeys,      2);
    out.uid     = getJsonString(obj, uidKeys,     4);
    out.name    = getJsonString(obj, nameKeys,    3);
    out.account = getJsonString(obj, accountKeys, 4);
    out.created = getJsonString(obj, createdKeys, 5);

    if (!out.uid.length()) out.uid = uid;
    return true;
  };

  if (doc.is<JsonArray>()) {
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
      if (!v.is<JsonObjectConst>()) continue;
      if (handleObj(v.as<JsonObjectConst>())) return true;
    }
    return false;
  }

  if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "profesor", "teacher", "result", "items"};
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArray>()) {
        JsonArrayConst arr = root[key].as<JsonArrayConst>();
        for (JsonVariantConst v : arr) {
          if (!v.is<JsonObjectConst>()) continue;
          if (handleObj(v.as<JsonObjectConst>())) return true;
        }
        return false;
      }
    }
    return handleObj(root);
  }

  return false;
}

static String findUidByAccountInStudentsDb(const String &account) {
  if (!account.length()) return "";
  String body = listAlumnos();
  if (!body.length()) return "";

  size_t cap = body.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  if (deserializeJson(doc, body)) return "";

  auto checkObj = [&](JsonObjectConst obj) -> String {
    const char* uidKeys[]     = {"rfid_uid", "uid", "alumno_uid", "user_uid"};
    const char* accountKeys[] = {"account", "cuenta", "matricula", "account_number"};
    String uid = getJsonString(obj, uidKeys,     4);
    String acc = getJsonString(obj, accountKeys, 4);
    if (uid.length() && acc == account) return uid;
    return "";
  };

  if (doc.is<JsonArray>()) {
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
      if (!v.is<JsonObjectConst>()) continue;
      String uid = checkObj(v.as<JsonObjectConst>());
      if (uid.length()) return uid;
    }
  } else if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "alumnos", "rows", "result", "items"};
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArray>()) {
        JsonArrayConst arr = root[key].as<JsonArrayConst>();
        for (JsonVariantConst v : arr) {
          if (!v.is<JsonObjectConst>()) continue;
          String uid = checkObj(v.as<JsonObjectConst>());
          if (uid.length()) return uid;
        }
        return "";
      }
    }
    return checkObj(root);
  }

  return "";
}

static bool uidExistsInUsers(const String &uid) {
  std::vector<AlumnoDbRow> rows;
  return loadAlumnoRowsByUid(uid, rows) && rows.size() > 0;
}

static bool uidExistsInTeachers(const String &uid) {
  ProfesorDbRow p;
  return loadProfesorByUidDb(uid, p);
}

static std::pair<String,String> findByAccountLocal(const String &account) {
  String uid = findUidByAccountInStudentsDb(account);
  if (uid.length()) return std::make_pair(uid, String("users"));
  return std::make_pair(String(""), String(""));
}

// ---------------- Oracle helpers ----------------

static bool oracleDeleteAlumnoRowsByUid(const String &uid) {
  String body = listAlumnos();
  if (!body.length()) {
    Serial.println("WARN: no se pudo leer /alumnos desde Oracle para limpiar filas previas");
    return false;
  }

  size_t cap = body.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("WARN: error parseando JSON de alumnos Oracle: ");
    Serial.println(err.c_str());
    return false;
  }

  bool ok = true;

  auto handleObj = [&](JsonObjectConst obj) {
    const char* uidKeys[] = {"rfid_uid", "uid", "alumno_uid", "user_uid"};
    const char* idKeys[]  = {"id", "ID"};
    String rowUid = getJsonString(obj, uidKeys, 4);
    String id     = getJsonString(obj, idKeys,  2);

    if (rowUid == uid && id.length()) {
      int idNum = id.toInt();
      if (idNum > 0) {
        if (!deleteAlumnoById(idNum)) {
          Serial.print("WARN: no se pudo borrar alumno Oracle id=");
          Serial.println(idNum);
          ok = false;
        } else {
          Serial.print("DB_SYNC: alumno Oracle eliminado id=");
          Serial.println(idNum);
        }
      }
    }
  };

  if (doc.is<JsonArray>()) {
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
      if (!v.is<JsonObjectConst>()) continue;
      handleObj(v.as<JsonObjectConst>());
    }
  } else if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "alumnos", "rows", "result", "items"};
    bool wrapped = false;
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArray>()) {
        wrapped = true;
        JsonArrayConst arr = root[key].as<JsonArrayConst>();
        for (JsonVariantConst v : arr) {
          if (!v.is<JsonObjectConst>()) continue;
          handleObj(v.as<JsonObjectConst>());
        }
        break;
      }
    }
    if (!wrapped) handleObj(root);
  }

  return ok;
}

static bool oracleSyncUserRows(
    const String &uid,
    const String &name,
    const String &account,
    const std::vector<String> &materias,
    const String &createdAt
) {
  bool ok = true;

  if (!oracleDeleteAlumnoRowsByUid(uid)) {
    Serial.println("WARN: no se pudieron limpiar las filas previas del alumno en Oracle");
    ok = false;
  }

  for (auto &mat : materias) {
    if (mat.length() == 0) continue;
    if (!sendAlumnoRegistro(uid, name, account, mat, createdAt)) {
      Serial.print("WARN: no se pudo sincronizar alumno con Oracle para materia: ");
      Serial.println(mat);
      ok = false;
    } else {
      Serial.print("DB_SYNC: alumno sincronizado correctamente para materia: ");
      Serial.println(mat);
    }
  }

  return ok;
}

static bool syncTeacherToOracle(const String &uid, const String &name, const String &account, const String &createdAt) {
  if (updateProfesorByUid(uid, uid, name, account, createdAt)) {
    Serial.println("DB_SYNC: profesor sincronizado correctamente (UPDATE)");
    return true;
  }

  Serial.println("WARN: updateProfesorByUid falló, intentando crear profesor en Oracle");
  if (sendProfesorRegistro(uid, name, account, createdAt)) {
    Serial.println("DB_SYNC: profesor sincronizado correctamente (CREATE)");
    return true;
  }

  Serial.println("WARN: no se pudo sincronizar profesor con Oracle");
  return false;
}

// Propaga el renombre de un profesor en todos los horarios de la base de datos.
// Reemplaza SPIFFS.open() / writeAllLines() / parseQuotedCSVLine() por
// listHorarios() + updateHorarioById() completamente en memoria.
static void propagateTeacherRenameInSchedules(const String &oldName, const String &newName) {
  if (!oldName.length() || oldName == newName) return;

  String body = listHorarios();
  if (!body.length()) {
    Serial.println("WARN: propagateTeacherRenameInSchedules: listHorarios() vacío");
    return;
  }

  size_t cap = body.length() * 2 + 1024;
  if (cap < 8192) cap = 8192;

  DynamicJsonDocument doc(cap);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("WARN: propagateTeacherRenameInSchedules: error JSON: ");
    Serial.println(err.c_str());
    return;
  }

  // Claves posibles según tu esquema de horarios
  const char* idKeys[]        = {"id", "ID", "horario_id"};
  const char* materiaKeys[]   = {"materia", "subject", "asignatura"};
  const char* profesorKeys[]  = {"profesor", "teacher", "nombre_profesor"};
  const char* diaKeys[]       = {"dia", "day", "dia_semana"};
  const char* inicioKeys[]    = {"hora_inicio", "start", "inicio"};
  const char* finKeys[]       = {"hora_fin", "end", "fin"};
  const char* createdKeys[]   = {"created_at", "created", "createdAt"};

  auto processItem = [&](JsonObjectConst obj) {
    String id       = getJsonString(obj, idKeys,       3);
    String materia  = getJsonString(obj, materiaKeys,  3);
    String profesor = getJsonString(obj, profesorKeys, 3);
    String dia      = getJsonString(obj, diaKeys,      3);
    String inicio   = getJsonString(obj, inicioKeys,   3);
    String fin      = getJsonString(obj, finKeys,      3);
    String created  = getJsonString(obj, createdKeys,  3);

    if (!id.length()) return;

    // Verificar si el profesor de este horario coincide con oldName
    bool needsUpdate = false;

    if (profesor == oldName) {
      needsUpdate = true;
      profesor = newName;
    }

    // El campo materia puede venir como "Materia||Profesor" (clave compuesta)
    int sepIdx = materia.indexOf("||");
    if (sepIdx >= 0) {
      String ownerProf = materia.substring(sepIdx + 2);
      ownerProf.trim();
      if (ownerProf == oldName) {
        String ownerMat = materia.substring(0, sepIdx);
        ownerMat.trim();
        materia = ownerMat + String("||") + newName;
        needsUpdate = true;
      }
    }

    if (!needsUpdate) return;

    int idNum = id.toInt();
    if (idNum <= 0) return;

    if (updateHorarioById(idNum, materia, profesor, dia, inicio, fin, created)) {
      Serial.print("DB_SYNC: horario actualizado con nuevo nombre de profesor, id=");
      Serial.println(idNum);
    } else {
      Serial.print("WARN: no se pudo actualizar horario id=");
      Serial.println(idNum);
    }
  };

  if (doc.is<JsonArray>()) {
    JsonArrayConst arr = doc.as<JsonArrayConst>();
    for (JsonVariantConst v : arr) {
      if (v.is<JsonObjectConst>()) processItem(v.as<JsonObjectConst>());
    }
  } else if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "horarios", "rows", "result", "items"};
    bool wrapped = false;
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArray>()) {
        wrapped = true;
        JsonArrayConst arr = root[key].as<JsonArrayConst>();
        for (JsonVariantConst v : arr) {
          if (v.is<JsonObjectConst>()) processItem(v.as<JsonObjectConst>());
        }
        break;
      }
    }
    if (!wrapped) processItem(root);
  }
}

// ---------------- render / lógica compartida ----------------

static void renderEditPage(const String &uid, const String &return_to_in, const String &origin_path) {
  String return_to = sanitizeReturnToLocal(return_to_in);

  bool found = false;
  String foundName = "", foundAccount = "", foundCreated = "";
  String source = "users";
  std::vector<String> foundMaterias;
  ProfesorDbRow teacherRow;

  std::vector<AlumnoDbRow> alumnoRows;
  if (loadAlumnoRowsByUid(uid, alumnoRows) && alumnoRows.size() > 0) {
    found = true;
    source = "users";
    foundName    = alumnoRows[0].name;
    foundAccount = alumnoRows[0].account;

    for (auto &r : alumnoRows) {
      if (foundCreated.length() == 0 && r.created.length()) foundCreated = r.created;
      if (r.materia.length()) foundMaterias.push_back(r.materia);
    }
    if (!foundCreated.length()) foundCreated = nowCreatedFallback();
  } else if (loadProfesorByUidDb(uid, teacherRow)) {
    found = true;
    source       = "teachers";
    foundName    = teacherRow.name;
    foundAccount = teacherRow.account;
    foundCreated = teacherRow.created.length() ? teacherRow.created : nowCreatedFallback();
  }

  if (!found) { server.send(404, "text/plain", "Usuario no encontrado"); return; }

  auto courses = loadCourses();
  std::vector<String> materias;
  for (auto &c : courses) {
    bool ok = true;
    for (auto &m : materias) if (m == c.materia) { ok = false; break; }
    if (ok) materias.push_back(c.materia);
  }

  std::vector<String> normalMaterias;
  for (auto &m : foundMaterias) {
    if (m.length() == 0) continue;
    bool ok = true;
    for (auto &x : normalMaterias) if (x == m) { ok = false; break; }
    if (ok) normalMaterias.push_back(m);
  }

  String formAction = "/edit_post";
  if (origin_path == "/capture_edit") formAction = "/capture_edit_post";

  String initialWarnJS = "";
  if (server.hasArg("err")) {
    String e = server.arg("err");
    String msg = "";
    if (e == "prof_required")    msg = "Una de las materias seleccionadas no tiene profesor asignado. Por favor seleccione un profesor para cada materia.";
    else if (e == "materia_required") msg = "Debe seleccionar al menos una materia antes de guardar.";
    if (msg.length()) {
      msg.replace("\\", "\\\\");
      msg.replace("\"", "\\\"");
      msg.replace("\n", "\\n");
      initialWarnJS = msg;
    }
  }

  String html = htmlHeader("Editar Usuario");
  html += R"rawliteral(
<style>
.edit-card { max-width:900px; margin:10px auto; padding:16px; }
.form-grid { display:grid; grid-template-columns: 1fr 1fr; gap:12px; align-items:start; }
.form-row{ display:flex; flex-direction:column; }
.form-row.full{ grid-column:1 / -1; }
label.small{ font-size:0.9rem; color:#1f2937; margin-bottom:6px; font-weight:600; }
input[type="text"], input[type="tel"], select, input[readonly] { padding:10px 12px; border-radius:8px; border:1px solid #e6eef6; background:#fff; font-size:0.95rem; }
input[readonly]{ background:#f8fafc; color:#274151; }
.materia-list { margin-top:8px; display:flex; flex-direction:column; gap:8px; }
.materia-row { display:flex; gap:8px; align-items:center; }
.materia-row select { min-width:160px; }
.smallbtn { padding:6px 8px; border-radius:6px; text-decoration:none; cursor:pointer; }
.warn { display:none; border-radius:8px; padding:10px; margin-top:10px; font-weight:600; max-width:100%; box-sizing:border-box; }
.form-actions { display:flex; gap:8px; justify-content:center; margin-top:14px; grid-column:1 / -1; }
.form-actions .btn { padding:8px 10px; font-size:0.92rem; border-radius:6px; min-width:110px; }
@media (max-width:720px) { .form-grid { grid-template-columns:1fr; } .edit-card{ margin:8px; } .materia-row { flex-direction:column; align-items:stretch; } }
</style>
)rawliteral";

  html += "<div class='card edit-card'><h2>Editar Usuario</h2>";
  html += "<form method='POST' action='" + htmlEscapeLocal(formAction) + "' class='form-grid' id='editForm' novalidate>";

  html += "<div class='form-row full'><label class='small'>UID (no editable):</label>";
  html += "<input readonly value='" + htmlEscapeLocal(uid) + "'></div>";

  html += "<div class='form-row'><label class='small'>Nombre:</label>";
  html += "<input id='fld_name' name='name' required value='" + htmlEscapeLocal(foundName) + "'></div>";

  html += "<div class='form-row'><label class='small'>Cuenta (7 dígitos):</label>";
  html += "<input id='fld_account' name='account' required maxlength='7' minlength='7' value='" + htmlEscapeLocal(foundAccount) + "'></div>";

  if (source == "users") {
    html += "<div class='form-row full'><label class='small'>Materias asignadas (puede agregar varias):</label>";
    html += "<div id='materias_container' class='materia-list'></div>";
    html += "<div style='margin-top:8px;display:flex;gap:8px;align-items:center;'><button type='button' id='addMateriaBtn' class='btn btn-blue'>➕ Agregar materia</button><span class='small' style='margin-left:8px;color:#475569;'>Seleccione al menos una materia antes de guardar.</span></div>";
    html += "</div>";

    html += "<input type='hidden' id='mat_count' name='materias_count' value='0'>";
    html += "<script>var __availableMaterias = [";
    for (size_t i = 0; i < materias.size(); ++i) {
      if (i) html += ",";
      html += "\"" + htmlEscapeLocal(materias[i]) + "\"";
    }
    html += "];\n</script>";
  } else {
    html += "<input type='hidden' name='materia' value=''>\n";
    html += "<input type='hidden' name='profesor' value=''>\n";
    html += "<div class='form-row full'><p class='small'>Este usuario es un maestro; las materias se gestionan por separado.</p></div>";
  }

  html += "<div id='warn' class='warn'></div>";

  html += "<input type='hidden' name='orig_uid' value='" + htmlEscapeLocal(uid) + "'>";
  html += "<input type='hidden' name='source' value='" + htmlEscapeLocal(source) + "'>";
  html += "<input type='hidden' name='return_to' value='" + htmlEscapeLocal(return_to) + "'>";

  html += "<div class='form-row full'><label class='small'>Registrado:</label>";
  html += "<div style='padding:8px;background:#f5f7f5;border-radius:6px;'>" + htmlEscapeLocal(foundCreated) + "</div></div>";

  html += "<div class='form-actions'>";
  html += "<button type='submit' id='saveBtn' class='btn btn-green'>Guardar</button>";
  html += "<a class='btn btn-red' href='" + htmlEscapeLocal(return_to) + "'>Cancelar</a>";
  html += "</div>";

  html += "</form></div>" + htmlFooter();

  {
    String jsFlags = "<script>\n";
    jsFlags += "var __isUserEdit = ";
    jsFlags += (source == "users") ? "true" : "false";
    jsFlags += ";\n";
    if (initialWarnJS.length()) {
      jsFlags += "var __initial_warn = \"" + initialWarnJS + "\";\n";
    } else {
      jsFlags += "var __initial_warn = null;\n";
    }
    jsFlags += "</script>\n";
    html += jsFlags;
  }

  html += R"rawliteral(
<script>
function createElem(tag, attrs, text) {
  var e = document.createElement(tag);
  if (attrs) {
    for (var k in attrs) {
      if (k === 'class') e.className = attrs[k];
      else if (k === 'html') e.innerHTML = attrs[k];
      else e.setAttribute(k, attrs[k]);
    }
  }
  if (text) e.textContent = text;
  return e;
}
function fetchProfesores(materia, cb) {
  fetch('/profesores_for?materia=' + encodeURIComponent(materia))
    .then(r => r.json())
    .then(j => { if (cb) cb(j && j.profesores ? j.profesores : []); })
    .catch(e => { if (cb) cb([]); });
}
var materiasContainer=null, matCountInput=null, saveBtn=null, warnBox=null;
function setWarn(msg){
  if(!warnBox) return;
  if(msg && msg.length){
    warnBox.textContent = msg;
    warnBox.style.display = 'block';
    warnBox.style.background = '#fff7ed';
    warnBox.style.border = '1px solid #ffd8a8';
    warnBox.style.color = '#7b2e00';
    warnBox.style.padding = '10px';
    warnBox.style.borderRadius = '6px';
  } else {
    warnBox.style.display = 'none';
    warnBox.textContent = '';
  }
}

function updateSaveButtonState(){
  if (typeof __isUserEdit === 'undefined' || !__isUserEdit) {
    if (saveBtn) saveBtn.disabled = false;
    setWarn('');
    return;
  }

  var ok = true;
  var rows = materiasContainer ? materiasContainer.querySelectorAll('.materia-row') : [];
  var selectedCount = 0;
  var pending = 0;

  for (var i=0;i<rows.length;i++){
    (function(r){
      var matSel = r.querySelector('select[name^="materia_"]');
      var profSel = r.querySelector('select[name^="profesor_"]');
      if (!matSel) return;
      var mv = matSel.value || '';
      if (!mv) return;
      selectedCount++;
      var c = profSel ? profSel.getAttribute('data-prof-count') : null;
      if (c !== null) {
        if (parseInt(c,10) >= 2 && !profSel.value) ok = false;
      } else {
        pending++;
        fetchProfesores(mv, function(list){
          pending--;
          if (list.length >= 2) {
            profSel.setAttribute('data-prof-count', String(list.length));
            if (!profSel.value) ok = false;
          } else if (list.length === 1) {
            profSel.innerHTML = '';
            var o = createElem('option',{ 'value': list[0] }, list[0]);
            profSel.appendChild(o);
            profSel.value = list[0];
            profSel.setAttribute('data-prof-count','1');
          } else {
            profSel.innerHTML = '';
            profSel.appendChild(createElem('option',{ 'value':'' }, '-- Ninguno --'));
            profSel.setAttribute('data-prof-count','0');
            ok = false;
          }
          if (pending === 0) {
            saveBtn.disabled = !(ok && selectedCount > 0);
            if (!saveBtn.disabled) setWarn('');
            else if (selectedCount === 0) setWarn('Debe seleccionar al menos una materia antes de guardar.');
            else setWarn('Complete los profesores requeridos para las materias seleccionadas.');
          }
        });
      }
    })(rows[i]);
  }

  if (pending === 0) {
    saveBtn.disabled = !(ok && selectedCount > 0);
    if (!saveBtn.disabled) setWarn('');
    else if (selectedCount === 0) setWarn('Debe seleccionar al menos una materia antes de guardar.');
    else setWarn('Complete los profesores requeridos para las materias seleccionadas.');
  } else {
    saveBtn.disabled = true;
    if (selectedCount === 0) setWarn('Debe seleccionar al menos una materia antes de guardar.');
  }
}

function addMateriaRow(materia, profesor){
  var idx = parseInt(matCountInput.value||"0",10);
  var row = createElem('div',{ 'class':'materia-row' });
  var matSel = createElem('select',{ 'name':'materia_' + idx });
  matSel.appendChild(createElem('option',{ 'value':''}, '-- Ninguna --'));
  if (typeof __availableMaterias !== 'undefined') {
    for (var i=0;i<__availableMaterias.length;i++){
      var o = createElem('option',{ 'value': __availableMaterias[i] }, __availableMaterias[i]);
      if (materia && materia === __availableMaterias[i]) o.selected = true;
      matSel.appendChild(o);
    }
  }
  var profSel = createElem('select',{ 'name':'profesor_' + idx });
  profSel.appendChild(createElem('option',{ 'value':'' }, '-- Ninguno --'));

  var rm = createElem('button',{ 'type':'button', 'class':'smallbtn btn btn-red' }, 'Eliminar');
  rm.addEventListener('click', function(){
    row.remove();
    var rows = materiasContainer.querySelectorAll('.materia-row');
    for (var r=0;r<rows.length;r++){
      var ms = rows[r].querySelector('select[name^="materia_"]');
      var ps = rows[r].querySelector('select[name^="profesor_"]');
      if (ms) ms.name = 'materia_' + r;
      if (ps) ps.name = 'profesor_' + r;
    }
    matCountInput.value = String(rows.length);
    updateSaveButtonState();
  });

  matSel.addEventListener('change', function(){
    var val = matSel.value || '';
    profSel.innerHTML = '';
    profSel.appendChild(createElem('option',{ 'value':'' }, '-- Ninguno --'));
    profSel.removeAttribute('data-prof-count');
    if (!val) { updateSaveButtonState(); return; }
    fetchProfesores(val, function(list){
      if (!list || list.length === 0) {
        profSel.disabled = true;
        profSel.setAttribute('data-prof-count','0');
      } else if (list.length === 1) {
        profSel.innerHTML = '';
        var o = createElem('option',{ 'value': list[0] }, list[0]);
        profSel.appendChild(o);
        profSel.value = list[0];
        profSel.removeAttribute('disabled');
        profSel.setAttribute('data-prof-count','1');
      } else {
        profSel.innerHTML = '';
        for (var i=0;i<list.length;i++){
          var o = createElem('option',{ 'value': list[i] }, list[i]);
          profSel.appendChild(o);
        }
        profSel.removeAttribute('disabled');
        profSel.setAttribute('data-prof-count', String(list.length));
      }
      if (profesor && profesor.length) {
        for (var i=0;i<profSel.options.length;i++) {
          if (profSel.options[i].value == profesor) { profSel.value = profesor; break; }
        }
      }
      updateSaveButtonState();
    });
  });
  profSel.addEventListener('change', updateSaveButtonState);

  row.appendChild(matSel);
  row.appendChild(profSel);
  row.appendChild(rm);
  if (materiasContainer) materiasContainer.appendChild(row);
  matCountInput.value = String(parseInt(matCountInput.value||"0",10) + 1);

  if (matSel.value) {
    matSel.dispatchEvent(new Event('change'));
  } else updateSaveButtonState();
}

document.addEventListener('DOMContentLoaded', function(){
  materiasContainer = document.getElementById('materias_container');
  matCountInput = document.getElementById('mat_count');
  saveBtn = document.getElementById('saveBtn');
  warnBox = document.getElementById('warn');
  var addBtn = document.getElementById('addMateriaBtn');
  if (addBtn) addBtn.addEventListener('click', function(){ addMateriaRow('', ''); });

  var existing = [
)rawliteral";

  for (size_t i = 0; i < normalMaterias.size(); ++i) {
    String m = normalMaterias[i];
    m.replace("\\", "\\\\");
    m.replace("\"", "\\\"");
    html += "\"" + m + "\"";
    if (i + 1 < normalMaterias.size()) html += ",";
  }

  html += R"rawliteral(
  ];

  if (typeof __isUserEdit !== 'undefined' && __isUserEdit) {
    for (var i=0;i<existing.length;i++){
      addMateriaRow(existing[i], '');
    }
    updateSaveButtonState();
  } else {
    if (saveBtn) saveBtn.disabled = false;
    setTimeout(function(){ setWarn(''); }, 20);
  }

  if (typeof __initial_warn !== 'undefined' && __initial_warn) {
    setTimeout(function(){ setWarn(__initial_warn); }, 50);
  }

  var form = document.getElementById('editForm');
  form.addEventListener('submit', function(ev){
    if (typeof __isUserEdit === 'undefined' || !__isUserEdit) {
      return;
    }
    updateSaveButtonState();
    if (saveBtn.disabled) {
      ev.preventDefault();
      setWarn('No puede guardar: seleccione al menos 1 materia y complete profesores requeridos.');
      if (materiasContainer) materiasContainer.scrollIntoView({behavior:'smooth', block:'center'});
      return false;
    }
  });
});
</script>
)rawliteral";

  server.send(200, "text/html", html);
}

// ---------------- handlers públicos ----------------

// GET /edit
void handleEditGet() {
  if (!server.hasArg("uid")) { server.send(400, "text/plain", "uid required"); return; }
  String uid = server.arg("uid");
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : String();
  renderEditPage(uid, return_to, server.uri());
}

// POST /edit_post (y /capture_edit_post comparte esta lógica)
static void processEditPostAndRedirect(const String &redirect_to) {
  String uid;
  if (server.hasArg("orig_uid")) uid = server.arg("orig_uid");
  else if (server.hasArg("uid")) uid = server.arg("uid");
  uid.trim();

  String name    = server.hasArg("name")    ? server.arg("name")    : String(); name.trim();
  String account = server.hasArg("account") ? server.arg("account") : String(); account.trim();
  String source  = server.hasArg("source")  ? server.arg("source")  : String(); source.trim();
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : String();
  return_to = sanitizeReturnToLocal(return_to);

  if (uid.length() == 0)     { server.send(400, "text/plain", "UID vacío");      return; }
  if (name.length() == 0)    { server.send(400, "text/plain", "Nombre vacío");   return; }
  if (account.length() != 7) { server.send(400, "text/plain", "Cuenta inválida"); return; }
  for (size_t i = 0; i < account.length(); ++i) {
    if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }
  }

  if (!(source == "users" || source == "teachers")) {
    source = uidExistsInTeachers(uid) ? "teachers" : "users";
  }

  String accountOwnerUid = findUidByAccountInStudentsDb(account);
  if (accountOwnerUid.length() && accountOwnerUid != uid) {
    server.send(400, "text/plain", "Cuenta duplicada con otro usuario");
    return;
  }

  // ---- ALUMNOS ----
  if (source == "users") {
    int mcount = 0;
    if (server.hasArg("materias_count")) {
      mcount = server.arg("materias_count").toInt();
      if (mcount < 0)   mcount = 0;
      if (mcount > 200) mcount = 200;
    }

    std::vector<std::pair<String,String>> materias;
    for (int i = 0; i < mcount; ++i) {
      String mk = String("materia_")  + String(i);
      String pk = String("profesor_") + String(i);
      String mval = server.hasArg(mk) ? server.arg(mk) : String(); mval.trim();
      String pval = server.hasArg(pk) ? server.arg(pk) : String(); pval.trim();
      if (mval.length() == 0) continue;
      if (pval.length() == 0) {
        String loc = "/edit?uid=" + uid + "&err=prof_required";
        server.sendHeader("Location", loc);
        server.send(303, "text/plain", "prof required");
        return;
      }
      materias.push_back(std::make_pair(mval, pval));
    }

    if (materias.size() == 0) {
      String loc = "/edit?uid=" + uid + "&err=materia_required";
      server.sendHeader("Location", loc);
      server.send(303, "text/plain", "materia required");
      return;
    }

    std::vector<AlumnoDbRow> existingRows;
    if (!loadAlumnoRowsByUid(uid, existingRows) || existingRows.size() == 0) {
      server.send(404, "text/plain", "Usuario no encontrado");
      return;
    }

    String created = nowCreatedFallback();
    for (auto &r : existingRows) {
      if (r.created.length()) { created = r.created; break; }
    }

    std::vector<String> uniqueSubmitted;
    for (auto &mp : materias) {
      bool exists = false;
      for (auto &m : uniqueSubmitted) {
        if (m == mp.first) { exists = true; break; }
      }
      if (!exists) uniqueSubmitted.push_back(mp.first);
    }

    bool allOk = true;
    std::vector<bool> used(existingRows.size(), false);

    for (auto &mp : materias) {
      const String &mat = mp.first;
      int matchedIndex = -1;
      for (size_t i = 0; i < existingRows.size(); ++i) {
        if (used[i]) continue;
        if (existingRows[i].materia == mat) { matchedIndex = (int)i; break; }
      }

      if (matchedIndex >= 0) {
        used[matchedIndex] = true;
        const AlumnoDbRow &row = existingRows[matchedIndex];
        int idNum = row.id.toInt();
        if (idNum > 0) {
          if (!updateAlumnoById(idNum, uid, name, account, mat, created)) {
            Serial.print("WARN: updateAlumnoById falló para id="); Serial.println(idNum);
            allOk = false;
          } else {
            Serial.print("DB_SYNC: alumno actualizado id="); Serial.println(idNum);
          }
        } else {
          if (!sendAlumnoRegistro(uid, name, account, mat, created)) {
            Serial.print("WARN: sendAlumnoRegistro falló para materia="); Serial.println(mat);
            allOk = false;
          }
        }
      } else {
        if (!sendAlumnoRegistro(uid, name, account, mat, created)) {
          Serial.print("WARN: sendAlumnoRegistro falló para materia="); Serial.println(mat);
          allOk = false;
        } else {
          Serial.print("DB_SYNC: alumno creado para materia="); Serial.println(mat);
        }
      }
    }

    for (size_t i = 0; i < existingRows.size(); ++i) {
      if (used[i]) continue;
      int idNum = existingRows[i].id.toInt();
      if (idNum > 0) {
        if (!deleteAlumnoById(idNum)) {
          Serial.print("WARN: no se pudo borrar alumno id="); Serial.println(idNum);
          allOk = false;
        } else {
          Serial.print("DB_SYNC: alumno eliminado id="); Serial.println(idNum);
        }
      }
    }

    if (!oracleSyncUserRows(uid, name, account, uniqueSubmitted, created)) {
      Serial.println("WARN: sincronización Oracle de alumno no completa");
    } else {
      Serial.println("DB_SYNC: alumno sincronizado correctamente con Oracle");
    }

    server.sendHeader("Location", return_to);
    server.send(303, "text/plain", allOk ? "Updated" : "Updated with warnings");
    return;
  }

  // ---- TEACHERS ----
  if (source == "teachers") {
    ProfesorDbRow teacher;
    if (!loadProfesorByUidDb(uid, teacher)) {
      server.send(404, "text/plain", "Usuario no encontrado");
      return;
    }

    String created = teacher.created.length() ? teacher.created : nowCreatedFallback();
    String oldName = teacher.name;

    bool updated = updateProfesorByUid(uid, uid, name, account, created);
    if (!updated) {
      Serial.println("WARN: updateProfesorByUid falló, intentando crear profesor en Oracle");
      if (!sendProfesorRegistro(uid, name, account, created)) {
        server.send(500, "text/plain", "Error guardando profesor");
        return;
      }
    }

    if (!syncTeacherToOracle(uid, name, account, created)) {
      Serial.println("WARN: sincronización Oracle de profesor no completa");
    }

    // Propagar renombre en horarios de la BD (sin SPIFFS)
    if (oldName.length() && oldName != name) {
      propagateTeacherRenameInSchedules(oldName, name);
    }

    server.sendHeader("Location", return_to);
    server.send(303, "text/plain", "Updated");
    return;
  }

  server.send(400, "text/plain", "source inválido");
}

// POST /edit_post
void handleEditPost() {
  processEditPostAndRedirect("/students_all");
}