// src/web/teachers.cpp
#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <ArduinoJson.h>

#include "teachers.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include "db_sync.h"
#include "time_utils.h"

// ------------------------------------------------------------
// Helpers de texto
// ------------------------------------------------------------

static String htmlEscape(const String &s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

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

static String trimCopy(String s) {
  s.trim();
  return s;
}

// ------------------------------------------------------------
// JSON helpers
// ------------------------------------------------------------

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
    const char* keys[] = {"data", "items", "rows", "result", "response", "profesores", "teachers", "payload", "list"};
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
// Modelo
// ------------------------------------------------------------

struct TeacherRec {
  int id = -1;
  String uid;
  String name;
  String acc;
  String materia;
  String created;
};

struct TeacherGroup {
  String uid;
  String name;
  String acc;
  String created;
  std::vector<String> materias;
};

static String baseMateriaFromStoredField(const String &materiaField) {
  String m = materiaField;
  m.trim();
  int idx = m.indexOf("||");
  if (idx >= 0) {
    m = m.substring(0, idx);
    m.trim();
  }
  return m;
}

static String teacherFromStoredField(const String &materiaField) {
  String m = materiaField;
  m.trim();
  int idx = m.indexOf("||");
  if (idx >= 0) {
    String prof = m.substring(idx + 2);
    prof.trim();
    return prof;
  }
  return String();
}

static void appendUnique(std::vector<String> &vec, const String &value) {
  String v = value;
  v.trim();
  if (!v.length()) return;
  for (auto &x : vec) {
    if (x == v) return;
  }
  vec.push_back(v);
}

static TeacherRec parseTeacherRecFromObject(const JsonObjectConst &obj) {
  TeacherRec r;

  const char* idKeys[]      = {"id", "teacher_id", "profesor_id", "registro_id"};
  const char* uidKeys[]     = {"rfid_uid", "uid", "UID", "rfid", "codigo", "tag_uid"};
  const char* nameKeys[]    = {"name", "nombre", "full_name", "nombre_completo", "nombreCompleto"};
  const char* accKeys[]     = {"account", "cuenta", "matricula", "registro", "numero_cuenta"};
  const char* matKeys[]     = {"materia", "subject", "asignatura", "nombre_materia", "nombreMateria"};
  const char* createdKeys[] = {"created_at", "createdAt", "fecha", "timestamp", "created"};

  String idStr = jsonGetAny(obj, idKeys, sizeof(idKeys) / sizeof(idKeys[0]));
  r.id = idStr.length() ? idStr.toInt() : -1;
  r.uid = jsonGetAny(obj, uidKeys, sizeof(uidKeys) / sizeof(uidKeys[0]));
  r.name = jsonGetAny(obj, nameKeys, sizeof(nameKeys) / sizeof(nameKeys[0]));
  r.acc = jsonGetAny(obj, accKeys, sizeof(accKeys) / sizeof(accKeys[0]));
  r.materia = jsonGetAny(obj, matKeys, sizeof(matKeys) / sizeof(matKeys[0]));
  r.created = jsonGetAny(obj, createdKeys, sizeof(createdKeys) / sizeof(createdKeys[0]));

  r.uid.trim();
  r.name.trim();
  r.acc.trim();
  r.materia.trim();
  r.created.trim();

  return r;
}

static void expandTeacherItem(JsonVariantConst item, std::vector<TeacherRec> &out) {
  if (!item.is<JsonObjectConst>()) return;

  JsonObjectConst o = item.as<JsonObjectConst>();
  TeacherRec base = parseTeacherRecFromObject(o);

  // Si el backend devuelve materias como array, expandimos una fila por materia.
  if (o.containsKey("materias") && o["materias"].is<JsonArrayConst>()) {
    JsonArrayConst arr = o["materias"].as<JsonArrayConst>();
    bool pushedAny = false;

    for (JsonVariantConst m : arr) {
      String mat = jsonVariantToString(m);
      mat.trim();
      if (!mat.length()) continue;

      TeacherRec r = base;
      r.materia = mat;
      out.push_back(r);
      pushedAny = true;
    }

    if (pushedAny) return;
  }

  // Si el campo materia viene embebido como "Materia||Profesor", lo dejamos tal cual.
  out.push_back(base);
}

static std::vector<TeacherRec> fetchTeachersFromServer() {
  std::vector<TeacherRec> out;
  if (!serverSeemsReady()) return out;

  String json = listProfesores();
  if (!json.length()) return out;

  DynamicJsonDocument doc(32768);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("WARN: no se pudo parsear JSON de listProfesores(): ");
    Serial.println(err.c_str());
    return out;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    expandTeacherItem(item, out);
  });

  return out;
}

static bool fetchTeacherByUid(const String &uid, TeacherRec &out) {
  out = TeacherRec();

  if (!serverSeemsReady()) return false;

  String json = getProfesorByUid(uid);
  if (!json.length()) return false;

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("WARN: no se pudo parsear JSON de getProfesorByUid(): ");
    Serial.println(err.c_str());
    return false;
  }

  std::vector<TeacherRec> tmp;
  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    expandTeacherItem(item, tmp);
  });

  if (tmp.empty()) return false;

  // Tomamos la primera coincidencia útil
  out = tmp[0];
  if (out.uid.length() == 0) out.uid = uid;
  return true;
}

static std::vector<TeacherGroup> groupTeachers(const std::vector<TeacherRec> &rows) {
  std::vector<TeacherGroup> groups;

  for (auto &r : rows) {
    String key = r.uid.length() ? r.uid : r.name;
    int idx = -1;

    for (int i = 0; i < (int)groups.size(); ++i) {
      String gkey = groups[i].uid.length() ? groups[i].uid : groups[i].name;
      if (gkey == key) {
        idx = i;
        break;
      }
    }

    if (idx == -1) {
      TeacherGroup g;
      g.uid = r.uid;
      g.name = r.name;
      g.acc = r.acc;
      g.created = r.created.length() ? r.created : nowISO();
      if (r.materia.length()) appendUnique(g.materias, baseMateriaFromStoredField(r.materia));
      groups.push_back(g);
    } else {
      if (r.uid.length() && groups[idx].uid.length() == 0) groups[idx].uid = r.uid;
      if (r.name.length() && groups[idx].name.length() == 0) groups[idx].name = r.name;
      if (r.acc.length() && groups[idx].acc.length() == 0) groups[idx].acc = r.acc;
      if (r.created.length() && groups[idx].created.length() == 0) groups[idx].created = r.created;

      if (r.materia.length()) appendUnique(groups[idx].materias, baseMateriaFromStoredField(r.materia));
    }
  }

  return groups;
}

static std::vector<String> coursesProfessorsForMateria(const String &materia) {
  std::vector<String> out;
  auto courses = loadCourses();

  for (auto &c : courses) {
    if (trimCopy(c.materia) == trimCopy(materia) && c.profesor.length()) {
      appendUnique(out, c.profesor);
    }
  }

  return out;
}

static std::vector<TeacherGroup> teachersForMateriaFromServer(const String &materia) {
  std::vector<TeacherGroup> groups;
  auto rows = fetchTeachersFromServer();

  for (auto &r : rows) {
    if (trimCopy(baseMateriaFromStoredField(r.materia)) != trimCopy(materia)) continue;

    String key = r.uid.length() ? r.uid : r.name;
    int idx = -1;
    for (int i = 0; i < (int)groups.size(); ++i) {
      String gkey = groups[i].uid.length() ? groups[i].uid : groups[i].name;
      if (gkey == key) {
        idx = i;
        break;
      }
    }

    if (idx == -1) {
      TeacherGroup g;
      g.uid = r.uid;
      g.name = r.name;
      g.acc = r.acc;
      g.created = r.created.length() ? r.created : nowISO();
      appendUnique(g.materias, baseMateriaFromStoredField(r.materia));
      groups.push_back(g);
    } else {
      if (r.uid.length() && groups[idx].uid.length() == 0) groups[idx].uid = r.uid;
      if (r.name.length() && groups[idx].name.length() == 0) groups[idx].name = r.name;
      if (r.acc.length() && groups[idx].acc.length() == 0) groups[idx].acc = r.acc;
      if (r.created.length() && groups[idx].created.length() == 0) groups[idx].created = r.created;
      appendUnique(groups[idx].materias, baseMateriaFromStoredField(r.materia));
    }
  }

  if (!groups.empty()) return groups;

  // Fallback: materias/cursos si el backend todavía no devuelve profesor_materia en listProfesores
  auto profs = coursesProfessorsForMateria(materia);
  for (auto &p : profs) {
    TeacherGroup g;
    g.uid = "";
    g.name = p;
    g.acc = "-";
    g.created = nowISO();
    appendUnique(g.materias, materia);
    groups.push_back(g);
  }

  return groups;
}

static std::vector<String> uniqueMateriasFromTeachers() {
  std::vector<String> out;
  auto rows = fetchTeachersFromServer();

  for (auto &r : rows) {
    String mat = baseMateriaFromStoredField(r.materia);
    if (!mat.length()) continue;
    appendUnique(out, mat);
  }

  return out;
}

static std::vector<String> materiasForTeacherName(const String &name) {
  std::vector<String> out;
  auto rows = fetchTeachersFromServer();

  for (auto &r : rows) {
    if (trimCopy(r.name) == trimCopy(name)) {
      appendUnique(out, baseMateriaFromStoredField(r.materia));
    }
  }

  if (out.empty()) {
    auto courses = loadCourses();
    for (auto &c : courses) {
      if (trimCopy(c.profesor) == trimCopy(name)) {
        appendUnique(out, trimCopy(c.materia));
      }
    }
  }

  return out;
}

static bool deleteTeacherMateriaRowsByUidAndMateria(const String &uid, const String &materia) {
  auto rows = fetchTeachersFromServer();
  bool found = false;
  bool ok = true;

  for (auto &r : rows) {
    if (trimCopy(r.uid) == trimCopy(uid) && trimCopy(baseMateriaFromStoredField(r.materia)) == trimCopy(materia)) {
      found = true;
      if (r.id > 0) {
        if (!deleteProfesorMateriaById(r.id)) {
          Serial.print("WARN: no se pudo borrar profesor_materia id=");
          Serial.println(r.id);
          ok = false;
        } else {
          Serial.print("DB_SYNC: profesor_materia eliminado id=");
          Serial.println(r.id);
        }
      }
    }
  }

  if (!found) {
    Serial.println("DB_SYNC: no había relación profesor-materia coincidente en Oracle");
  }

  return ok;
}

static bool deleteTeacherRowsByUid(const String &uid) {
  TeacherRec teacher;
  bool haveTeacher = fetchTeacherByUid(uid, teacher);
  auto rows = fetchTeachersFromServer();

  bool any = false;
  bool ok = true;

  for (auto &r : rows) {
    if (trimCopy(r.uid) == trimCopy(uid) && r.id > 0) {
      any = true;
      if (!deleteProfesorMateriaById(r.id)) {
        Serial.print("WARN: no se pudo borrar relación profesor-materia id=");
        Serial.println(r.id);
        ok = false;
      }
    }
  }

  // Borrado total del profesor
  if (!deleteProfesorByUid(uid, true)) {
    Serial.println("WARN: no se pudo eliminar profesor en Oracle con cascade=true");
    ok = false;
  } else {
    Serial.println("DB_SYNC: profesor eliminado en Oracle con cascade=true");
  }

  if (!any && !haveTeacher) {
    Serial.println("DB_SYNC: no se encontró profesor para eliminar");
  }

  return ok;
}

// ------------------------------------------------------------
// Handlers
// ------------------------------------------------------------

void handleTeachersForMateria() {
  if (!server.hasArg("materia")) {
    server.send(400, "text/plain", "materia required");
    return;
  }

  String materia = server.arg("materia");
  materia.trim();

  String html = htmlHeader(("Maestros - " + materia).c_str());
  html += "<div class='card'><h2>Maestros - " + htmlEscape(materia) + "</h2>";

  String rt = String("/teachers?materia=") + urlEncodeLocal(materia);
  html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px;gap:8px;'>";
  html += "<a class='btn btn-blue' href='/capture_individual?return_to=" + urlEncodeLocal(rt) + "&target=teachers'>Capturar Maestro</a>";
  html += "</div>";

  html += "<div class='filters'>"
          "<input id='tf_name' placeholder='Filtrar Nombre'>"
          "<input id='tf_acc' placeholder='Filtrar Cuenta'>"
          "<button class='search-btn btn btn-blue' onclick='applyTeacherFilters()'>Buscar</button>"
          "<button class='search-btn btn btn-green' onclick='clearTeacherFilters()'>Limpiar</button>"
          "</div>";

  auto groups = teachersForMateriaFromServer(materia);

  if (groups.size() == 0) {
    html += "<p>No hay maestros registrados para esta materia.</p>";
  } else {
    html += "<table id='teachers_mat_table'><tr><th>Nombre</th><th>Cuenta</th><th>Registro</th><th>Acciones</th></tr>";
    for (auto &g : groups) {
      String mats = "";
      for (size_t i = 0; i < g.materias.size(); ++i) {
        if (i) mats += "; ";
        mats += g.materias[i];
      }

      html += "<tr><td>" + htmlEscape(g.name) + "</td><td>" + htmlEscape(g.acc) + "</td><td>" + htmlEscape(g.created) + "</td>";
      html += "<td>";

      if (g.uid.length()) {
        html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncodeLocal(g.uid) + "&return_to=" + urlEncodeLocal(rt) + "'>✏️ Editar</a> ";
      } else {
        html += "<a class='btn btn-blue' href='/capture_individual?return_to=" + urlEncodeLocal(rt) + "&target=teachers'>Capturar Maestro</a> ";
      }

      if (g.uid.length()) {
        html += "<form method='POST' action='/teacher_remove_course' style='display:inline;margin-left:6px;' onsubmit='return confirm(\"Eliminar este maestro de la materia?\");'>";
        html += "<input type='hidden' name='uid' value='" + htmlEscape(g.uid) + "'>";
        html += "<input type='hidden' name='materia' value='" + htmlEscape(materia) + "'>";
        html += "<input class='btn btn-red' type='submit' value='Eliminar del curso'>";
        html += "</form>";
      }

      html += "</td></tr>";
    }
    html += "</table>";

    html += "<script>"
            "function applyTeacherFilters(){"
            "const table=document.getElementById('teachers_mat_table'); if(!table) return;"
            "const f1=document.getElementById('tf_name').value.trim().toLowerCase();"
            "const f2=document.getElementById('tf_acc').value.trim().toLowerCase();"
            "for(let r=1;r<table.rows.length;r++){"
            "const row=table.rows[r]; if(row.cells.length<3) continue;"
            "const name=row.cells[0].textContent.toLowerCase();"
            "const acc=row.cells[1].textContent.toLowerCase();"
            "const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1);"
            "row.style.display = ok ? '' : 'none';"
            "}"
            "}"
            "function clearTeacherFilters(){"
            "document.getElementById('tf_name').value='';"
            "document.getElementById('tf_acc').value='';"
            "applyTeacherFilters();"
            "}"
            "</script>";
  }

  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleTeachersAll() {
  String searchUid = server.hasArg("search_uid") ? server.arg("search_uid") : String();

  String html = htmlHeader("Maestros - Todos");
  html += "<div class='card'><h2>Todos los maestros</h2>";

  html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px;gap:8px;'>";
  html += "<a class='btn btn-blue' href='/capture_individual?return_to=/teachers_all&target=teachers'>Capturar Maestro</a>";
  html += "</div>";

  html += "<div class='filters'>"
          "<input id='ta_name' placeholder='Filtrar Nombre'>"
          "<input id='ta_acc' placeholder='Filtrar Cuenta'>"
          "<input id='ta_mat' placeholder='Filtrar Materia'>"
          "<button class='search-btn btn btn-blue' onclick='applyAllTeacherFilters()'>Buscar</button>"
          "<button class='search-btn btn btn-green' onclick='clearAllTeacherFilters()'>Limpiar</button>"
          "</div>";

  auto rows = fetchTeachersFromServer();
  auto groups = groupTeachers(rows);

  if (groups.size() == 0) {
    html += "<p>No hay maestros registrados.</p>";
  } else {
    if (searchUid.length()) {
      bool foundAny = false;

      TeacherRec tr;
      if (fetchTeacherByUid(searchUid, tr)) {
        foundAny = true;

        std::vector<String> mats = materiasForTeacherName(tr.name);
        if (mats.empty()) appendUnique(mats, baseMateriaFromStoredField(tr.materia));

        String matsStr = "-";
        if (!mats.empty()) {
          matsStr = "";
          for (size_t i = 0; i < mats.size(); ++i) {
            if (i) matsStr += "; ";
            matsStr += mats[i];
          }
        }

        html += "<table id='teachers_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
        html += "<tr><td>" + htmlEscape(tr.name) + "</td><td>" + htmlEscape(tr.acc.length() ? tr.acc : String("-")) + "</td><td>" + htmlEscape(matsStr) + "</td><td>" + htmlEscape(tr.created.length() ? tr.created : nowISO()) + "</td><td>";

        if (tr.uid.length()) {
          html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncodeLocal(tr.uid) + "&return_to=" + urlEncodeLocal(String("/teachers_all")) + "'>✏️ Editar</a> ";
          html += "<form method='POST' action='/teacher_delete' style='display:inline;margin-left:6px;' onsubmit='return confirm(\"Eliminar totalmente este maestro? Esto puede eliminar materias y horarios asociados.\");'>";
          html += "<input type='hidden' name='uid' value='" + htmlEscape(tr.uid) + "'>";
          html += "<input class='btn btn-red' type='submit' value='Eliminar totalmente'>";
          html += "</form>";
        }

        html += "</td></tr></table>";
      }

      if (!foundAny) {
        html += "<p>No se encontró maestro con UID " + htmlEscape(searchUid) + ".</p>";
      }
    } else {
      html += "<table id='teachers_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";

      for (auto &g : groups) {
        String mats = "";
        for (size_t i = 0; i < g.materias.size(); ++i) {
          if (i) mats += "; ";
          mats += g.materias[i];
        }
        if (mats.length() == 0) mats = "-";

        html += "<tr><td>" + htmlEscape(g.name) + "</td><td>" + htmlEscape(g.acc.length() ? g.acc : String("-")) + "</td><td>" + htmlEscape(mats) + "</td><td>" + htmlEscape(g.created.length() ? g.created : nowISO()) + "</td><td>";

        if (g.uid.length()) {
          html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncodeLocal(g.uid) + "&return_to=" + urlEncodeLocal(String("/teachers_all")) + "'>✏️ Editar</a> ";
          html += "<form method='POST' action='/teacher_delete' style='display:inline;margin-left:6px;' onsubmit='return confirm(\"Eliminar totalmente este maestro? Esto puede eliminar materias y horarios asociados.\");'>";
          html += "<input type='hidden' name='uid' value='" + htmlEscape(g.uid) + "'>";
          html += "<input class='btn btn-red' type='submit' value='Eliminar totalmente'>";
          html += "</form>";
        } else {
          html += "<a class='btn btn-blue' href='/capture_individual?return_to=/teachers_all&target=teachers'>Capturar Maestro</a>";
        }

        html += "</td></tr>";
      }

      html += "</table>";

      html += "<script>"
              "function applyAllTeacherFilters(){"
              "const table=document.getElementById('teachers_all_table'); if(!table) return;"
              "const f1=document.getElementById('ta_name').value.trim().toLowerCase();"
              "const f2=document.getElementById('ta_acc').value.trim().toLowerCase();"
              "const f3=document.getElementById('ta_mat').value.trim().toLowerCase();"
              "for(let r=1;r<table.rows.length;r++){"
              "const row=table.rows[r]; if(row.cells.length<4) continue;"
              "const name=row.cells[0].textContent.toLowerCase();"
              "const acc=row.cells[1].textContent.toLowerCase();"
              "const mats=row.cells[2].textContent.toLowerCase();"
              "const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1)&&(mats.indexOf(f3)!==-1);"
              "row.style.display = ok ? '' : 'none';"
              "}"
              "}"
              "function clearAllTeacherFilters(){"
              "document.getElementById('ta_name').value='';"
              "document.getElementById('ta_acc').value='';"
              "document.getElementById('ta_mat').value='';"
              "applyAllTeacherFilters();"
              "}"
              "</script>";
    }
  }

  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleTeacherRemoveCourse() {
  if (!server.hasArg("uid") || !server.hasArg("materia")) {
    server.send(400, "text/plain", "faltan");
    return;
  }

  String uid = server.arg("uid");
  String materia = server.arg("materia");

  if (!deleteTeacherMateriaRowsByUidAndMateria(uid, materia)) {
    Serial.println("WARN: no se pudo sincronizar la eliminación de maestro de la materia en Oracle");
  }

  server.sendHeader("Location", "/teachers?materia=" + urlEncodeLocal(materia));
  server.send(303, "text/plain", "Removed");
}

void handleTeacherDelete() {
  if (!server.hasArg("uid")) {
    server.send(400, "text/plain", "faltan");
    return;
  }

  String uid = server.arg("uid");

  if (!deleteTeacherRowsByUid(uid)) {
    Serial.println("WARN: no se pudo sincronizar la eliminación total del maestro en Oracle");
  }

  server.sendHeader("Location", "/teachers_all");
  server.send(303, "text/plain", "Deleted");
}

// ------------------------------------------------------------
// Registro de rutas
// ------------------------------------------------------------

void registerTeachersHandlers() {
  server.on("/teachers", HTTP_GET, handleTeachersForMateria);
  server.on("/teachers_all", HTTP_GET, handleTeachersAll);
  server.on("/teacher_remove_course", HTTP_POST, handleTeacherRemoveCourse);
  server.on("/teacher_delete", HTTP_POST, handleTeacherDelete);
}