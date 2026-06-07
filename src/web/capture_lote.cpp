// src/web/capture_lote.cpp
#include "capture_lote.h"
#include "capture_common.h"
#include "web_common.h"
#include "globals.h"
#include "files_utils.h"
#include "db_sync.h"

#include <ctype.h>
#include <vector>
#include <utility>
#include <ArduinoJson.h>

// Externals (asegúrate están declaradas en globals.h)
extern volatile bool captureMode;
extern volatile bool captureBatchMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

extern volatile bool awaitingSelfRegister;
extern String currentSelfRegUID;
extern unsigned long awaitingSinceMs;
extern std::vector<SelfRegSession> selfRegSessions;
extern String currentSelfRegToken;

// Small helpers
static String htmlEscape(const String &s) {
  String r;
  r.reserve(s.length());
  for (size_t i = 0; i < (size_t)s.length(); ++i) {
    char c = s[i];
    switch (c) {
      case '&': r += "&amp;"; break;
      case '<': r += "&lt;"; break;
      case '>': r += "&gt;"; break;
      case '"': r += "&quot;"; break;
      case '\'': r += "&#39;"; break;
      default: r += c;
    }
  }
  return r;
}

static String jsEscape(const String &s) {
  String r;
  r.reserve(s.length() * 2);
  for (size_t i = 0; i < (size_t)s.length(); ++i) {
    char c = s[i];
    if (c == '\\') { r += "\\\\"; }
    else if (c == '\'') { r += "\\'"; }
    else if (c == '\n') { r += "\\n"; }
    else if (c == '\r') { r += "\\r"; }
    else r += c;
  }
  return r;
}

static String jsonAnyString(const JsonObjectConst &obj, const char* const* keys, size_t keyCount) {
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

// ---------------------------
// DB rows
// ---------------------------
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

struct ProfesorDbRow {
  String id;
  String uid;
  String name;
  String account;
  String created;
};

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
    r.materia = jsonAnyString(obj, materiaKeys, 3);
    r.profesor = jsonAnyString(obj, profesorKeys, 4);
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
    const char* idKeys[] = {"id", "ID"};
    const char* uidKeys[] = {"rfid_uid", "uid", "alumno_uid", "user_uid"};
    const char* nameKeys[] = {"name", "nombre", "full_name"};
    const char* accountKeys[] = {"account", "cuenta", "matricula", "account_number"};
    const char* materiaKeys[] = {"materia", "subject"};
    const char* createdKeys[] = {"created", "created_at", "createdAt", "fecha", "timestamp"};

    AlumnoDbRow r;
    r.id = jsonAnyString(obj, idKeys, 2);
    r.uid = jsonAnyString(obj, uidKeys, 4);
    r.name = jsonAnyString(obj, nameKeys, 3);
    r.account = jsonAnyString(obj, accountKeys, 4);
    r.materia = jsonAnyString(obj, materiaKeys, 2);
    r.created = jsonAnyString(obj, createdKeys, 5);

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
    const char* wrappers[] = {"data", "alumnos", "students", "rows", "result", "items"};
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

static bool parseProfesoresResponse(const String &body, std::vector<ProfesorDbRow> &out) {
  out.clear();
  if (!body.length()) return false;

  size_t cap = body.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("WARN: error parseando listProfesores(): ");
    Serial.println(err.c_str());
    return false;
  }

  auto handleObj = [&](JsonObjectConst obj) {
    const char* idKeys[] = {"id", "ID"};
    const char* uidKeys[] = {"rfid_uid", "uid", "profesor_uid", "teacher_uid"};
    const char* nameKeys[] = {"name", "nombre", "full_name"};
    const char* accountKeys[] = {"account", "cuenta", "matricula", "account_number"};
    const char* createdKeys[] = {"created", "created_at", "createdAt", "fecha", "timestamp"};

    ProfesorDbRow r;
    r.id = jsonAnyString(obj, idKeys, 2);
    r.uid = jsonAnyString(obj, uidKeys, 4);
    r.name = jsonAnyString(obj, nameKeys, 3);
    r.account = jsonAnyString(obj, accountKeys, 4);
    r.created = jsonAnyString(obj, createdKeys, 5);

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
    const char* wrappers[] = {"data", "profesores", "teachers", "rows", "result", "items"};
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
  String body = listAlumnos();
  if (!body.length()) return out;
  parseAlumnosResponse(body, out);
  return out;
}

static std::vector<ProfesorDbRow> loadProfesoresFromDb() {
  std::vector<ProfesorDbRow> out;
  String body = listProfesores();
  if (!body.length()) return out;
  parseProfesoresResponse(body, out);
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

static std::vector<ProfesorDbRow> loadProfesoresByUidDb(const String &uid) {
  std::vector<ProfesorDbRow> all = loadProfesoresFromDb();
  std::vector<ProfesorDbRow> out;
  for (auto &r : all) {
    if (r.uid == uid) out.push_back(r);
  }
  return out;
}

static bool uidExistsInTeachers(const String &uid) {
  if (uid.length() == 0) return false;
  return !loadProfesoresByUidDb(uid).empty();
}

static String teacherNameByUidDb(const String &uid) {
  auto rows = loadProfesoresByUidDb(uid);
  if (!rows.empty()) return rows[0].name;
  return String();
}

static bool studentExistsInMateria(const String &uid, const String &materia) {
  if (uid.length() == 0 || materia.length() == 0) return false;
  std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
  for (auto &r : rows) {
    if (r.materia == materia) return true;
  }
  return false;
}

static bool studentExistsInDb(const String &uid) {
  if (uid.length() == 0) return false;
  return !loadAlumnosByUidDb(uid).empty();
}

static std::pair<String, String> findByAccountDb(const String &account) {
  if (account.length() == 0) return std::make_pair(String(""), String(""));

  std::vector<AlumnoDbRow> alumnos = loadAlumnosFromDb();
  for (auto &r : alumnos) {
    if (r.account == account) return std::make_pair(r.uid, String("users"));
  }

  std::vector<ProfesorDbRow> profes = loadProfesoresFromDb();
  for (auto &r : profes) {
    if (r.account == account) return std::make_pair(r.uid, String("teachers"));
  }

  return std::make_pair(String(""), String(""));
}

static String getUserNameForUidMateria(const String &uid, const String &materia) {
  String name = "";
  if (uid.length() == 0) return name;

  std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
  for (auto &r : rows) {
    if (r.name.length() && name.length() == 0) name = r.name;
    if (materia.length() > 0 && r.materia == materia && r.name.length()) return r.name;
  }
  return name;
}

static String computeScheduleBaseMat() {
  String scheduleOwner = currentScheduledMateria();
  String scheduleBaseMat;
  int idx = scheduleOwner.indexOf("||");
  if (idx < 0) {
    scheduleBaseMat = scheduleOwner;
    scheduleBaseMat.trim();
  } else {
    scheduleBaseMat = scheduleOwner.substring(0, idx);
    scheduleBaseMat.trim();
  }
  return scheduleBaseMat;
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

static String nowStr() {
  return nowISO();
}

static bool registerDeniedNotificationDb(const String &uid, const String &name, const String &account, const String &note) {
  // Usamos la BD de notificaciones en lugar de archivar en SPIFFS
  bool ok = sendNotificacionRegistro(nowISO(), uid, name, account, note);
  if (!ok) {
    Serial.println("WARN: no se pudo registrar la notificación de denegado en Oracle");
  }
  return ok;
}

static bool registerBatchAlumnoDb(const String &uid, const String &name, const String &account, const String &materia, const String &createdAt) {
  bool ok = true;

  if (!sendAlumnoRegistro(uid, name, account, materia, createdAt)) {
    Serial.println("WARN: no se pudo registrar alumno del lote en Oracle");
    ok = false;
  } else {
    Serial.println("DB_SYNC: alumno del lote registrado correctamente");
  }

  if (!sendAsistencia(createdAt, uid, name, account, materia, "entrada")) {
    Serial.println("WARN: no se pudo registrar asistencia del lote en Oracle");
    ok = false;
  } else {
    Serial.println("DB_SYNC: asistencia del lote registrada correctamente");
  }

  return ok;
}

static bool isTeacherUidDb(const String &uid) {
  return uidExistsInTeachers(uid);
}

static void removeTeacherUidsFromQueue(std::vector<String> &uids, std::vector<String> &removedTeachers) {
  removedTeachers.clear();

  for (int i = (int)uids.size() - 1; i >= 0; --i) {
    String uid = uids[i];
    if (!isTeacherUidDb(uid)) continue;

    String teacherName = teacherNameByUidDb(uid);
    String entry = uid;
    if (teacherName.length()) entry += " - " + teacherName;
    removedTeachers.push_back(entry);
    uids.erase(uids.begin() + i);
  }
}

static void noteTeacherBlocked(const String &uid, const String &teacherName, const String &context) {
  String note = "Tarjeta de maestro detectada y omitida en captura por lote";
  if (context.length()) note += " (" + context + ")";
  if (teacherName.length()) note += ": " + teacherName;
  else note += ": " + uid;
  registerDeniedNotificationDb(uid, teacherName, String(), note);
}

// -------------------- Page --------------------
void capture_lote_page() {
  String return_to = "/students";
  if (server.hasArg("return_to")) {
    String rt = server.arg("return_to");
    rt.trim();
    if (rt.length() && rt[0] == '/') return_to = rt;
  }

  clearCaptureQueueFile();

  captureMode = true;
  captureBatchMode = true;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  #ifdef USE_DISPLAY
  showCaptureMode(true, false);
  #endif

  String scheduleBaseMat = computeScheduleBaseMat();

  String coursesOptionsHtml = "";
  if (scheduleBaseMat.length() == 0) {
    auto materias = loadMateriasFromDb();
    for (size_t i = 0; i < materias.size(); ++i) {
      String label = materias[i].materia;
      if (materias[i].profesor.length()) label += " (" + materias[i].profesor + ")";
      coursesOptionsHtml += "<option value='" + htmlEscape(materias[i].materia) + "'>" + htmlEscape(label) + "</option>";
    }
  }

  String html = htmlHeader("Capturar - Batch");
  html += "<div class='card'><h2>Batch capture</h2>";
  html += "<p class='small'>Acerque varias tarjetas; las UIDs quedarán en una cola. Revise los datos a la derecha y luego 'Terminar y Guardar'.</p>";
  html += "<p class='small' style='color:#b00020;'><strong>Nota:</strong> Las tarjetas de maestros serán rechazadas automáticamente.</p>";

  html += "<div style='display:flex;gap:12px;align-items:flex-start;'>";
  html += "<div style='width:300px;display:flex;flex-direction:column;gap:8px;'>";

  html += "<form method='POST' action='/capture_remove_last' style='display:inline;margin-bottom:6px;'><button class='btn btn-yellow' type='submit'>Borrar última</button></form>";

  html += "<form method='POST' action='/cancel_capture' style='display:inline;margin-bottom:6px;' onsubmit='return confirm(\"Cancelar y limpiar cola? Esto borrará los UIDs en la cola.\")'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  html += "<button class='btn btn-red' type='submit'>Cancelar / Limpiar Cola</button></form>";

  html += "<form id='finishForm' method='POST' action='/capture_finish' style='display:inline;margin-top:6px;' onsubmit='return confirm(\"Terminar y guardar las entradas para los UIDs en la cola?\")'>";
  html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
  if (scheduleBaseMat.length() > 0) {
    html += "<input type='hidden' id='batch_materia' name='materia' value='" + htmlEscape(scheduleBaseMat) + "'>";
    html += "<button id='finishBtn' class='btn btn-green' type='submit'>Terminar y Guardar</button>";
  } else {
    html += "<input type='hidden' id='batch_materia' name='materia' value=''>";
    html += "<div style='font-size:12px;color:#555;margin-top:6px;'>Seleccione la materia usando el control de arriba (se aplicará a todas las filas)</div>";
    html += "<button id='finishBtn' class='btn btn-green' type='submit' disabled>Terminar y Guardar</button>";
  }
  html += "</form>";

  html += "</div>";

  html += "<div style='flex:1;min-width:340px;'>";
  if (scheduleBaseMat.length() == 0) {
    html += "<div style='margin-bottom:8px;display:flex;gap:8px;align-items:center;'>";
    html += "<label style='font-weight:600;'>Materia (aplica a todo el lote):</label>";
    html += "<select id='global_materia_select' style='min-width:220px;'><option value=''>-- Seleccionar --</option>";
    html += coursesOptionsHtml;
    html += "</select>";
    html += "</div>";
  } else {
    html += "<div style='margin-bottom:8px;color:#0b1220;font-weight:600;'>Materia en horario: " + htmlEscape(scheduleBaseMat) + "</div>";
  }

  html += "<p>Cola actual: <span id='queue_count'>0</span> UID(s).</p>";
  html += "<div id='banners_container' style='min-height:0;'></div>";
  html += "<div id='queue_list' style='background:#f5f7fb;padding:8px;border-radius:8px;min-height:120px;margin-top:8px;color:#0b1220;'>Cargando...</div>";
  html += "</div>";

  html += "</div></div>" + htmlFooter();

  String scheduleFlag = scheduleBaseMat.length() ? "true" : "false";

  html += R"rawliteral(
<script>
var scheduleHasMateria = )rawliteral";
  html += scheduleFlag;
  html += R"rawliteral(;
var selectedMateria = '';
function setGlobalMateriaValue(val) {
  selectedMateria = val || '';
  var hidden = document.getElementById('batch_materia');
  if (hidden) hidden.value = selectedMateria;
  var finishBtn = document.getElementById('finishBtn');
  if (finishBtn) finishBtn.disabled = (!selectedMateria || selectedMateria.trim() == '');
}

function removeUid(uid) {
  if (!uid) return;
  fetch('/capture_remove_uid', {
    method: 'POST',
    headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    body: 'uid=' + encodeURIComponent(uid)
  }).then(function(r){
    setTimeout(pollQueue, 250);
  }).catch(function(){ setTimeout(pollQueue, 500); });
}

var teacherBannerTimer = null;
function pollQueue() {
  fetch('/capture_batch_poll')
    .then(r=>r.json())
    .then(j=>{
      var cntEl = document.getElementById('queue_count');
      if (cntEl) cntEl.textContent = j.uids ? j.uids.length : 0;

      var bc = document.getElementById('banners_container');
      bc.innerHTML = '';

      if (j.awaiting) {
        var y = document.createElement('div');
        y.style.marginBottom='8px'; y.style.padding='8px'; y.style.borderRadius='6px'; y.style.background='#fff8d6';
        y.style.color='#111'; y.style.fontWeight='700'; y.style.textAlign='center';
        y.innerHTML='Atención: Registrando nuevo usuario (UID: ' + (j.awaiting_uid||'') + '). No pasar tarjeta hasta terminar el registro.';
        bc.appendChild(y);
      }

      if (j.wrong_card) {
        var red = document.createElement('div');
        red.style.marginBottom='8px'; red.style.padding='8px'; red.style.borderRadius='6px'; red.style.background='#ef4444';
        red.style.color='#fff'; red.style.fontWeight='700'; red.style.textAlign='center';
        red.textContent = 'Espere su turno: registro en curso';
        bc.appendChild(red);
      }

      if (j.teacher_blocked) {
        var existing = document.getElementById('teacher_blocked_banner');
        if (!existing) {
          var tb = document.createElement('div');
          tb.id = 'teacher_blocked_banner';
          tb.style.marginBottom='8px'; tb.style.padding='8px'; tb.style.borderRadius='6px'; tb.style.background='#ffeaa7';
          tb.style.color='#111'; tb.style.fontWeight='700'; tb.style.textAlign='center';
          tb.textContent = j.teacher_blocked_message || 'Tarjeta de maestro rechazada';
          bc.appendChild(tb);
        } else {
          bc.appendChild(existing);
        }

        if (teacherBannerTimer) clearTimeout(teacherBannerTimer);
        teacherBannerTimer = setTimeout(function(){
          var el = document.getElementById('teacher_blocked_banner');
          if (el && el.parentNode) el.parentNode.removeChild(el);
          teacherBannerTimer = null;
        }, 5000);
      } else {
        var el = document.getElementById('teacher_blocked_banner');
        if (el && el.parentNode) el.parentNode.removeChild(el);
        if (teacherBannerTimer) { clearTimeout(teacherBannerTimer); teacherBannerTimer = null; }
      }

      var list = document.getElementById('queue_list');
      if (!j.uids || j.uids.length==0) {
        list.innerHTML = 'No hay UIDs capturadas aún.';
      } else {
        var html = '<table style="width:100%;border-collapse:collapse;"><tr><th style="text-align:left;padding:6px">UID</th><th style="text-align:left;padding:6px">Reg</th><th style="text-align:left;padding:6px">Nombre</th><th style="text-align:left;padding:6px">Cuenta</th><th style="text-align:left;padding:6px">Materia</th><th style="text-align:center;padding:6px">Acción</th></tr>';
        for (var i=0;i<j.uids.length;i++){
          var u = j.uids[i];
          var reg = u.registered ? '✅' : '❌';
          var nm = u.name || '';
          var acc = u.account || '';
          var mat = '';
          if (scheduleHasMateria) mat = u.materia || '';
          else mat = (selectedMateria && selectedMateria.length) ? selectedMateria : (u.materia || '');
          html += '<tr><td style="padding:6px;border-top:1px solid #ddd;">' + (u.uid||'') + '</td>';
          html += '<td style="padding:6px;border-top:1px solid #ddd;">' + reg + '</td>';
          html += '<td style="padding:6px;border-top:1px solid #ddd;">' + (nm||'') + '</td>';
          html += '<td style="padding:6px;border-top:1px solid #ddd;">' + (acc||'') + '</td>';
          html += '<td style="padding:6px;border-top:1px solid #ddd;">' + (mat||'') + '</td>';
          html += '<td style="padding:6px;border-top:1px solid #ddd;text-align:center;">';
          html += '<button style="padding:4px 8px;border-radius:4px;border:none;background:#ef4444;color:#fff;cursor:pointer;" onclick="removeUid(\'' + (u.uid||'').replace(/'/g,'\\\'') + '\')">Eliminar</button>';
          html += '</td></tr>';
        }
        html += '</table>';
        list.innerHTML = html;
      }

      if (!scheduleHasMateria) {
        var g = document.getElementById('global_materia_select');
        if (g) g.value = selectedMateria || '';
      }

      var finishBtn = document.getElementById('finishBtn');
      if (finishBtn) {
        var disable = false;
        if (!scheduleHasMateria) {
          if (!selectedMateria || selectedMateria.trim()=='') disable = true;
        }
        finishBtn.disabled = disable;
        if (disable) finishBtn.classList.remove('btn-green');
        else finishBtn.classList.add('btn-green');
      }
    }).catch(e=>{
    });
  setTimeout(pollQueue, 900);
}
document.addEventListener('DOMContentLoaded', function(){
  var g = document.getElementById('global_materia_select');
  if (g) g.addEventListener('change', function(){ setGlobalMateriaValue(this.value); });
  var hidden = document.getElementById('batch_materia');
  if (hidden && hidden.value && hidden.value.trim()!='') {
    selectedMateria = hidden.value;
  }
  pollQueue();
});
</script>
)rawliteral";

  server.send(200, "text/html", html);
}

// Batch poll - BLOQUEO TOTAL INMEDIATO: NO PROCESA NADA DE MAESTROS
void capture_lote_batchPollGET() {
  auto u = readCaptureQueue();
  if (u.size() == 1 && u[0].length() == 0) u.clear();

  String scheduleBaseMat = computeScheduleBaseMat();

  // Filtrar maestros de la cola
  std::vector<String> removedTeachers;
  removeTeacherUidsFromQueue(u, removedTeachers);

  if (!removedTeachers.empty()) {
    writeCaptureQueue(u);
    for (auto &t : removedTeachers) {
      String uid = t;
      String teacherName = "";
      int dash = t.indexOf(" - ");
      if (dash > 0) {
        uid = t.substring(0, dash);
        teacherName = t.substring(dash + 3);
      }
      noteTeacherBlocked(uid, teacherName, "cola");
    }
  }

  bool cardTriggered = false;
  if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
    for (size_t i = 0; i < u.size(); ++i) {
      if (u[i] == currentSelfRegUID) { cardTriggered = true; break; }
    }
  }

  static unsigned long wrongCardStartTime = 0;
  static unsigned long lastShowWrongRedMs = 0;
  static unsigned long teacherBlockedTime = 0;

  bool wrongCard = false;
  bool teacherBlocked = false;
  String teacherBlockedMessage = "";

  if (captureUID.length() > 0) {
    if (isTeacherUidDb(captureUID)) {
      if (teacherBlockedTime == 0 || (millis() - teacherBlockedTime) > 5000UL) {
        teacherBlockedTime = millis();
        String teacherName = teacherNameByUidDb(captureUID);
        noteTeacherBlocked(captureUID, teacherName, "escaneo");
      }

      teacherBlocked = true;
      teacherBlockedMessage = "Esta tarjeta está registrada como maestro. No puede registrarse en captura por lote.";
      goto send_response;
    }
  }

  if (captureUID.length() > 0 && !isTeacherUidDb(captureUID)) {
    if (awaitingSelfRegister && currentSelfRegUID.length() > 0) {
      if (captureUID != currentSelfRegUID) {
        wrongCard = true;
        if (wrongCardStartTime == 0) wrongCardStartTime = millis();
        #ifdef USE_DISPLAY
        if (lastShowWrongRedMs == 0 || (millis() - lastShowWrongRedMs) > 2000) {
          showTemporaryRedMessage("Espere su turno: registro en curso", 2000);
          lastShowWrongRedMs = millis();
        }
        #endif
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      } else {
        if (appendUidToCaptureQueue(captureUID)) {
          captureUID = "";
          captureName = "";
          captureAccount = "";
          captureDetectedAt = 0;
        }
      }
    } else if (!awaitingSelfRegister) {
      if (appendUidToCaptureQueue(captureUID)) {
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      }
    }
  }

  if (wrongCardStartTime > 0 && (millis() - wrongCardStartTime) < 2500) {
    wrongCard = true;
  } else {
    wrongCardStartTime = 0;
  }

  if (teacherBlockedTime > 0 && (millis() - teacherBlockedTime) < 5000UL) {
    teacherBlocked = true;
  } else {
    teacherBlockedTime = 0;
  }

  if (lastShowWrongRedMs != 0 && (millis() - lastShowWrongRedMs) > 2500) {
    lastShowWrongRedMs = 0;
  }

send_response:
  u = readCaptureQueue();

  String j = "{\"uids\":[";
  bool first = true;
  for (size_t i = 0; i < u.size(); ++i) {
    String uid = u[i];

    if (isTeacherUidDb(uid)) {
      noteTeacherBlocked(uid, teacherNameByUidDb(uid), "respuesta");
      continue;
    }

    std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
    bool reg = !rows.empty();

    String name = "";
    String account = "";
    String materia = "";

    if (reg) {
      if (rows[0].name.length()) name = rows[0].name;
      if (rows[0].account.length()) account = rows[0].account;
      if (rows[0].materia.length()) materia = rows[0].materia;
    }

    if (scheduleBaseMat.length() > 0) materia = scheduleBaseMat;
    else materia = String("");

    if (!first) j += ",";
    first = false;
    j += "{\"uid\":\"" + jsEscape(uid) + "\",";
    j += "\"registered\":" + String(reg ? "true" : "false") + ",";
    j += "\"name\":\"" + jsEscape(name) + "\",";
    j += "\"account\":\"" + jsEscape(account) + "\",";
    j += "\"materia\":\"" + jsEscape(materia) + "\"}";
  }
  j += "],";
  j += "\"awaiting\":" + String(awaitingSelfRegister ? "true" : "false") + ",";
  j += "\"awaiting_uid\":\"" + jsEscape(currentSelfRegUID) + "\",";
  j += "\"card_triggered\":" + String(cardTriggered ? "true" : "false") + ",";
  j += "\"wrong_card\":" + String(wrongCard ? "true" : "false") + ",";
  j += "\"teacher_blocked\":" + String(teacherBlocked ? "true" : "false") + ",";
  if (teacherBlocked) {
    j += "\"teacher_blocked_message\":\"" + jsEscape(teacherBlockedMessage) + "\"";
  } else {
    j += "\"teacher_blocked_message\":\"\"";
  }
  j += "}";

  server.send(200, "application/json", j);
}

void capture_lote_pausePOST() {
  if (captureBatchMode && captureMode) {
    captureMode = false;
    #ifdef USE_DISPLAY
    showCaptureMode(true, true);
    #endif
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "paused");
    return;
  }
  if (captureBatchMode && !captureMode) {
    captureMode = true;
    #ifdef USE_DISPLAY
    showCaptureMode(true, false);
    #endif
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "resumed");
    return;
  }
  captureMode = true;
  captureBatchMode = true;
  #ifdef USE_DISPLAY
  showCaptureMode(true, false);
  #endif
  server.sendHeader("Location", "/capture_batch");
  server.send(303, "text/plain", "started");
}

void capture_lote_removeLastPOST() {
  auto q = readCaptureQueue();
  if (q.size() == 0) {
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "queue empty");
    return;
  }
  q.pop_back();
  writeCaptureQueue(q);
  String rt = "/capture_batch";
  if (server.hasArg("return_to")) rt = server.arg("return_to");
  server.sendHeader("Location", rt.length() ? rt : String("/capture_batch"));
  server.send(303, "text/plain", "removed last");
}

void capture_lote_removeUidPOST() {
  if (!server.hasArg("uid")) {
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "missing uid");
    return;
  }
  String uid = server.arg("uid");
  uid.trim();
  if (uid.length() == 0) {
    server.sendHeader("Location", "/capture_batch");
    server.send(303, "text/plain", "empty uid");
    return;
  }
  auto q = readCaptureQueue();
  bool found = false;
  for (int i = 0; i < (int)q.size(); ++i) {
    if (q[i] == uid) {
      q.erase(q.begin() + i);
      found = true;
      break;
    }
  }
  if (found) writeCaptureQueue(q);
  String rt = "/capture_batch";
  if (server.hasArg("return_to")) rt = server.arg("return_to");
  server.sendHeader("Location", rt.length() ? rt : String("/capture_batch"));
  server.send(303, "text/plain", "removed uid");
}

void capture_lote_generateLinksPOST() {
  auto lines = readCaptureQueue();
  if (lines.size() == 0) {
    server.sendHeader("Location", "/capture");
    server.send(303, "text/plain", "queue empty");
    return;
  }

  std::vector<String> teacherList;
  for (int i = (int)lines.size() - 1; i >= 0; --i) {
    String uid = lines[i];
    if (isTeacherUidDb(uid)) {
      String teacherName = teacherNameByUidDb(uid);
      teacherList.push_back(uid + (teacherName.length() ? String(" - ") + teacherName : ""));
      lines.erase(lines.begin() + i);
      noteTeacherBlocked(uid, teacherName, "generar_links");
    }
  }

  if (lines.size() == 0) {
    clearCaptureQueueFile();
    String html = htmlHeader("Error - No se generaron links");
    html += "<div class='card'><h2>No se generaron links</h2>";
    html += "<p class='small'>No se pudieron generar links porque todas las tarjetas en la cola son de maestros (no permitidos en captura por lote).</p>";
    html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture_batch'>Volver a Captura por Lote</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  std::vector<String> urls;
  for (auto &ln : lines) {
    String uid = ln;
    uid.trim();
    if (uid.length() == 0) continue;

    if (isTeacherUidDb(uid)) {
      continue;
    }

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

  if (urls.size() == 0) {
    String html = htmlHeader("Error - No se generaron links");
    html += "<div class='card'><h2>No se generaron links</h2>";
    html += "<p class='small'>No se pudieron generar links. Revise la cola o intente nuevamente.</p>";
    html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture_batch'>Volver a Captura por Lote</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  String html = htmlHeader("Links de Auto-registro");
  html += "<div class='card'><h2>Links generados</h2>";
  html += "<p class='small'>Estos enlaces expirarán en 5 minutos. Entregue el QR o enlace al alumno para que complete su registro.</p>";
  html += "<ul>";
  for (auto &u : urls) html += "<li><a href='" + u + "'>" + u + "</a></li>";
  html += "</ul>";

  if (teacherList.size() > 0) {
    html += "<div style='margin-top:12px;padding:10px;background:#fff3cd;border:1px solid #ffeaa7;border-radius:6px;'>";
    html += "<strong>Se omitieron las siguientes tarjetas (maestros):</strong><ul>";
    for (auto &t : teacherList) html += "<li>" + htmlEscape(t) + "</li>";
    html += "</ul></div>";
  }

  html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/capture_batch'>Volver</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

void capture_lote_finishPOST() {
  auto q = readCaptureQueue();
  if (q.size() == 0) {
    captureMode = false;
    captureBatchMode = false;
    #ifdef USE_DISPLAY
    showCaptureMode(false, false);
    #endif
    String rt = server.hasArg("return_to") ? server.arg("return_to") : String("/capture_batch");
    server.sendHeader("Location", rt);
    server.send(303, "text/plain", "nothing");
    return;
  }

  String chosenMateria;
  if (server.hasArg("materia")) {
    chosenMateria = server.arg("materia");
    chosenMateria.trim();
  }
  if (chosenMateria.length() == 0) {
    chosenMateria = computeScheduleBaseMat();
  }
  if (chosenMateria.length() == 0) {
    String html = htmlHeader("Materia requerida");
    html += "<div class='card'><h3 style='color:red;'>Seleccione una materia antes de terminar el lote.</h3>";
    html += "<p><a class='btn btn-blue' href='/capture_batch'>Volver</a></p></div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  std::vector<String> successList;
  std::vector<String> duplicateList;
  bool anyWarn = false;

  for (auto &uid : q) {
    uid.trim();
    if (uid.length() == 0) continue;

    if (isTeacherUidDb(uid)) {
      noteTeacherBlocked(uid, teacherNameByUidDb(uid), "finish");
      continue;
    }

    if (studentExistsInMateria(uid, chosenMateria)) {
      String studentName = getUserNameForUidMateria(uid, chosenMateria);
      duplicateList.push_back(uid + " - " + (studentName.length() ? studentName : "Sin nombre"));
      continue;
    }

    std::vector<AlumnoDbRow> rows = loadAlumnosByUidDb(uid);
    String name = "";
    String account = "";

    if (!rows.empty()) {
      if (rows[0].name.length()) name = rows[0].name;
      if (rows[0].account.length()) account = rows[0].account;
    }

    String createdAt = nowISO();
    if (!registerBatchAlumnoDb(uid, name, account, chosenMateria, createdAt)) {
      anyWarn = true;
    }

    successList.push_back(uid + " - " + (name.length() ? name : "Sin nombre"));
  }

  clearCaptureQueueFile();
  captureMode = false;
  captureBatchMode = false;
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;
  #ifdef USE_DISPLAY
  showCaptureMode(false, false);
  #endif

  String html = htmlHeader("Informe de Captura por Lote");
  html += "<div class='card' style='max-width:900px;margin:0 auto;'>";
  html += "<h2 style='margin-top:0;'>Informe de Captura por Lote</h2>";
  html += "<p class='small'>Materia: <strong>" + htmlEscape(chosenMateria) + "</strong></p>";
  html += "<div style='display:flex;gap:16px;align-items:center;margin-top:8px;'>";
  html += "<div style='background:#e6ffed;padding:12px;border-radius:8px;flex:1;border:1px solid #c7f0d4;'>";
  html += "<div style='font-size:18px;font-weight:700;'>✅ Registros exitosos</div>";
  html += "<div style='font-size:22px;margin-top:6px;'>" + String(successList.size()) + "</div>";
  html += "</div>";
  html += "<div style='background:#fff7e6;padding:12px;border-radius:8px;flex:1;border:1px solid #ffe6b8;'>";
  html += "<div style='font-size:18px;font-weight:700;'>⚠️ Duplicados</div>";
  html += "<div style='font-size:22px;margin-top:6px;'>" + String(duplicateList.size()) + "</div>";
  html += "</div>";
  html += "</div>";

  if (duplicateList.size() > 0) {
    html += "<div style='margin-top:16px;padding:12px;background:#fff3cd;border-radius:8px;border:1px solid #ffeaa7;'>";
    html += "<h4 style='margin-top:0;color:#856404;'>Alumnos ya registrados en esta materia</h4>";
    html += "<ul style='margin:0 0 0 18px;'>";
    for (auto &dup : duplicateList) {
      html += "<li>" + htmlEscape(dup) + "</li>";
    }
    html += "</ul>";
    html += "<p class='small' style='margin:8px 0 0 0;color:#856404;'>Estos alumnos no fueron registrados nuevamente.</p>";
    html += "</div>";
  }

  if (successList.size() > 0) {
    html += "<div style='margin-top:16px;padding:12px;background:#e6ffed;border-radius:8px;border:1px solid #c7f0d4;'>";
    html += "<h4 style='margin-top:0;color:#0a7020;'>Registros guardados</h4>";
    html += "<ul style='margin:0 0 0 18px;'>";
    int shown = 0;
    for (auto &ok : successList) {
      if (shown++ >= 10) break;
      html += "<li>" + htmlEscape(ok) + "</li>";
    }
    if (successList.size() > 10) html += "<li>... y " + String(successList.size() - 10) + " más</li>";
    html += "</ul>";
    html += "</div>";
  }

  if (anyWarn) {
    html += "<p class='small' style='margin-top:12px;color:#b45309;'>Algunas operaciones no pudieron sincronizarse completamente con la base de datos.</p>";
  }

  String rt = server.hasArg("return_to") ? server.arg("return_to") : String("/students");
  html += "<div style='margin-top:20px;display:flex;gap:10px;'>";
  html += "<a class='btn btn-blue' href='" + htmlEscape(rt) + "'>Volver a Alumnos</a>";
  html += "<a class='btn btn-green' href='/capture_batch'>Nueva Captura por Lote</a>";
  html += "</div>";
  html += "</div>" + htmlFooter();

  server.send(200, "text/html", html);
}

void capture_lote_cancelPOST() {
  String oldWaitingUID = currentSelfRegUID;

  clearCaptureQueueFile();
  captureUID = "";
  captureName = "";
  captureAccount = "";
  captureDetectedAt = 0;

  if (oldWaitingUID.length()) {
    for (int i = (int)selfRegSessions.size() - 1; i >= 0; --i) {
      if (selfRegSessions[i].uid == oldWaitingUID) {
        selfRegSessions.erase(selfRegSessions.begin() + i);
      }
    }
  }
  awaitingSelfRegister = false;
  currentSelfRegUID = "";
  awaitingSinceMs = 0;
  currentSelfRegToken = "";

  captureMode = false;
  captureBatchMode = false;
  #ifdef USE_DISPLAY
  showCaptureMode(false, false);
  #endif

  String rt = "/students";
  if (server.hasArg("return_to")) rt = server.arg("return_to");
  server.sendHeader("Location", rt.length() ? rt : String("/students"));
  server.send(303, "text/plain", "cancelled");
}