// src/web/students.cpp
#include <Arduino.h>
#include <vector>
#include <algorithm>

#include "students.h"
#include "globals.h"
#include "web_common.h"
#include "db_sync.h"
#include "time_utils.h"

#include <ArduinoJson.h>

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

static String urlEncode(const String &str) {
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

static String lowerCopy(String s) {
  s.trim();
  s.toLowerCase();
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
    for (JsonVariantConst item : arr) {
      callback(item);
    }
    return;
  }

  if (root.is<JsonObjectConst>()) {
    JsonObjectConst o = root.as<JsonObjectConst>();
    const char* keys[] = {"data", "items", "rows", "result", "response", "alumnos", "students", "payload"};
    for (const char* key : keys) {
      if (o.containsKey(key) && o[key].is<JsonArrayConst>()) {
        JsonArrayConst arr = o[key].as<JsonArrayConst>();
        for (JsonVariantConst item : arr) {
          callback(item);
        }
        return;
      }
    }

    // Si no hay arreglo interno, tratamos el objeto como un solo registro
    callback(root);
  }
}

static bool serverSeemsReady() {
  return (WiFi.status() == WL_CONNECTED) && pingServer();
}

static String normalizeMateriaField(String s) {
  s.trim();
  int idx = s.indexOf("||");
  if (idx >= 0) s = s.substring(0, idx);
  s.trim();
  return s;
}

static String makeMateriaKey(const String &materia, const String &profesor) {
  if (profesor.length()) return materia + String("||") + profesor;
  return materia;
}

// ------------------------------------------------------------
// Modelo de alumno en Oracle
// ------------------------------------------------------------

struct OracleAlumnoRec {
  int id = -1;
  String uid;
  String name;
  String account;
  String materia;
  String created_at;
};

static OracleAlumnoRec parseAlumnoRec(JsonObjectConst obj) {
  OracleAlumnoRec r;

  const char* idKeys[]      = {"id", "alumno_id", "student_id", "registro_id"};
  const char* uidKeys[]     = {"rfid_uid", "uid", "UID", "rfid", "codigo"};
  const char* nameKeys[]    = {"name", "nombre", "full_name", "nombre_completo", "nombreCompleto"};
  const char* accKeys[]     = {"account", "cuenta", "matricula", "registro", "numero_cuenta"};
  const char* matKeys[]     = {"materia", "subject", "asignatura", "nombre_materia", "nombreMateria"};
  const char* createdKeys[] = {"created_at", "createdAt", "fecha", "timestamp", "created"};

  String idStr = jsonGetAny(obj, idKeys, sizeof(idKeys) / sizeof(idKeys[0]));
  r.id = idStr.length() ? idStr.toInt() : -1;
  r.uid = jsonGetAny(obj, uidKeys, sizeof(uidKeys) / sizeof(uidKeys[0]));
  r.name = jsonGetAny(obj, nameKeys, sizeof(nameKeys) / sizeof(nameKeys[0]));
  r.account = jsonGetAny(obj, accKeys, sizeof(accKeys) / sizeof(accKeys[0]));
  r.materia = jsonGetAny(obj, matKeys, sizeof(matKeys) / sizeof(matKeys[0]));
  r.created_at = jsonGetAny(obj, createdKeys, sizeof(createdKeys) / sizeof(createdKeys[0]));

  r.uid.trim();
  r.name.trim();
  r.account.trim();
  r.materia.trim();
  r.created_at.trim();

  return r;
}

static bool loadOracleAlumnos(std::vector<OracleAlumnoRec> &out) {
  out.clear();

  if (!serverSeemsReady()) {
    Serial.println("WARN: servidor no disponible para cargar alumnos");
    return false;
  }

  String json = listAlumnos();
  if (!json.length()) {
    Serial.println("WARN: listAlumnos() devolvió vacío");
    return false;
  }

  DynamicJsonDocument doc(32768);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.print("WARN: no se pudo parsear JSON de alumnos Oracle: ");
    Serial.println(err.c_str());
    return false;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    out.push_back(parseAlumnoRec(item.as<JsonObjectConst>()));
  });

  return true;
}

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

static std::vector<OracleAlumnoRec> fetchAlumnosForMateria(const String &materia) {
  std::vector<OracleAlumnoRec> rows;
  std::vector<OracleAlumnoRec> all;
  if (!loadOracleAlumnos(all)) return rows;

  String target = trimCopy(materia);

  for (auto &r : all) {
    String rowMat = baseMateriaFromStoredField(r.materia);
    if (rowMat == target) {
      rows.push_back(r);
    }
  }

  return rows;
}

static bool oracleDeleteStudentRowsByUID(const String &uid) {
  std::vector<OracleAlumnoRec> rows;
  if (!loadOracleAlumnos(rows)) {
    Serial.println("WARN: no se pudieron leer alumnos Oracle para borrar por UID");
    return false;
  }

  bool any = false;
  bool ok = true;

  for (auto &r : rows) {
    if (r.uid == uid && r.id > 0) {
      any = true;
      if (!deleteAlumnoById(r.id)) {
        Serial.print("WARN: no se pudo borrar alumno Oracle id=");
        Serial.println(r.id);
        ok = false;
      } else {
        Serial.print("DB_SYNC: alumno Oracle eliminado id=");
        Serial.println(r.id);
      }
    }
  }

  if (!any) {
    Serial.println("DB_SYNC: no había filas Oracle para ese UID");
  }

  return ok;
}

static bool oracleRemoveStudentCourse(
    const String &uid,
    const String &materia,
    const String &fallbackName,
    const String &fallbackAccount,
    const String &fallbackCreatedAt
) {
  std::vector<OracleAlumnoRec> rows;
  if (!loadOracleAlumnos(rows)) {
    Serial.println("WARN: no se pudieron leer alumnos Oracle para quitar materia");
    return false;
  }

  std::vector<OracleAlumnoRec> matches;
  for (auto &r : rows) {
    if (r.uid == uid) matches.push_back(r);
  }

  if (matches.empty()) {
    Serial.println("DB_SYNC: no se encontró el UID en Oracle para quitar materia");
    return true;
  }

  std::vector<OracleAlumnoRec> exactMatches;
  for (auto &r : matches) {
    if (baseMateriaFromStoredField(r.materia) == trimCopy(materia)) {
      exactMatches.push_back(r);
    }
  }

  if (!exactMatches.empty()) {
    bool ok = true;
    for (auto &r : exactMatches) {
      if (r.id > 0) {
        if (!deleteAlumnoById(r.id)) {
          Serial.print("WARN: no se pudo borrar la materia del alumno en Oracle id=");
          Serial.println(r.id);
          ok = false;
        } else {
          Serial.print("DB_SYNC: materia eliminada del alumno en Oracle id=");
          Serial.println(r.id);
        }
      }
    }
    return ok;
  }

  // Si no hay coincidencia exacta, usar la regla antigua:
  // si solo hay una fila para ese UID, la quitamos y la reinsertamos sin materia.
  if (matches.size() == 1) {
    OracleAlumnoRec row = matches[0];
    if (row.id > 0) {
      if (!deleteAlumnoById(row.id)) {
        Serial.println("WARN: no se pudo borrar la única fila Oracle del alumno");
        return false;
      }
      Serial.println("DB_SYNC: fila Oracle del alumno eliminada para reinsertar sin materia");
    }

    String name = fallbackName.length() ? fallbackName : row.name;
    String account = fallbackAccount.length() ? fallbackAccount : row.account;
    String created = fallbackCreatedAt.length() ? fallbackCreatedAt : row.created_at;
    if (created.length() == 0) created = nowISO();

    if (!sendAlumnoRegistro(uid, name, account, String(), created)) {
      Serial.println("WARN: no se pudo reinsertar alumno en Oracle sin materia");
      return false;
    }

    Serial.println("DB_SYNC: alumno reinsertado en Oracle sin materia");
    return true;
  }

  // Si hay varias filas y ninguna coincide por materia, no hacemos una edición arriesgada.
  Serial.println("WARN: no se encontró coincidencia exacta de materia en Oracle para quitar");
  return false;
}

static std::vector<String> uniqueMatsFromRows(const std::vector<OracleAlumnoRec> &rows) {
  std::vector<String> mats;
  for (auto &r : rows) {
    String m = baseMateriaFromStoredField(r.materia);
    if (!m.length()) continue;

    bool found = false;
    for (auto &x : mats) {
      if (x == m) { found = true; break; }
    }
    if (!found) mats.push_back(m);
  }
  return mats;
}

// ------------------------------------------------------------
// GET /students?materia=...
// ------------------------------------------------------------
void handleStudentsForMateria() {
  if (!server.hasArg("materia")) {
    server.send(400, "text/plain", "materia required");
    return;
  }

  String materia = server.arg("materia");
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : String();
  bool hideCaptureButtons = server.hasArg("hide_capture") && server.arg("hide_capture") == "1";

  String html = htmlHeader(("Alumnos - " + materia).c_str());
  html += "<div class='card'><h2>Alumnos - " + htmlEscape(materia) + "</h2>";

  String rt = String("/students?materia=") + urlEncode(materia);
  if (profesor.length()) rt += "&profesor=" + urlEncode(profesor);

  if (!hideCaptureButtons) {
    html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px;gap:8px;'>";
    html += "<a class='btn btn-blue' href='/capture_individual?return_to=" + urlEncode(rt) + "&target=students'>Capturar individual</a>";
    html += "<a class='btn btn-blue' href='/capture_batch?return_to=" + urlEncode(rt) + "'>Capturar lote</a>";
    html += "</div>";
  } else {
    html += "<div style='height:8px;margin-bottom:8px;'></div>";
  }

  html += "<div class='filters'>"
          "<input id='sf_name' placeholder='Filtrar Nombre'>"
          "<input id='sf_acc' placeholder='Filtrar Cuenta'>"
          "<button class='search-btn btn btn-blue' onclick='applyStudentFilters()'>Buscar</button>"
          "<button class='search-btn btn btn-green' onclick='clearStudentFilters()'>Limpiar</button>"
          "</div>";

  auto users = fetchAlumnosForMateria(materia);

  if (users.size() == 0) {
    html += "<p>No hay alumnos registrados para esta materia.</p>";
  } else {
    html += "<table id='students_mat_table'><tr><th>Nombre</th><th>Cuenta</th><th>Registro</th><th>Acciones</th></tr>";
    for (auto &r : users) {
      html += "<tr><td>" + htmlEscape(r.name) + "</td><td>" + htmlEscape(r.account) + "</td><td>" + htmlEscape(r.created_at.length() ? r.created_at : nowISO()) + "</td>";

      html += "<td>";
      html += "<form method='POST' action='/student_remove_course' style='display:inline' onsubmit='return confirm(\"Eliminar este alumno de la materia?\");'>";
      html += "<input type='hidden' name='uid' value='" + htmlEscape(r.uid) + "'>";
      html += "<input type='hidden' name='materia' value='" + htmlEscape(materia) + "'>";
      if (profesor.length()) html += "<input type='hidden' name='profesor' value='" + htmlEscape(profesor) + "'>";
      if (return_to.length()) html += "<input type='hidden' name='return_to' value='" + htmlEscape(return_to) + "'>";
      html += "<input type='hidden' name='hide_capture' value='" + String(hideCaptureButtons ? "1" : "0") + "'>";
      html += "<input class='btn btn-red' type='submit' value='Eliminar del curso'>";
      html += "</form>";
      html += "</td></tr>";
    }
    html += "</table>";

    html += "<script>"
            "function applyStudentFilters(){ "
            "const table=document.getElementById('students_mat_table'); if(!table) return; "
            "const f1=document.getElementById('sf_name').value.trim().toLowerCase(); "
            "const f2=document.getElementById('sf_acc').value.trim().toLowerCase(); "
            "for(let r=1;r<table.rows.length;r++){ "
            "const row=table.rows[r]; if(row.cells.length<3) continue; "
            "const name=row.cells[0].textContent.toLowerCase(); "
            "const acc=row.cells[1].textContent.toLowerCase(); "
            "const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1); "
            "row.style.display = ok ? '' : 'none'; "
            "} }"
            "function clearStudentFilters(){ document.getElementById('sf_name').value=''; document.getElementById('sf_acc').value=''; applyStudentFilters(); }"
            "</script>";
  }

  String backTarget = return_to.length() ? return_to : String("/materias");
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='" + backTarget + "'>Volver</a> <a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();

  server.send(200, "text/html", html);
}

// ------------------------------------------------------------
// GET /students_all
// ------------------------------------------------------------
void handleStudentsAll() {
  String searchUid = server.hasArg("search_uid") ? server.arg("search_uid") : String();

  String html = htmlHeader("Alumnos - Todos");
  html += "<div class='card'><h2>Todos los alumnos</h2>";

  html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px;gap:8px;'>";
  html += "<a class='btn btn-blue' href='/capture_individual?return_to=/students_all&target=students'>Capturar individual</a>";
  html += "<a class='btn btn-blue' href='/capture_batch?return_to=/students_all'>Capturar lote</a>";
  html += "</div>";

  html += "<div class='filters'>"
          "<input id='sa_name' placeholder='Filtrar Nombre'>"
          "<input id='sa_acc' placeholder='Filtrar Cuenta'>"
          "<input id='sa_mat' placeholder='Filtrar Materia'>"
          "<button class='search-btn btn btn-blue' onclick='applyAllStudentFilters()'>Buscar</button>"
          "<button class='search-btn btn btn-green' onclick='clearAllStudentFilters()'>Limpiar</button>"
          "</div>";

  std::vector<OracleAlumnoRec> rows;
  if (!loadOracleAlumnos(rows)) {
    html += "<p>No se pudieron cargar los alumnos desde el servidor.</p>";
    html += htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // Agrupar por UID
  struct GroupRec {
    String uid;
    String name;
    String account;
    String created;
    std::vector<String> mats;
  };

  std::vector<GroupRec> groups;
  for (auto &r : rows) {
    int idx = -1;
    for (int i = 0; i < (int)groups.size(); ++i) {
      if (groups[i].uid == r.uid) {
        idx = i;
        break;
      }
    }

    String mat = baseMateriaFromStoredField(r.materia);

    if (idx == -1) {
      GroupRec g;
      g.uid = r.uid;
      g.name = r.name;
      g.account = r.account;
      g.created = r.created_at.length() ? r.created_at : nowISO();
      if (mat.length()) g.mats.push_back(mat);
      groups.push_back(g);
    } else {
      if (mat.length()) {
        bool found = false;
        for (auto &x : groups[idx].mats) {
          if (x == mat) { found = true; break; }
        }
        if (!found) groups[idx].mats.push_back(mat);
      }
    }
  }

  if (searchUid.length()) {
    bool foundAny = false;
    for (auto &g : groups) {
      if (g.uid == searchUid) {
        html += "<table id='students_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
        String mats = "";
        for (size_t j = 0; j < g.mats.size(); ++j) {
          if (j) mats += "; ";
          mats += g.mats[j];
        }
        if (mats.length() == 0) mats = "-";

        html += "<tr><td>" + htmlEscape(g.name) + "</td><td>" + htmlEscape(g.account) + "</td><td>" + htmlEscape(mats) + "</td><td>" + htmlEscape(g.created) + "</td><td>";
        html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncode(g.uid) + "&return_to=" + urlEncode(String("/students_all")) + "'>✏️ Editar</a> ";
        html += "<form method='POST' action='/student_delete' style='display:inline' onsubmit='return confirm(\"Eliminar totalmente este alumno?\");'>";
        html += "<input type='hidden' name='uid' value='" + htmlEscape(g.uid) + "'>";
        html += "<input class='btn btn-red' type='submit' value='Eliminar totalmente'>";
        html += "</form>";
        html += "</td></tr></table>";
        foundAny = true;
        break;
      }
    }
    if (!foundAny) {
      html += "<p>No se encontró alumno con UID " + htmlEscape(searchUid) + ".</p>";
    }
  } else {
    if (groups.size() == 0) {
      html += "<p>No hay alumnos registrados.</p>";
    } else {
      html += "<table id='students_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
      for (auto &g : groups) {
        String mats = "";
        for (size_t j = 0; j < g.mats.size(); ++j) {
          if (j) mats += "; ";
          mats += g.mats[j];
        }
        if (mats.length() == 0) mats = "-";

        html += "<tr><td>" + htmlEscape(g.name) + "</td><td>" + htmlEscape(g.account) + "</td><td>" + htmlEscape(mats) + "</td><td>" + htmlEscape(g.created) + "</td><td>";
        html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncode(g.uid) + "&return_to=" + urlEncode(String("/students_all")) + "'>✏️ Editar</a> ";
        html += "<form method='POST' action='/student_delete' style='display:inline' onsubmit='return confirm(\"Eliminar totalmente este alumno?\");'>";
        html += "<input type='hidden' name='uid' value='" + htmlEscape(g.uid) + "'>";
        html += "<input class='btn btn-red' type='submit' value='Eliminar totalmente'>";
        html += "</form>";
        html += "</td></tr>";
      }
      html += "</table>";

      html += "<script>"
              "function applyAllStudentFilters(){ "
              "const table=document.getElementById('students_all_table'); if(!table) return; "
              "const f1=document.getElementById('sa_name').value.trim().toLowerCase(); "
              "const f2=document.getElementById('sa_acc').value.trim().toLowerCase(); "
              "const f3=document.getElementById('sa_mat').value.trim().toLowerCase(); "
              "for(let r=1;r<table.rows.length;r++){ "
              "const row=table.rows[r]; if(row.cells.length<4) continue; "
              "const name=row.cells[0].textContent.toLowerCase(); "
              "const acc=row.cells[1].textContent.toLowerCase(); "
              "const mats=row.cells[2].textContent.toLowerCase(); "
              "const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1)&&(mats.indexOf(f3)!==-1); "
              "row.style.display = ok ? '' : 'none'; "
              "} }"
              "function clearAllStudentFilters(){ document.getElementById('sa_name').value=''; document.getElementById('sa_acc').value=''; document.getElementById('sa_mat').value=''; applyAllStudentFilters(); }"
              "</script>";
    }
  }

  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

// ------------------------------------------------------------
// POST /student_remove_course
// ------------------------------------------------------------
void handleStudentRemoveCourse() {
  if (!server.hasArg("uid") || !server.hasArg("materia")) {
    server.send(400, "text/plain", "faltan");
    return;
  }

  String uid = server.arg("uid");
  String materia = server.arg("materia");
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : String();
  bool hideCapture = server.hasArg("hide_capture") && server.arg("hide_capture") == "1";

  std::vector<OracleAlumnoRec> rows;
  if (!loadOracleAlumnos(rows)) {
    server.send(500, "text/plain", "no se pudieron leer alumnos");
    return;
  }

  String removedName = "";
  String removedAcc = "";
  String removedCreated = "";

  // Buscar una fila exacta para usar como fallback
  for (auto &r : rows) {
    if (r.uid == uid && baseMateriaFromStoredField(r.materia) == trimCopy(materia)) {
      if (removedName.length() == 0) {
        removedName = r.name;
        removedAcc = r.account;
        removedCreated = r.created_at;
      }
    }
  }

  if (!oracleRemoveStudentCourse(uid, materia, removedName, removedAcc, removedCreated)) {
    Serial.println("WARN: no se pudo sincronizar la eliminación de materia del alumno en Oracle");
  }

  String redirect = String("/students?materia=") + urlEncode(materia);
  if (profesor.length()) redirect += "&profesor=" + urlEncode(profesor);
  if (return_to.length()) redirect += "&return_to=" + urlEncode(return_to);
  if (hideCapture) redirect += "&hide_capture=1";

  server.sendHeader("Location", redirect);
  server.send(303, "text/plain", "Removed");
}

// ------------------------------------------------------------
// POST /student_delete
// ------------------------------------------------------------
void handleStudentDelete() {
  if (!server.hasArg("uid")) {
    server.send(400, "text/plain", "faltan");
    return;
  }

  String uid = server.arg("uid");

  if (!oracleDeleteStudentRowsByUID(uid)) {
    Serial.println("WARN: no se pudo borrar completamente el alumno en Oracle");
  }

  server.sendHeader("Location", "/students_all");
  server.send(303, "text/plain", "Deleted");
}