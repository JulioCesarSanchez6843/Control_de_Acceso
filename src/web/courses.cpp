// src/web/courses.cpp
#include "courses.h"
#include "web_common.h"
#include "globals.h"
#include "db_sync.h"
#include "schedules.h"  // provee deleteScheduleSlot

#include <WiFi.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <cstring>

static const char *COURSE_KEY_SEP = "||";


// ------------------------------------------------------------
// Helpers locales
// ------------------------------------------------------------

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

static String urlEncode(const String &str) {
  String encoded = "";
  char buf[8];
  for (size_t i = 0; i < (size_t)str.length(); ++i) {
    unsigned char c = (unsigned char)str.charAt(i);
    if ((c >= 'a' && c <= 'z') ||
        (c >= 'A' && c <= 'Z') ||
        (c >= '0' && c <= '9') ||
        c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += (char)c;
    } else {
      snprintf(buf, sizeof(buf), "%%%02X", c);
      encoded += buf;
    }
  }
  return encoded;
}

static String jsonEscape(const String &s) {
  String o = s;
  o.replace("\\", "\\\\");
  o.replace("\"", "\\\"");
  o.replace("\n", "\\n");
  o.replace("\r", "\\r");
  return o;
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
    const char* keys[] = {"data", "items", "rows", "result", "response", "materias", "horarios", "profesores", "teachers", "subjects", "list"};
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

// ------------------------------------------------------------
// Modelos locales
// ------------------------------------------------------------

struct ScheduleRemoteEntry {
  String owner;
  String day;
  String start;
  String end;
};

static String parseTeacherNameFromJsonItem(JsonVariantConst item) {
  if (!item.is<JsonObjectConst>()) return "";
  JsonObjectConst o = item.as<JsonObjectConst>();

  const char* nameKeys[] = {"nombre", "name", "full_name", "nombre_completo", "nombreCompleto", "profesor", "teacher", "docente"};
  String name = jsonGetAny(o, nameKeys, sizeof(nameKeys) / sizeof(nameKeys[0]));
  name.trim();
  return name;
}

static Course courseFromJson(JsonVariantConst item) {
  Course c;

  if (!item.is<JsonObjectConst>()) {
    String raw = jsonVariantToString(item);
    raw.trim();
    c.materia = raw;
    c.profesor = "";
    c.created_at = "";
    return c;
  }

  JsonObjectConst o = item.as<JsonObjectConst>();

  const char* matKeys[] = {"materia", "subject", "asignatura", "nombre_materia", "nombreMateria", "course"};
  const char* profKeys[] = {"profesor", "teacher", "docente", "nombre_profesor", "nombreProfesor", "teacher_name"};
  const char* createdKeys[] = {"created_at", "createdAt", "fecha", "timestamp", "created"};

  c.materia = jsonGetAny(o, matKeys, sizeof(matKeys) / sizeof(matKeys[0]));
  c.profesor = jsonGetAny(o, profKeys, sizeof(profKeys) / sizeof(profKeys[0]));
  c.created_at = jsonGetAny(o, createdKeys, sizeof(createdKeys) / sizeof(createdKeys[0]));

  c.materia.trim();
  c.profesor.trim();
  c.created_at.trim();

  return c;
}

static ScheduleRemoteEntry scheduleFromJson(JsonVariantConst item) {
  ScheduleRemoteEntry e;

  if (!item.is<JsonObjectConst>()) {
    String raw = jsonVariantToString(item);
    raw.trim();
    e.owner = raw;
    e.day = "";
    e.start = "";
    e.end = "";
    return e;
  }

  JsonObjectConst o = item.as<JsonObjectConst>();

  const char* ownerKeys[] = {"materia", "subject", "asignatura", "owner", "course_key", "clave", "owner_key"};
  const char* dayKeys[]   = {"day", "dia", "weekday", "day_name", "nombre_dia", "nombreDia"};
  const char* startKeys[] = {"start", "inicio", "hora_inicio", "start_time", "horaInicio"};
  const char* endKeys[]   = {"end", "fin", "hora_fin", "end_time", "horaFin"};

  e.owner = jsonGetAny(o, ownerKeys, sizeof(ownerKeys) / sizeof(ownerKeys[0]));
  e.day   = jsonGetAny(o, dayKeys, sizeof(dayKeys) / sizeof(dayKeys[0]));
  e.start = jsonGetAny(o, startKeys, sizeof(startKeys) / sizeof(startKeys[0]));
  e.end   = jsonGetAny(o, endKeys, sizeof(endKeys) / sizeof(endKeys[0]));

  e.owner.trim();
  e.day.trim();
  e.start.trim();
  e.end.trim();
  return e;
}

static std::vector<Course> fetchCoursesFromServer() {
  std::vector<Course> res;
  if (!serverSeemsReady()) return res;

  String payload = listMaterias();
  if (!payload.length()) return res;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listMaterias JSON invalido: %s\n", de.c_str());
    return res;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    Course c = courseFromJson(item);
    if (c.materia.length() || c.profesor.length() || c.created_at.length()) {
      res.push_back(c);
    }
  });

  return res;
}

static std::vector<ScheduleRemoteEntry> fetchSchedulesFromServer() {
  std::vector<ScheduleRemoteEntry> res;
  if (!serverSeemsReady()) return res;

  String payload = listHorarios();
  if (!payload.length()) return res;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listHorarios JSON invalido: %s\n", de.c_str());
    return res;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    ScheduleRemoteEntry e = scheduleFromJson(item);
    if (e.owner.length() || e.day.length() || e.start.length() || e.end.length()) {
      res.push_back(e);
    }
  });

  return res;
}

static std::vector<String> fetchTeacherNamesFromServer() {
  std::vector<String> out;
  if (!serverSeemsReady()) return out;

  String payload = listProfesores();
  if (!payload.length()) return out;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listProfesores JSON invalido: %s\n", de.c_str());
    return out;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    String name = parseTeacherNameFromJsonItem(item);
    if (!name.length()) return;
    bool found = false;
    for (auto &x : out) {
      if (x == name) { found = true; break; }
    }
    if (!found) out.push_back(name);
  });

  return out;
}

static std::vector<String> getProfessorsForMateriaFromServer(const String &materia) {
  std::vector<String> out;
  if (!serverSeemsReady()) return out;

  auto courses = fetchCoursesFromServer();
  for (auto &c : courses) {
    if (c.materia == materia) {
      bool found = false;
      for (auto &p : out) {
        if (p == c.profesor) { found = true; break; }
      }
      if (!found && c.profesor.length()) out.push_back(c.profesor);
    }
  }
  return out;
}

static int countCoursesWithName(const std::vector<Course> &courses, const String &materia) {
  int cnt = 0;
  for (auto &c : courses) {
    if (c.materia == materia) cnt++;
  }
  return cnt;
}

static bool coursePairExists(const std::vector<Course> &courses, const String &materia, const String &profesor) {
  for (auto &c : courses) {
    if (c.materia == materia && c.profesor == profesor) return true;
  }
  return false;
}

static int findCourseIndex(const std::vector<Course> &courses, const String &materia, const String &profesor) {
  for (int i = 0; i < (int)courses.size(); ++i) {
    if (courses[i].materia == materia && courses[i].profesor == profesor) return i;
  }
  return -1;
}

static bool courseExistsRemote(const String &materia) {
  auto courses = fetchCoursesFromServer();
  for (auto &c : courses) {
    if (c.materia == materia) return true;
  }
  return false;
}

static bool slotOccupiedRemote(const String &day, const String &start, String *ownerOut = nullptr) {
  auto schedules = fetchSchedulesFromServer();
  for (auto &s : schedules) {
    if (s.day == day && s.start == start) {
      if (ownerOut) *ownerOut = s.owner;
      return true;
    }
  }
  return false;
}

static bool scheduleBelongsToCourse(const String &owner, const String &mat, const String &prof) {
  if (owner == makeCourseKey(mat, prof)) return true;

  String ownerMat, ownerProf;
  if (splitCourseKey(owner, ownerMat, ownerProf)) {
    return ownerMat == mat && ownerProf == prof;
  }

  return owner == mat;
}

static void migrateSchedulesForCourseRename(const String &oldMat, const String &oldProf,
                                           const String &newMat, const String &newProf,
                                           bool oldNameWasUnique) {
  String oldKey = makeCourseKey(oldMat, oldProf);
  String newKey = makeCourseKey(newMat, newProf);

  auto schedules = fetchSchedulesFromServer();
  for (auto &s : schedules) {
    bool shouldMove = false;

    if (s.owner == oldKey) {
      shouldMove = true;
    } else if (oldNameWasUnique && s.owner == oldMat) {
      shouldMove = true;
    }

    if (!shouldMove) continue;

    deleteScheduleSlot(s.owner, s.day, s.start);
    addScheduleSlot(newKey, s.day, s.start, s.end);
  }
}

static void removeSchedulesForDeletedCourse(const String &mat, const String &prof, bool oldNameWasUnique) {
  String key = makeCourseKey(mat, prof);

  auto schedules = fetchSchedulesFromServer();
  for (auto &s : schedules) {
    bool shouldDelete = false;

    if (s.owner == key) {
      shouldDelete = true;
    } else if (oldNameWasUnique && s.owner == mat) {
      shouldDelete = true;
    }

    if (shouldDelete) {
      deleteScheduleSlot(s.owner, s.day, s.start);
    }
  }
}

static void migrateStudentsForCourseRename(const String &oldMat, const String &newMat, bool oldNameWasUnique) {
  if (!oldNameWasUnique || oldMat == newMat) return;

  auto students = listAlumnos();
  if (!students.length()) return;

  size_t cap = students.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  DeserializationError de = deserializeJson(doc, students);
  if (de) {
    Serial.printf("WARN: listAlumnos JSON invalido: %s\n", de.c_str());
    return;
  }

  auto updateRow = [&](JsonObjectConst obj) {
    const char* idKeys[] = {"id", "ID"};
    const char* uidKeys[] = {"rfid_uid", "uid", "alumno_uid", "user_uid"};
    const char* nameKeys[] = {"name", "nombre", "full_name"};
    const char* accountKeys[] = {"account", "cuenta", "matricula", "account_number"};
    const char* materiaKeys[] = {"materia", "subject"};
    const char* createdKeys[] = {"created", "created_at", "createdAt", "fecha", "timestamp"};

    String id = jsonGetAny(obj, idKeys, 2);
    String uid = jsonGetAny(obj, uidKeys, 4);
    String name = jsonGetAny(obj, nameKeys, 3);
    String account = jsonGetAny(obj, accountKeys, 4);
    String materia = jsonGetAny(obj, materiaKeys, 2);
    String created = jsonGetAny(obj, createdKeys, 5);

    if (materia != oldMat) return;

    int idNum = id.toInt();
    if (idNum > 0) {
      updateAlumnoById(idNum, uid, name, account, newMat, created);
    }
  };

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    updateRow(item.as<JsonObjectConst>());
  });
}

static void deleteStudentsForDeletedCourse(const String &mat) {
  auto students = listAlumnos();
  if (!students.length()) return;

  size_t cap = students.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  DeserializationError de = deserializeJson(doc, students);
  if (de) {
    Serial.printf("WARN: listAlumnos JSON invalido: %s\n", de.c_str());
    return;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    JsonObjectConst obj = item.as<JsonObjectConst>();

    const char* idKeys[] = {"id", "ID"};
    const char* materiaKeys[] = {"materia", "subject"};

    String id = jsonGetAny(obj, idKeys, 2);
    String materia = jsonGetAny(obj, materiaKeys, 2);

    if (materia != mat) return;

    int idNum = id.toInt();
    if (idNum > 0) {
      deleteAlumnoById(idNum);
    }
  });
}

static bool deleteCourseRelatedSchedules(const String &mat, const String &prof, bool oldNameWasUnique) {
  removeSchedulesForDeletedCourse(mat, prof, oldNameWasUnique);
  return true;
}

static bool registerCourseInDb(const String &materia, const String &profesor, const String &createdAt) {
  if (!sendMateriaRegistro(materia, profesor, createdAt)) {
    Serial.println("WARN: no se pudo registrar la materia en la BD");
    return false;
  }
  Serial.println("DB_SYNC: materia registrada correctamente");
  return true;
}

static bool updateCourseInDb(const String &oldMat, const String &newMat, const String &newProf, const String &createdAt) {
  if (!updateMateriaByName(oldMat, newMat, newProf, createdAt)) {
    Serial.println("WARN: no se pudo actualizar la materia en la BD");
    return false;
  }
  Serial.println("DB_SYNC: materia actualizada correctamente");
  return true;
}

static bool deleteCourseInDb(const String &mat, bool cascade) {
  if (!deleteMateriaByName(mat, cascade)) {
    Serial.println("WARN: no se pudo eliminar la materia en la BD");
    return false;
  }
  Serial.println("DB_SYNC: materia eliminada correctamente");
  return true;
}

// Export visible desde otros .cpp
std::vector<String> getProfessorsForMateria(const String &materia) {
  return getProfessorsForMateriaFromServer(materia);
}

std::vector<String> loadRegisteredTeachersNames() {
  return fetchTeacherNamesFromServer();
}

// ------------------------------------------------------------
// Implementaciones públicas usadas por otros módulos
// ------------------------------------------------------------

std::vector<Course> loadCourses() {
  return fetchCoursesFromServer();
}

bool courseExists(const String &materia) {
  return courseExistsRemote(materia);
}

void addCourse(const String &materia, const String &prof) {
  String mat = materia; mat.trim();
  String pr = prof; pr.trim();
  if (!mat.length() || !pr.length()) return;
  String createdAt = nowISO();
  registerCourseInDb(mat, pr, createdAt);
}

void writeCourses(const std::vector<Course> &list) {
  std::vector<Course> current = fetchCoursesFromServer();

  // 1) Crear o actualizar lo necesario
  for (const auto &desired : list) {
    if (!desired.materia.length() || !desired.profesor.length()) continue;

    int idx = findCourseIndex(current, desired.materia, desired.profesor);
    if (idx >= 0) {
      // Ya existe exacto; nada que hacer.
      continue;
    }

    // Intentar encontrar una materia con el mismo nombre para renombrar/ajustar
    int sameNameIdx = -1;
    for (int i = 0; i < (int)current.size(); ++i) {
      if (current[i].materia == desired.materia) {
        sameNameIdx = i;
        break;
      }
    }

    if (sameNameIdx >= 0) {
      Course old = current[sameNameIdx];
      updateCourseInDb(old.materia, desired.materia, desired.profesor, old.created_at);
      current[sameNameIdx] = desired;
      continue;
    }

    registerCourseInDb(desired.materia, desired.profesor, desired.created_at.length() ? desired.created_at : nowISO());
    current.push_back(desired);
  }

  // 2) Eliminar lo que ya no existe en el nuevo listado
  for (const auto &cur : current) {
    bool stillWanted = false;
    for (const auto &desired : list) {
      if (desired.materia == cur.materia && desired.profesor == cur.profesor) {
        stillWanted = true;
        break;
      }
    }

    if (stillWanted) continue;

    // Si ya no está en la lista final, intentar borrarlo.
    // Se usa cascade=true porque en esta capa la BD debe resolver la limpieza de relaciones.
    deleteCourseInDb(cur.materia, true);
  }
}

// ------------------------------------------------------------
// Handlers: materias
// ------------------------------------------------------------

void handleMaterias() {
  String html = htmlHeader("Materias");
  html += "<div class='card'><h2>Materias disponibles</h2>";
  auto courses = loadCourses();
  html += "<p class='small'>Pulse 'Agregar nueva materia' para registrar una materia. Desde aquí puede administrar estudiantes o ver el historial por días.</p>";

  html += "<div class='filters'><input id='f_mat' placeholder='Filtrar por materia'><input id='f_prof' placeholder='Filtrar por profesor'><button class='search-btn btn btn-blue' onclick='applyMateriaFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearMateriaFilters()'>Limpiar</button></div>";

  if (courses.size() == 0) {
    html += "<p>No hay materias registradas.</p>";
  } else {
    auto schedules = fetchSchedulesFromServer();
    html += "<table id='materias_table'><tr><th>Materia</th><th>Profesor</th><th>Creado</th><th>Horarios</th><th>Acción</th></tr>";
    for (auto &c : courses) {
      String schedStr = "";
      for (auto &s : schedules) {
        if (scheduleBelongsToCourse(s.owner, c.materia, c.profesor)) {
          if (schedStr.length()) schedStr += "; ";
          schedStr += s.day + " " + s.start + "-" + s.end;
        }
      }
      if (schedStr.length() == 0) schedStr = "-";

      html += "<tr><td>" + c.materia + "</td><td>" + c.profesor + "</td><td>" + c.created_at + "</td><td>" + schedStr + "</td>";

      html += "<td>";
      html += "<a class='btn btn-green' href='/materias/edit?materia=" + urlEncode(c.materia) + "&profesor=" + urlEncode(c.profesor) + "'>Editar</a> ";
      html += "<a class='btn' href='/materias_new_schedule?materia=" + urlEncode(c.materia) + "&profesor=" + urlEncode(c.profesor) + "' style='background:#5dade2;color:#fff;padding:6px 10px;border-radius:6px;text-decoration:none;margin-left:6px;'>Horarios</a> ";
      html += "<a class='btn btn-purple' href='/students?materia=" + urlEncode(c.materia) + "&profesor=" + urlEncode(c.profesor) + "&return_to=/materias&hide_capture=1' style='background:#6dd3d0;color:#000;padding:6px 10px;border-radius:6px;text-decoration:none;margin-left:6px;'>Administrar Estudiantes</a> ";
      html += "<a class='btn btn-orange' href='/materia_history?materia=" + urlEncode(c.materia) + "&profesor=" + urlEncode(c.profesor) + "' style='margin-left:6px;'>Historial</a> ";
      html += "<form method='POST' action='/materias_delete' style='display:inline' onsubmit='return confirm(\"Eliminar materia y sus horarios/usuarios? Esta acción es irreversible.\");'>"
              "<input type='hidden' name='materia' value='" + c.materia + "'>"
              "<input type='hidden' name='profesor' value='" + c.profesor + "'>"
              "<input class='btn btn-red' type='submit' value='Eliminar'></form>";
      html += "</td></tr>";
    }
    html += "</table>";

    html += "<script>"
            "function applyMateriaFilters(){"
            "const table=document.getElementById('materias_table');"
            "if(!table) return;"
            "const fmat=document.getElementById('f_mat').value.trim().toLowerCase();"
            "const fprof=document.getElementById('f_prof').value.trim().toLowerCase();"
            "for(let r=1;r<table.rows.length;r++){"
            "const row=table.rows[r];"
            "if(row.cells.length<2) continue;"
            "const mat=row.cells[0].textContent.toLowerCase();"
            "const prof=row.cells[1].textContent.toLowerCase();"
            "const ok=(mat.indexOf(fmat)!==-1)&&(prof.indexOf(fprof)!==-1);"
            "row.style.display=ok?'':'none';"
            "}"
            "}"
            "function clearMateriaFilters(){document.getElementById('f_mat').value='';document.getElementById('f_prof').value='';applyMateriaFilters();}"
            "</script>";
  }

  html += "<p style='margin-top:8px'><a class='btn btn-green' href='/materias/new'>Agregar nueva materia</a> <a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleMateriasNew() {
  auto teachers = fetchTeacherNamesFromServer();

  String html = htmlHeader("Agregar Materia");
  html += "<div class='card'><h2>Agregar nueva materia</h2>";
  html += "<p class='small'>Preferible: registre profesores primero (Menú → Maestros). Aquí puede elegir un profesor registrado o escribir uno nuevo.</p>";
  html += "<form method='POST' action='/materias_add'>";
  html += "Nombre materia:<br><input name='materia' required><br>";

  if (teachers.size() == 0) {
    html += "Profesor (no hay profesores registrados — ingrese nombre):<br>";
    html += "<input name='profesor' required placeholder='Nombre del profesor'><br>";
    html += "<p class='small' style='color:#b00020;'>No se encontraron profesores registrados. Puede registrar profesores en el menú de Maestros o escribir el nombre aquí.</p>";
  } else {
    html += "Profesor (seleccione):<br>";
    html += "<select name='profesor' required>";
    for (auto &t : teachers) {
      html += "<option value='" + t + "'>" + t + "</option>";
    }
    html += "</select><br>";
    html += "<p class='small'>Si desea usar un profesor no registrado, primero regístrelo en Maestros.</p>";
  }

  html += "<br>";
  html += "<input class='btn btn-green' type='submit' value='Agregar materia'> ";
  html += "<a class='btn btn-red' href='/materias'>Cancelar</a>";
  html += "</form></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

void handleMateriasAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) {
    server.send(400, "text/plain", "materia y profesor requeridos");
    return;
  }

  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();

  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }
  if (prof.length() == 0) { server.send(400, "text/plain", "profesor vacio"); return; }

  auto courses = loadCourses();
  if (coursePairExists(courses, mat, prof)) {
    String html = htmlHeader("Operación inválida");
    html += "<div class='card'><h3>Operación inválida — duplicado de materia y profesor</h3>";
    html += "<p class='small'>No se puede registrar la misma materia con el mismo profesor porque ya existe una entrada idéntica en el sistema.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-green' href='/materias/new'>Regresar</a> <a class='btn btn-blue' href='/materias'>Lista de materias</a></p>";
    html += htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  String createdAt = nowISO();
  if (!registerCourseInDb(mat, prof, createdAt)) {
    String html = htmlHeader("Error");
    html += "<div class='card'><h3>No se pudo registrar la materia en la base de datos.</h3>";
    html += "<p class='small'>Revise la conexión al servidor o los datos enviados.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a></p></div>";
    html += htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  server.sendHeader("Location", "/materias_new_schedule?materia=" + urlEncode(mat) + "&profesor=" + urlEncode(prof) + "&new=1");
  server.send(303, "text/plain", "Continuar a asignar horarios (opcional)");
}

void handleMateriasNewScheduleGET() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) {
    server.send(400, "text/plain", "materia y profesor requeridos");
    return;
  }

  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length() == 0 || prof.length() == 0) {
    server.send(400, "text/plain", "materia o profesor invalidos");
    return;
  }

  auto courses = loadCourses();
  if (!coursePairExists(courses, mat, prof)) {
    server.send(404, "text/plain", "Curso no encontrado");
    return;
  }

  bool fromNewFlow = (server.hasArg("new") && server.arg("new") == "1");
  String headerTitle = String("Asignar horarios - ") + mat + " (" + prof + ")";
  String html = htmlHeader(headerTitle.c_str());
  html += "<div class='card'><h2>Horarios para: " + mat + " — " + prof + "</h2>";

  html += "<table><tr><th>Hora</th>";
  for (int d = 0; d < 6; d++) html += "<th>" + String(DAYS[d]) + "</th>";
  html += "</tr>";

  String courseKey = makeCourseKey(mat, prof);

  for (int s = 0; s < SLOT_COUNT; s++) {
    int h = SLOT_STARTS[s];
    char lbl[16];
    snprintf(lbl, sizeof(lbl), "%02d:00 - %02d:00", h, h + 2);
    html += "<tr><th>" + String(lbl) + "</th>";

    for (int d = 0; d < 6; d++) {
      String day = DAYS[d];
      String start = String(h) + ":00";
      String end = String(h + 2) + ":00";
      String owner;
      bool occ = slotOccupiedRemote(day, start, &owner);

      html += "<td style='min-width:150px'>";
      if (occ) {
        if (scheduleBelongsToCourse(owner, mat, prof)) {
          html += "<div>" + mat + " (" + prof + ")</div>";
          html += "<div style='margin-top:6px'><form method='POST' action='/materias_new_schedule_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>"
                  "<input type='hidden' name='materia' value='" + mat + "'>"
                  "<input type='hidden' name='profesor' value='" + prof + "'>"
                  "<input type='hidden' name='day' value='" + day + "'>"
                  "<input type='hidden' name='start' value='" + start + "'>"
                  "<button class='btn btn-red' type='submit'>Eliminar</button>"
                  "</form></div>";
        } else {
          String ownerMat, ownerProf;
          if (splitCourseKey(owner, ownerMat, ownerProf)) {
            html += "<div class='occupied-other'>" + ownerMat + " (" + ownerProf + ")</div>";
          } else {
            html += "<div class='occupied-other'>" + owner + "</div>";
          }
        }
      } else {
        html += "<form method='POST' action='/materias_new_schedule_add' style='display:inline'>";
        html += "<input type='hidden' name='materia' value='" + mat + "'>";
        html += "<input type='hidden' name='profesor' value='" + prof + "'>";
        html += "<input type='hidden' name='day' value='" + day + "'>";
        html += "<input type='hidden' name='start' value='" + start + "'>";
        html += "<input type='hidden' name='end' value='" + end + "'>";
        html += "<button class='btn btn-green' type='submit'>Agregar</button>";
        html += "</form>";
      }
      html += "</td>";
    }
    html += "</tr>";
  }

  html += "</table>";

  html += "<p style='margin-top:12px'>";
  if (fromNewFlow) {
    html += "<form method='GET' action='/materias' style='display:inline'><button class='btn btn-green'>Continuar</button></form> ";
    html += "<form method='POST' action='/materias_delete' style='display:inline' onsubmit='return confirm(\"Cancelar registro y eliminar la materia? Esta acción borrará la materia y sus horarios/usuarios.\");'>";
    html += "<input type='hidden' name='materia' value='" + mat + "'>";
    html += "<input type='hidden' name='profesor' value='" + prof + "'>";
    html += "<button class='btn btn-red' type='submit'>Cancelar registro</button>";
    html += "</form>";
  } else {
    html += "<form method='GET' action='/materias' style='display:inline'><button class='btn btn-green'>Confirmar</button></form>";
  }
  html += "</p></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

void handleMateriasNewScheduleAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor") || !server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end")) {
    server.send(400, "text/plain", "faltan");
    return;
  }

  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();

  if (mat.length() == 0 || prof.length() == 0) {
    server.send(400, "text/plain", "materia o profesor vacio");
    return;
  }

  auto courses = loadCourses();
  if (!coursePairExists(courses, mat, prof)) {
    server.send(400, "text/plain", "Curso no registrado");
    return;
  }

  String courseKey = makeCourseKey(mat, prof);
  String owner;
  if (slotOccupiedRemote(day, start, &owner)) {
    if (owner != courseKey) {
      String html = htmlHeader("Horario ocupado");
      html += "<div class='card'><h3>Ese horario ya está ocupado por otra materia.</h3>";
      html += "<p class='small'>Seleccione otro horario.</p>";
      html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias_new_schedule?materia=" + urlEncode(mat) + "&profesor=" + urlEncode(prof) + "'>Volver</a></p></div>";
      html += htmlFooter();
      server.send(200, "text/html", html);
      return;
    }

    String html = htmlHeader("Horario duplicado");
    html += "<div class='card'><h3>Ese horario ya estaba asignado a este mismo curso.</h3>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias_new_schedule?materia=" + urlEncode(mat) + "&profesor=" + urlEncode(prof) + "'>Volver</a></p></div>";
    html += htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  addScheduleSlot(courseKey, day, start, end);
  Serial.println("Horario agregado en servidor");

  server.sendHeader("Location", "/materias_new_schedule?materia=" + urlEncode(mat) + "&profesor=" + urlEncode(prof));
  server.send(303, "text/plain", "Agregado");
}

void handleMateriasNewScheduleDelPOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor") || !server.hasArg("day") || !server.hasArg("start")) {
    server.send(400, "text/plain", "faltan");
    return;
  }

  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();

  if (mat.length() == 0 || prof.length() == 0) {
    server.send(400, "text/plain", "materia/profesor vacio");
    return;
  }

  String courseKey = makeCourseKey(mat, prof);
  bool ok = deleteScheduleSlot(courseKey, day, start);
  if (!ok) {
    // Fallback por compatibilidad con horarios antiguos guardados solo con materia.
    deleteScheduleSlot(mat, day, start);
  }

  server.sendHeader("Location", "/materias_new_schedule?materia=" + urlEncode(mat) + "&profesor=" + urlEncode(prof));
  server.send(303, "text/plain", "Eliminado");
}

void handleMateriasEditGET() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) {
    server.send(400, "text/plain", "materia y profesor requeridos");
    return;
  }

  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();

  auto courses = loadCourses();
  int idx = findCourseIndex(courses, mat, prof);
  if (idx == -1) {
    server.send(404, "text/plain", "Materia no encontrada");
    return;
  }

  auto teachers = fetchTeacherNamesFromServer();

  String html = htmlHeader("Editar Materia");
  html += R"rawliteral(
<style>
.edit-card { max-width:720px; margin:14px auto; padding:16px; box-sizing:border-box; }
.edit-card h2 { margin-top:0; }
.form-row { margin-bottom:12px; }
.form-row label { display:block; font-weight:600; margin-bottom:6px; }
.form-row input, .form-row select { width:100%; padding:10px; border-radius:8px; border:1px solid #dceef9; background:#fff; font-size:14px; }
.actions-row { display:flex; gap:10px; justify-content:flex-start; margin-top:12px; }
.small-note { font-size:12px; color:#666; margin-top:6px; }
</style>
)rawliteral";

  html += "<div class='card edit-card'><h2>Editar materia</h2>";
  html += "<form method='POST' action='/materias_edit'>";
  html += "<input type='hidden' name='orig_materia' value='" + mat + "'>";
  html += "<input type='hidden' name='orig_profesor' value='" + prof + "'>";

  html += "<div class='form-row'><label>Nombre materia:</label>";
  html += "<input name='materia' value='" + courses[idx].materia + "' required></div>";

  if (teachers.size() == 0) {
    html += "<div class='form-row'><label>Profesor (no hay profesores registrados — ingrese nombre):</label>";
    html += "<input name='profesor' value='" + courses[idx].profesor + "' required></div>";
    html += "<div class='small-note'>No se encontraron profesores registrados. Puede registrar profesores en el menú de Maestros o escribir el nombre aquí.</div>";
  } else {
    html += "<div class='form-row'><label>Profesor (seleccione):</label>";
    bool currentInList = false;
    html += "<select name='profesor' required>";
    for (auto &t : teachers) {
      if (t == courses[idx].profesor) {
        html += "<option value='" + t + "' selected>" + t + "</option>";
        currentInList = true;
      } else {
        html += "<option value='" + t + "'>" + t + "</option>";
      }
    }
    if (!currentInList) {
      html += "<option value='" + courses[idx].profesor + "' selected>" + courses[idx].profesor + "</option>";
    }
    html += "</select></div>";
    html += "<div class='small-note'>Si desea un profesor diferente que no aparece en la lista, agréguelo en Maestros y luego estará disponible aquí.</div>";
  }

  html += "<div class='actions-row'><button class='btn btn-green' type='submit'>Guardar cambios</button>";
  html += "<a class='btn btn-blue' href='/materias'>Volver</a></div>";
  html += "</form></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

void handleMateriasEditPOST() {
  if (!server.hasArg("orig_materia") || !server.hasArg("orig_profesor") || !server.hasArg("materia") || !server.hasArg("profesor")) {
    server.send(400, "text/plain", "faltan");
    return;
  }

  String orig = server.arg("orig_materia"); orig.trim();
  String origProf = server.arg("orig_profesor"); origProf.trim();
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();

  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }
  if (prof.length() == 0) { server.send(400, "text/plain", "profesor vacio"); return; }

  auto courses = loadCourses();
  int origIndex = findCourseIndex(courses, orig, origProf);
  if (origIndex == -1) {
    server.send(404, "text/plain", "Materia no encontrada");
    return;
  }

  for (int i = 0; i < (int)courses.size(); i++) {
    if (i == origIndex) continue;
    if (courses[i].materia == mat && courses[i].profesor == prof) {
      String html = htmlHeader("Duplicado");
      html += "<div class='card'><h3>No se puede guardar: otra entrada ya tiene la misma materia y profesor.</h3>";
      html += "<p class='small'>Edite los datos para evitar duplicados de materia+profesor.</p>";
      html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a></p>";
      html += htmlFooter();
      server.send(200, "text/html", html);
      return;
    }
  }

  String oldMat = courses[origIndex].materia;
  String oldProf = courses[origIndex].profesor;
  String oldCreated = courses[origIndex].created_at;
  bool oldNameWasUnique = (countCoursesWithName(courses, oldMat) == 1);

  // Actualización en BD
  if (!updateCourseInDb(oldMat, mat, prof, oldCreated)) {
    String html = htmlHeader("Error");
    html += "<div class='card'><h3>No se pudo actualizar la materia en la base de datos.</h3>";
    html += "<p class='small'>Revise la conexión al servidor o los datos enviados.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a></p></div>";
    html += htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // Migrar horarios y alumnos si el nombre de la materia cambió y era único
  migrateSchedulesForCourseRename(oldMat, oldProf, mat, prof, oldNameWasUnique);
  migrateStudentsForCourseRename(oldMat, mat, oldNameWasUnique);

  server.sendHeader("Location", "/materias");
  server.send(303, "text/plain", "Editado");
}

void handleMateriasDeletePOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) {
    server.send(400, "text/plain", "materia y profesor requeridos");
    return;
  }

  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length() == 0 || prof.length() == 0) {
    server.send(400, "text/plain", "materia/profesor vacio");
    return;
  }

  auto courses = loadCourses();
  int idx = findCourseIndex(courses, mat, prof);
  if (idx == -1) {
    server.send(404, "text/plain", "Materia no encontrada");
    return;
  }

  bool oldNameWasUnique = (countCoursesWithName(courses, mat) == 1);

  if (!deleteCourseInDb(mat, true)) {
    String html = htmlHeader("Error");
    html += "<div class='card'><h3>No se pudo eliminar la materia en la base de datos.</h3>";
    html += "<p class='small'>Revise la conexión al servidor o intente nuevamente.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a></p></div>";
    html += htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  deleteCourseRelatedSchedules(mat, prof, oldNameWasUnique);

  if (oldNameWasUnique) {
    deleteStudentsForDeletedCourse(mat);
  }

  server.sendHeader("Location", "/materias");
  server.send(303, "text/plain", "Eliminado");
}

// Endpoint JSON para obtener profesores por materia
void handleProfesoresForMateriaGET() {
  String mat = "";
  if (server.hasArg("materia")) {
    mat = server.arg("materia");
    mat.trim();
  }

  std::vector<String> profs = getProfessorsForMateriaFromServer(mat);
  String j = "{\"profesores\":[";
  for (size_t i = 0; i < profs.size(); ++i) {
    if (i) j += ",";
    j += "\"" + jsonEscape(profs[i]) + "\"";
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// Registro de rutas relacionadas con materias
void registerCoursesHandlers() {
  server.on("/materias", HTTP_GET, handleMaterias);
  server.on("/materias/new", HTTP_GET, handleMateriasNew);
  server.on("/materias_add", HTTP_POST, handleMateriasAddPOST);

  server.on("/materias_new_schedule", HTTP_GET, handleMateriasNewScheduleGET);
  server.on("/materias_new_schedule_add", HTTP_POST, handleMateriasNewScheduleAddPOST);
  server.on("/materias_new_schedule_del", HTTP_POST, handleMateriasNewScheduleDelPOST);

  server.on("/materias/edit", HTTP_GET, handleMateriasEditGET);
  server.on("/materias_edit", HTTP_POST, handleMateriasEditPOST);
  server.on("/materias_delete", HTTP_POST, handleMateriasDeletePOST);

  server.on("/profesores_for", HTTP_GET, handleProfesoresForMateriaGET);
}