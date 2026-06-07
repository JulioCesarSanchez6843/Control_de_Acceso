// src/web/schedules.cpp
#include "schedules.h"
#include "web_common.h"
#include "config.h"
#include "globals.h"
#include "db_sync.h"

#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <cstring>

// ------------------------------------------------------------
// Utilitarios locales
// ------------------------------------------------------------

static const char *COURSE_KEY_SEP = "||";

static String urlEncodeLocal(const String &str) {
  String ret;
  ret.reserve(str.length() * 3);
  for (size_t i = 0; i < (size_t)str.length(); ++i) {
    char c = str[i];
    if ((c >= '0' && c <= '9') ||
        (c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      ret += c;
    } else if (c == ' ') {
      ret += "%20";
    } else {
      char buf[8];
      snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
      ret += buf;
    }
  }
  return ret;
}

static String jsonEscape(const String &s) {
  String o = s;
  o.replace("\\", "\\\\");
  o.replace("\"", "\\\"");
  o.replace("\n", "\\n");
  o.replace("\r", "\\r");
  return o;
}

static String trimCopy(String s) {
  s.trim();
  return s;
}

static String lowerCopy(String s) {
  s.trim();
  s.toLowerCase();
  return s;
}

static String jsonVariantToString(JsonVariantConst v) {
  if (v.isNull()) return "";

  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    return s ? String(s) : String();
  }

  if (v.is<String>()) return v.as<String>();
  if (v.is<bool>()) return v.as<bool>() ? "true" : "false";
  if (v.is<long>()) return String(v.as<long>());
  if (v.is<unsigned long>()) return String(v.as<unsigned long>());
  if (v.is<int>()) return String(v.as<int>());
  if (v.is<float>()) return String(v.as<float>(), 4);
  if (v.is<double>()) return String(v.as<double>(), 4);

  return "";
}

static String jsonGetAny(const JsonObjectConst &o, const char* const keys[], size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const char* k = keys[i];
    if (!k) continue;
    if (o.containsKey(k)) {
      String s = jsonVariantToString(o[k]);
      s.trim();
      if (s.length()) return s;
    }
  }
  return "";
}

template <typename T>
static void visitJsonItems(JsonVariantConst root, T callback) {
  if (root.is<JsonArrayConst>()) {
    JsonArrayConst arr = root.as<JsonArrayConst>();
    for (JsonVariantConst item : arr) callback(item);
    return;
  }

  if (root.is<JsonObjectConst>()) {
    JsonObjectConst o = root.as<JsonObjectConst>();
    const char* keys[] = {
      "data", "items", "rows", "result", "response",
      "materias", "horarios", "profesores", "teachers",
      "subjects", "list", "payload"
    };
    for (const char* key : keys) {
      if (o.containsKey(key) && o[key].is<JsonArrayConst>()) {
        JsonArrayConst arr = o[key].as<JsonArrayConst>();
        for (JsonVariantConst item : arr) callback(item);
        return;
      }
    }
    callback(root);
  }
}

static bool serverSeemsReady() {
  return (WiFi.status() == WL_CONNECTED) && pingServer();
}

static String makeCourseKey(const String &materia, const String &profesor) {
  return materia + String(COURSE_KEY_SEP) + profesor;
}

static bool splitCourseKey(const String &key, String &materiaOut, String &profesorOut) {
  int idx = key.indexOf(String(COURSE_KEY_SEP));
  if (idx < 0) return false;
  materiaOut = key.substring(0, idx);
  profesorOut = key.substring(idx + strlen(COURSE_KEY_SEP));
  materiaOut.trim();
  profesorOut.trim();
  return true;
}

static bool parseHHMMPermissive(const String &t, int &outH, int &outM) {
  String s = t;
  s.trim();
  int colon = s.indexOf(':');
  if (colon < 0) return false;

  String hs = s.substring(0, colon);
  String ms = s.substring(colon + 1);
  hs.trim();
  ms.trim();

  if (hs.length() == 0 || ms.length() == 0) return false;

  int h = hs.toInt();
  int m = ms.toInt();
  if (h < 0 || h > 23) return false;
  if (m < 0 || m > 59) return false;

  outH = h;
  outM = m;
  return true;
}

static String getScheduleDay(const JsonObjectConst &o) {
  const char* dayKeys[] = {"day", "dia", "weekday", "day_name", "nombre_dia", "nombreDia"};
  return jsonGetAny(o, dayKeys, sizeof(dayKeys) / sizeof(dayKeys[0]));
}

static String getScheduleStart(const JsonObjectConst &o) {
  const char* startKeys[] = {"start", "inicio", "hora_inicio", "start_time", "horaInicio"};
  return jsonGetAny(o, startKeys, sizeof(startKeys) / sizeof(startKeys[0]));
}

static String getScheduleEnd(const JsonObjectConst &o) {
  const char* endKeys[] = {"end", "fin", "hora_fin", "end_time", "horaFin"};
  return jsonGetAny(o, endKeys, sizeof(endKeys) / sizeof(endKeys[0]));
}

static String getScheduleMateria(const JsonObjectConst &o) {
  const char* matKeys[] = {"materia", "subject", "asignatura", "nombre_materia", "nombreMateria", "course"};
  String materia = jsonGetAny(o, matKeys, sizeof(matKeys) / sizeof(matKeys[0]));
  materia.trim();

  const char* profKeys[] = {"profesor", "teacher", "docente", "nombre_profesor", "nombreProfesor", "teacher_name"};
  String profesor = jsonGetAny(o, profKeys, sizeof(profKeys) / sizeof(profKeys[0]));
  profesor.trim();

  if (materia.indexOf(COURSE_KEY_SEP) < 0 && profesor.length() > 0) {
    return materia + String(COURSE_KEY_SEP) + profesor;
  }
  return materia;
}

static String getScheduleProfesor(const JsonObjectConst &o) {
  const char* profKeys[] = {"profesor", "teacher", "docente", "nombre_profesor", "nombreProfesor", "teacher_name"};
  return jsonGetAny(o, profKeys, sizeof(profKeys) / sizeof(profKeys[0]));
}

static String getCourseMateria(const JsonObjectConst &o) {
  const char* matKeys[] = {"materia", "subject", "asignatura", "nombre_materia", "nombreMateria", "course"};
  return jsonGetAny(o, matKeys, sizeof(matKeys) / sizeof(matKeys[0]));
}

static String getCourseProfesor(const JsonObjectConst &o) {
  const char* profKeys[] = {"profesor", "teacher", "docente", "nombre_profesor", "nombreProfesor", "teacher_name"};
  return jsonGetAny(o, profKeys, sizeof(profKeys) / sizeof(profKeys[0]));
}

static String getCourseCreatedAt(const JsonObjectConst &o) {
  const char* createdKeys[] = {"created_at", "createdAt", "fecha", "timestamp", "created"};
  return jsonGetAny(o, createdKeys, sizeof(createdKeys) / sizeof(createdKeys[0]));
}

struct CourseRow {
  String materia;
  String profesor;
  String created_at;
};

struct ScheduleRow {
  String materia;   // puede venir como "Materia||Profesor"
  String day;
  String start;
  String end;
  String id;
};

static std::vector<CourseRow> fetchCoursesFromServer() {
  std::vector<CourseRow> out;
  if (!serverSeemsReady()) return out;

  String payload = listMaterias();
  if (payload.length() == 0) return out;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listMaterias JSON invalido: %s\n", de.c_str());
    return out;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    JsonObjectConst o = item.as<JsonObjectConst>();

    CourseRow c;
    c.materia = getCourseMateria(o);
    c.profesor = getCourseProfesor(o);
    c.created_at = getCourseCreatedAt(o);

    c.materia.trim();
    c.profesor.trim();
    c.created_at.trim();

    if (c.materia.length() || c.profesor.length() || c.created_at.length()) {
      out.push_back(c);
    }
  });

  return out;
}

static std::vector<ScheduleRow> fetchSchedulesFromServer() {
  std::vector<ScheduleRow> out;
  if (!serverSeemsReady()) return out;

  String payload = listHorarios();
  if (payload.length() == 0) return out;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listHorarios JSON invalido: %s\n", de.c_str());
    return out;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    JsonObjectConst o = item.as<JsonObjectConst>();

    ScheduleRow s;
    s.materia = getScheduleMateria(o);
    s.day = getScheduleDay(o);
    s.start = getScheduleStart(o);
    s.end = getScheduleEnd(o);

    const char* idKeys[] = {"id", "horario_id", "schedule_id", "slot_id"};
    s.id = jsonGetAny(o, idKeys, sizeof(idKeys) / sizeof(idKeys[0]));

    s.materia.trim();
    s.day.trim();
    s.start.trim();
    s.end.trim();
    s.id.trim();

    if (s.materia.length() || s.day.length() || s.start.length() || s.end.length()) {
      out.push_back(s);
    }
  });

  return out;
}

static std::vector<String> uniqueMateriaNamesFromServer() {
  std::vector<String> out;
  auto courses = fetchCoursesFromServer();
  for (auto &c : courses) {
    String m = trimCopy(c.materia);
    if (!m.length()) continue;
    bool found = false;
    for (auto &x : out) {
      if (x == m) { found = true; break; }
    }
    if (!found) out.push_back(m);
  }
  return out;
}

static std::vector<String> uniqueProfessorsForMateriaFromServer(const String &materia) {
  std::vector<String> out;
  auto courses = fetchCoursesFromServer();
  for (auto &c : courses) {
    if (trimCopy(c.materia) == trimCopy(materia) && c.profesor.length()) {
      bool found = false;
      for (auto &x : out) {
        if (x == c.profesor) { found = true; break; }
      }
      if (!found) out.push_back(c.profesor);
    }
  }
  return out;
}

static bool courseExistsRemote(const String &materia) {
  auto courses = fetchCoursesFromServer();
  for (auto &c : courses) {
    if (trimCopy(c.materia) == trimCopy(materia)) return true;
  }
  return false;
}

static bool coursePairExistsRemote(const String &materia, const String &profesor) {
  auto courses = fetchCoursesFromServer();
  for (auto &c : courses) {
    if (trimCopy(c.materia) == trimCopy(materia) && trimCopy(c.profesor) == trimCopy(profesor)) return true;
  }
  return false;
}

static int countProfessorsForMateriaRemote(const String &materia) {
  return (int)uniqueProfessorsForMateriaFromServer(materia).size();
}

static bool slotOccupiedRemote(const String &day, const String &start, String *ownerOut = nullptr) {
  auto schedules = fetchSchedulesFromServer();
  for (auto &e : schedules) {
    if (trimCopy(e.day) == trimCopy(day) && trimCopy(e.start) == trimCopy(start)) {
      if (ownerOut) *ownerOut = e.materia;
      return true;
    }
  }
  return false;
}

static bool deleteHorarioRemoteById(int id) {
  if (id <= 0) return false;
  return deleteHorarioById(id);
}

static bool deleteScheduleRemoteByFields(const String &materia, const String &day, const String &start, const String &profesor = String()) {
  if (!serverSeemsReady()) return false;

  String payload = listHorarios();
  if (payload.length() == 0) return false;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listHorarios JSON invalido para borrar: %s\n", de.c_str());
    return false;
  }

  bool anyFound = false;
  bool anyDeleted = false;

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    JsonObjectConst o = item.as<JsonObjectConst>();

    String rowMat = getScheduleMateria(o);
    String rowDay = getScheduleDay(o);
    String rowStart = getScheduleStart(o);
    String rowProf = getScheduleProfesor(o);
    const char* rowIdKeys[] = {"id", "horario_id", "schedule_id", "slot_id"};
    String rowId = jsonGetAny(o, rowIdKeys, 4);

    rowMat.trim();
    rowDay.trim();
    rowStart.trim();
    rowProf.trim();
    rowId.trim();

    if (trimCopy(rowDay) != trimCopy(day)) return;
    if (trimCopy(rowStart) != trimCopy(start)) return;

    bool match = false;
    String matBase = rowMat;
    String profBase = rowProf;

    int sep = matBase.indexOf(COURSE_KEY_SEP);
    if (sep >= 0) {
      if (profBase.length() == 0) {
        profBase = matBase.substring(sep + strlen(COURSE_KEY_SEP));
        matBase = matBase.substring(0, sep);
      } else {
        String parsedMat = matBase.substring(0, sep);
        String parsedProf = matBase.substring(sep + strlen(COURSE_KEY_SEP));
        matBase = parsedMat;
        if (profBase.length() == 0) profBase = parsedProf;
      }
      matBase.trim();
      profBase.trim();
    }

    if (trimCopy(materia).length() > 0) {
      if (trimCopy(matBase) != trimCopy(materia) && trimCopy(rowMat) != trimCopy(materia)) return;
    }

    if (profesor.length() > 0) {
      if (trimCopy(profBase) != trimCopy(profesor)) return;
    }

    match = true;
    if (!match) return;

    anyFound = true;

    int id = rowId.toInt();
    if (id > 0) {
      if (deleteHorarioRemoteById(id)) {
        anyDeleted = true;
      } else {
        Serial.printf("WARN: no se pudo borrar horario id=%d\n", id);
      }
    }
  });

  if (!anyFound) {
    Serial.println("INFO: no se encontró horario coincidente para borrar");
  }

  return anyDeleted || anyFound;
}

static bool scheduleOwnerMatchesCourse(const String &scheduleOwner, const String &courseMateria, const String &courseProfesor) {
  String ownerMat = scheduleOwner;
  String ownerProf = "";

  int idx = ownerMat.indexOf(COURSE_KEY_SEP);
  if (idx >= 0) {
    ownerProf = ownerMat.substring(idx + strlen(COURSE_KEY_SEP));
    ownerMat = ownerMat.substring(0, idx);
  }

  ownerMat.trim();
  ownerProf.trim();

  if (courseProfesor.length() > 0) {
    return trimCopy(ownerMat) == trimCopy(courseMateria) && trimCopy(ownerProf) == trimCopy(courseProfesor);
  }

  return trimCopy(ownerMat) == trimCopy(courseMateria);
}

static bool scheduleMatchesMateria(const String &scheduleOwner, const String &materia) {
  String ownerMat = scheduleOwner;
  int idx = ownerMat.indexOf(COURSE_KEY_SEP);
  if (idx >= 0) ownerMat = ownerMat.substring(0, idx);
  ownerMat.trim();
  return trimCopy(ownerMat) == trimCopy(materia);
}

static bool findCourseByMateria(const String &materia, CourseRow &outCourse) {
  auto courses = fetchCoursesFromServer();
  for (auto &c : courses) {
    if (trimCopy(c.materia) == trimCopy(materia)) {
      outCourse = c;
      return true;
    }
  }
  return false;
}

// ------------------------------------------------------------
// Sync helpers hacia Oracle
// ------------------------------------------------------------

static void syncHorarioCreateToOracle(
    const String &materia,
    const String &profesor,
    const String &day,
    const String &start,
    const String &end
) {
  if (!sendHorarioRegistro(materia, profesor, day, start, end, nowISO())) {
    Serial.println("WARN: no se pudo sincronizar horario con Oracle (CREATE)");
  } else {
    Serial.println("DB_SYNC: horario sincronizado correctamente (CREATE)");
  }
}

// ------------------------------------------------------------
// Vistas
// ------------------------------------------------------------

void handleSchedulesGrid() {
  auto schedules = fetchSchedulesFromServer();

  String html = htmlHeader("Horarios - Grilla");
  html += "<div class='card'><h2>Horarios del Laboratorio (LUN - SAB)</h2>";
  html += "<p class='small'>Vista de los horarios registrados. Para editar/agregar/quitar horarios pulsa <b>Editar Horarios</b>.</p>";

  html += "<table><tr><th>Hora</th>";
  for (int d = 0; d < 6; d++) html += "<th>" + String(DAYS[d]) + "</th>";
  html += "</tr>";

  for (int s = 0; s < SLOT_COUNT; s++) {
    int h = SLOT_STARTS[s];
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "%02d:00 - %02d:00", h, h + 2);
    html += "<tr><th>" + String(lbl) + "</th>";

    for (int d = 0; d < 6; d++) {
      String day = DAYS[d];
      String start = String(h) + ":00";

      String ownerMat = "";
      String ownerProf = "";

      for (auto &e : schedules) {
        if (trimCopy(e.day) == trimCopy(day) && trimCopy(e.start) == trimCopy(start)) {
          String owner = e.materia;
          int idx = owner.indexOf(COURSE_KEY_SEP);
          if (idx >= 0) {
            ownerMat = owner.substring(0, idx);
            ownerProf = owner.substring(idx + strlen(COURSE_KEY_SEP));
          } else {
            ownerMat = owner;
            ownerProf = e.end; // no usado, solo evita advertencia si backend no manda profesor
          }
          ownerMat.trim();
          ownerProf.trim();
          break;
        }
      }

      if (ownerMat.length() == 0) {
        html += "<td style='min-width:140px'>-</td>";
      } else {
        html += "<td style='min-width:140px'>";
        html += "<div style='display:flex;flex-direction:column;gap:4px;align-items:flex-start;'>";
        html += "<span style='padding:4px 8px;border-radius:8px;background:#eef7ed;'>" + ownerMat + "</span>";
        if (ownerProf.length()) {
          html += "<span style='padding:3px 7px;border-radius:8px;background:#eef5ff;margin-top:2px;'>" + ownerProf + "</span>";
        }
        html += "</div></td>";
      }
    }

    html += "</tr>";
  }

  html += "</table>";
  html += "<p style='margin-top:12px'>"
          "<a class='btn btn-green' href='/schedules/edit'>✏️ Editar Horarios</a> "
          "<a class='btn btn-blue' href='/'>Inicio</a>"
          "</p></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

void handleSchedulesEditGrid() {
  auto schedules = fetchSchedulesFromServer();
  auto uniqueMat = uniqueMateriaNamesFromServer();

  String html = htmlHeader("Horarios - Editar (Global)");
  html += "<div class='card'><h2>Editar horarios (Global)</h2>";
  html += "<p class='small'>Seleccione una materia registrada para asignar al slot vacío, o elimine materias asignadas. La lista de profesores se carga desde el servidor.</p>";

  html += "<table><tr><th>Hora</th>";
  for (int d = 0; d < 6; d++) html += "<th>" + String(DAYS[d]) + "</th>";
  html += "</tr>";

  for (int s = 0; s < SLOT_COUNT; s++) {
    int h = SLOT_STARTS[s];
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "%02d:00 - %02d:00", h, h + 2);
    html += "<tr><th>" + String(lbl) + "</th>";

    for (int d = 0; d < 6; d++) {
      String day = DAYS[d];
      String start = String(h) + ":00";
      String end = String(h + 2) + ":00";

      ScheduleRow occupiedRow;
      bool occupied = false;
      for (auto &e : schedules) {
        if (trimCopy(e.day) == trimCopy(day) && trimCopy(e.start) == trimCopy(start)) {
          occupied = true;
          occupiedRow = e;
          break;
        }
      }

      html += "<td style='min-width:170px;vertical-align:top;'>";

      if (occupied) {
        String owner = occupiedRow.materia;
        String mat = owner;
        String prof = "";

        int idx = owner.indexOf(COURSE_KEY_SEP);
        if (idx >= 0) {
          mat = owner.substring(0, idx);
          prof = owner.substring(idx + strlen(COURSE_KEY_SEP));
        }
        mat.trim();
        prof.trim();

        html += "<div style='display:flex;flex-direction:column;gap:6px;'>";
        html += "<div><strong>" + mat + "</strong></div>";
        if (prof.length()) {
          html += "<div style='color:#114b8b;'>" + prof + "</div>";
        }
        html += "<div style='margin-top:6px'>";
        html += "<form method='POST' action='/schedules_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>";
        html += "<input type='hidden' name='materia' value='" + mat + "'>";
        if (prof.length()) html += "<input type='hidden' name='profesor' value='" + prof + "'>";
        html += "<input type='hidden' name='day' value='" + day + "'>";
        html += "<input type='hidden' name='start' value='" + start + "'>";
        html += "<input class='btn btn-red' type='submit' value='Eliminar'>";
        html += "</form></div>";
        html += "</div>";
      } else {
        html += "<form method='POST' action='/schedules_add_slot' style='display:flex;flex-direction:column;gap:6px;' onsubmit='return validateSchedForm(this)'>";
        html += "<input type='hidden' name='day' value='" + day + "'>";
        html += "<input type='hidden' name='start' value='" + start + "'>";
        html += "<input type='hidden' name='end' value='" + end + "'>";

        html += "<select name='materia' class='sched_materia_select' onchange='onSchedMateriaChange(this)'>";
        html += "<option value=''>-- Seleccionar materia --</option>";
        for (auto &m : uniqueMat) {
          html += "<option value='" + m + "'>" + m + "</option>";
        }
        html += "</select>";

        html += "<select name='profesor' class='sched_prof_select' disabled><option value=''>-- Profesor --</option></select>";

        html += "<div style='display:flex;justify-content:center;'><input class='btn btn-green sched_add_btn' type='submit' value='Agregar'></div>";
        html += "</form>";
      }

      html += "</td>";
    }

    html += "</tr>";
  }

  html += "</table>";

  html += R"rawliteral(
<script>
function validateSchedForm(form) {
  try {
    var mat = form.querySelector('select[name="materia"]');
    var prof = form.querySelector('select[name="profesor"]');
    if (!mat || !mat.value || mat.value.trim() === '') {
      alert('Seleccione una materia.');
      return false;
    }
    if (prof && !prof.disabled) {
      if (!prof.value || prof.value.trim() === '') {
        alert('Seleccione un profesor para esta materia.');
        return false;
      }
    }
  } catch (e) {}
  return true;
}

function onSchedMateriaChange(selectEl) {
  try {
    var mat = selectEl.value || '';
    var form = selectEl.closest('form');
    if (!form) return;

    var profSel = form.querySelector('.sched_prof_select');
    var addBtn = form.querySelector('.sched_add_btn');

    profSel.innerHTML = '<option value="">-- Profesor --</option>';
    profSel.disabled = true;
    if (addBtn) addBtn.disabled = true;

    if (!mat) return;

    fetch('/profesores_for?materia=' + encodeURIComponent(mat), { method: 'GET' })
      .then(function(resp) {
        return resp.json();
      })
      .then(function(result) {
        var profs = (result && result.profesores) ? result.profesores : [];
        if (!profs || profs.length === 0) {
          profSel.innerHTML = '<option value="">-- No hay profesores --</option>';
          profSel.disabled = true;
          if (addBtn) addBtn.disabled = true;
          return;
        }

        if (profs.length === 1) {
          profSel.innerHTML = '<option value="">-- Profesor --</option>';
          var o = document.createElement('option');
          o.value = profs[0];
          o.textContent = profs[0];
          o.selected = true;
          profSel.appendChild(o);
          profSel.disabled = true;
          if (addBtn) addBtn.disabled = false;
          return;
        }

        profSel.innerHTML = '<option value="">-- Profesor --</option>';
        profs.forEach(function(p) {
          var o = document.createElement('option');
          o.value = p;
          o.textContent = p;
          profSel.appendChild(o);
        });
        profSel.disabled = false;
        if (addBtn) addBtn.disabled = true;
        profSel.onchange = function() {
          if (addBtn) addBtn.disabled = (this.value === '' || this.value.trim() === '');
        };
      })
      .catch(function(err) {
        profSel.innerHTML = '<option value="">-- Error cargando profesores --</option>';
        profSel.disabled = true;
        if (addBtn) addBtn.disabled = true;
      });
  } catch (e) {}
}

document.addEventListener('DOMContentLoaded', function() {
  var forms = document.querySelectorAll('form');
  forms.forEach(function(f) {
    var btn = f.querySelector('.sched_add_btn');
    if (btn) btn.disabled = true;
  });
});
</script>
)rawliteral";

  html += "<p style='margin-top:12px'>"
          "<a class='btn btn-red' href='/schedules'>Ver Horarios (solo lectura)</a> "
          "<a class='btn btn-blue' href='/'>Inicio</a>"
          "</p></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

// ------------------------------------------------------------
// POST /schedules_add_slot
// ------------------------------------------------------------

void handleSchedulesAddSlot() {
  if (!server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end") || !server.hasArg("materia")) {
    server.send(400, "text/plain", "faltan parametros");
    return;
  }

  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();
  String materia = server.arg("materia"); materia.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  profesor.trim();

  if (day.length() == 0 || start.length() == 0 || end.length() == 0 || materia.length() == 0) {
    server.send(400, "text/plain", "datos invalidos");
    return;
  }

  if (!courseExistsRemote(materia)) {
    server.send(400, "text/plain", "Materia no registrada");
    return;
  }

  int profCount = countProfessorsForMateriaRemote(materia);

  if (profCount > 1) {
    if (profesor.length() == 0) {
      server.send(400, "text/plain", "Seleccione un profesor para esta materia (la materia tiene varios profesores).");
      return;
    }
    if (!coursePairExistsRemote(materia, profesor)) {
      server.send(400, "text/plain", "Curso (materia+profesor) no registrado");
      return;
    }
  } else if (profCount == 1) {
    if (profesor.length() == 0) {
      auto ps = uniqueProfessorsForMateriaFromServer(materia);
      if (ps.size() == 1) profesor = ps[0];
    }
  } else {
    server.send(400, "text/plain", "No hay profesores registrados para esta materia; no se puede asignar.");
    return;
  }

  String owner;
  if (slotOccupiedRemote(day, start, &owner)) {
    server.sendHeader("Location", "/schedules?msg=ocupado");
    server.send(303, "text/plain", "Slot ocupado");
    return;
  }

  syncHorarioCreateToOracle(materia, profesor, day, start, end);

  server.sendHeader("Location", "/schedules/edit");
  server.send(303, "text/plain", "Agregado");
}

// ------------------------------------------------------------
// DELETE /schedules_del
// ------------------------------------------------------------

void handleSchedulesDel() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) {
    server.send(400, "text/plain", "faltan parametros");
    return;
  }

  String mat = server.arg("materia"); mat.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  profesor.trim();

  if (!deleteScheduleRemoteByFields(mat, day, start, profesor)) {
    Serial.println("WARN: no se pudo eliminar el horario en servidor");
  }

  server.sendHeader("Location", "/schedules/edit");
  server.send(303, "text/plain", "Borrado");
}

// ------------------------------------------------------------
// GET /schedules_for?materia=...
// ------------------------------------------------------------

void handleSchedulesForMateriaGET() {
  if (!server.hasArg("materia")) {
    server.send(400, "text/plain", "materia required");
    return;
  }

  String materia = server.arg("materia");
  materia.trim();
  if (!courseExistsRemote(materia)) {
    server.send(404, "text/plain", "Materia no encontrada");
    return;
  }

  String requestedProfesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  requestedProfesor.trim();

  auto schedules = fetchSchedulesFromServer();
  auto profs = uniqueProfessorsForMateriaFromServer(materia);

  String title = "Horarios - " + materia;
  String html = htmlHeader(title.c_str());
  html += "<div class='card'><h2>Horarios para: " + materia + "</h2>";
  html += "<p class='small'>Aquí puede agregar o eliminar horarios únicamente por materia.</p>";

  if (profs.size() > 1 && requestedProfesor.length() == 0) {
    html += "<p class='small' style='color:#8a4b00;'>Esta materia tiene varios profesores. Para asignar un horario específico, seleccione uno en el formulario.</p>";
  }

  html += "<div class='filters'>"
          "<input id='sf_day' placeholder='Filtrar día (LUN/MAR/...)'>"
          "<input id='sf_time' placeholder='Filtrar hora (07:00)'>"
          "<button class='search-btn btn btn-blue' onclick='applySchedFilters()'>Buscar</button>"
          "<button class='search-btn btn btn-green' onclick='clearSchedFilters()'>Limpiar</button>"
          "</div>";

  html += "<table id='sched_mat_table'><tr><th>Día</th><th>Inicio</th><th>Fin</th><th>Profesor</th><th>Acción</th></tr>";

  for (auto &s : schedules) {
    String ownerMat = s.materia;
    String ownerProf = "";
    int idx = ownerMat.indexOf(COURSE_KEY_SEP);
    if (idx >= 0) {
      ownerProf = ownerMat.substring(idx + strlen(COURSE_KEY_SEP));
      ownerMat = ownerMat.substring(0, idx);
    }
    ownerMat.trim();
    ownerProf.trim();

    if (ownerMat != materia) continue;

    if (requestedProfesor.length() && ownerProf.length() && ownerProf != requestedProfesor) continue;

    html += "<tr><td>" + s.day + "</td><td>" + s.start + "</td><td>" + s.end + "</td><td>" + (ownerProf.length() ? ownerProf : String("-")) + "</td>";
    html += "<td><form method='POST' action='/schedules_for_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>";
    html += "<input type='hidden' name='materia' value='" + materia + "'>";
    html += "<input type='hidden' name='day' value='" + s.day + "'>";
    html += "<input type='hidden' name='start' value='" + s.start + "'>";
    if (ownerProf.length()) html += "<input type='hidden' name='profesor' value='" + ownerProf + "'>";
    html += "<input class='btn btn-red' type='submit' value='Eliminar'></form></td></tr>";
  }
  html += "</table>";

  html += "<h3>Añadir horario para " + materia + "</h3>";
  html += "<form method='POST' action='/schedules_for_add'>";

  html += "<input type='hidden' name='materia' value='" + materia + "'>";

  html += "Día: <select name='day'>";
  for (int d = 0; d < 6; d++) {
    html += "<option value='" + String(DAYS[d]) + "'>" + String(DAYS[d]) + "</option>";
  }
  html += "</select> ";

  html += "Inicio (HH:MM): <input name='start' placeholder='07:00'> ";
  html += "Fin (HH:MM): <input name='end' placeholder='09:00'> ";

  if (requestedProfesor.length()) {
    html += "<input type='hidden' name='profesor' value='" + requestedProfesor + "'>";
  } else if (profs.size() == 1) {
    html += "<input type='hidden' name='profesor' value='" + profs[0] + "'>";
    html += "<span class='small' style='margin-left:6px;color:#114b8b;'>Profesor asignado: " + profs[0] + "</span>";
  } else if (profs.size() > 1) {
    html += "Profesor: <select name='profesor' required><option value=''>-- Seleccione --</option>";
    for (auto &p : profs) {
      html += "<option value='" + p + "'>" + p + "</option>";
    }
    html += "</select> ";
  }

  html += "<input class='btn btn-green' type='submit' value='Agregar horario'>";
  html += "</form>";

  html += "<script>"
          "function applySchedFilters(){ "
          "const table=document.getElementById('sched_mat_table'); if(!table) return; "
          "const fd=document.getElementById('sf_day').value.trim().toLowerCase(); "
          "const ft=document.getElementById('sf_time').value.trim().toLowerCase(); "
          "for(let r=1;r<table.rows.length;r++){ "
          "const row=table.rows[r]; "
          "const day=row.cells[0].textContent.toLowerCase(); "
          "const start=row.cells[1].textContent.toLowerCase(); "
          "const ok=(day.indexOf(fd)!==-1)&&(start.indexOf(ft)!==-1); "
          "row.style.display = ok ? '' : 'none'; "
          "} "
          "} "
          "function clearSchedFilters(){ document.getElementById('sf_day').value=''; document.getElementById('sf_time').value=''; applySchedFilters(); }"
          "</script>";

  html += "<p style='margin-top:12px'><a class='btn btn-blue' href='/'>Menu</a></p></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

// ------------------------------------------------------------
// POST /schedules_for_add
// ------------------------------------------------------------

void handleSchedulesForMateriaAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end")) {
    server.send(400, "text/plain", "faltan parametros");
    return;
  }

  String materia = server.arg("materia"); materia.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  profesor.trim();

  if (materia.length() == 0 || day.length() == 0 || start.length() == 0 || end.length() == 0) {
    server.send(400, "text/plain", "datos invalidos");
    return;
  }

  if (!courseExistsRemote(materia)) {
    server.send(400, "text/plain", "Materia no registrada");
    return;
  }

  if (profesor.length() == 0) {
    auto profs = uniqueProfessorsForMateriaFromServer(materia);
    if (profs.size() == 1) {
      profesor = profs[0];
    }
  }

  String owner;
  if (slotOccupiedRemote(day, start, &owner)) {
    server.sendHeader("Location", "/schedules?msg=ocupado");
    server.send(303, "text/plain", "Slot ocupado");
    return;
  }

  syncHorarioCreateToOracle(materia, profesor, day, start, end);

  server.sendHeader("Location", "/schedules_for?materia=" + urlEncodeLocal(materia));
  server.send(303, "text/plain", "Agregado");
}

// ------------------------------------------------------------
// POST /schedules_for_del
// ------------------------------------------------------------

void handleSchedulesForMateriaDelPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) {
    server.send(400, "text/plain", "faltan");
    return;
  }

  String mat = server.arg("materia"); mat.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  profesor.trim();

  if (!deleteScheduleRemoteByFields(mat, day, start, profesor)) {
    Serial.println("WARN: no se pudo eliminar el horario en servidor");
  }

  server.sendHeader("Location", "/schedules_for?materia=" + urlEncodeLocal(mat));
  server.send(303, "text/plain", "Borrado");
}
// ------------------------------------------------------------
// Función pública requerida por courses.cpp
// Elimina un slot de horario identificado por (owner/materia, day, start).
// Delega en deleteScheduleRemoteByFields que ya maneja la búsqueda por ID
// en el servidor remoto.
// ------------------------------------------------------------
bool deleteScheduleSlot(const String &courseKey, const String &day, const String &start) {
  // courseKey puede ser "materia||profesor" o solo "materia"
  String materia = courseKey;
  String profesor = "";
  int sep = courseKey.indexOf("||");
  if (sep >= 0) {
    materia  = courseKey.substring(0, sep);
    profesor = courseKey.substring(sep + 2);
    materia.trim();
    profesor.trim();
  }
  return deleteScheduleRemoteByFields(materia, day, start, profesor);
}