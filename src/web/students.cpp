// src/web/students.cpp
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>
#include <vector>

#include "students.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include "db_sync.h"
#include <ArduinoJson.h>

// Pequeña función de escape HTML usada localmente
static String htmlEscape(const String &s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

// URL-encode simple
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
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
      ret += buf;
    }
  }
  return ret;
}

// ============================================================
// Helpers Oracle
// ============================================================

struct OracleAlumnoRec {
  int id = -1;
  String uid;
  String name;
  String account;
  String materia;
  String created_at;
};

static bool loadOracleAlumnos(std::vector<OracleAlumnoRec> &out) {
  out.clear();

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

  if (!doc.is<JsonArray>()) {
    Serial.println("WARN: /alumnos no devolvió un arreglo JSON");
    return false;
  }

  for (JsonObject obj : doc.as<JsonArray>()) {
    OracleAlumnoRec r;
    r.id = obj["id"] | -1;
    r.uid = obj["rfid_uid"] | "";
    r.name = obj["name"] | "";
    r.account = obj["account"] | "";
    r.materia = obj["materia"] | "";
    r.created_at = obj["created_at"] | "";
    out.push_back(r);
  }

  return true;
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

  if (matches.size() == 0) {
    Serial.println("DB_SYNC: no se encontró el UID en Oracle para quitar materia");
    return true;
  }

  OracleAlumnoRec exact;
  bool hasExact = false;
  for (auto &r : matches) {
    if (r.materia == materia) {
      exact = r;
      hasExact = true;
      break;
    }
  }

  // Si hay más de una fila para ese UID, eliminamos solo la materia correspondiente.
  // Si solo queda una, la convertimos en registro sin materia (como hace SPIFFS).
  if (matches.size() > 1) {
    if (!hasExact) {
      Serial.println("WARN: no se encontró coincidencia exacta de materia en Oracle; borrando primera fila del UID");
      OracleAlumnoRec target = matches[0];
      if (target.id > 0) {
        if (!deleteAlumnoById(target.id)) {
          Serial.println("WARN: no se pudo borrar la fila de Oracle");
          return false;
        }
      }
      return true;
    }

    if (exact.id > 0) {
      if (!deleteAlumnoById(exact.id)) {
        Serial.println("WARN: no se pudo borrar la materia del alumno en Oracle");
        return false;
      }
      Serial.println("DB_SYNC: materia eliminada del alumno en Oracle");
    }
    return true;
  }

  // Si solo hay una fila para ese UID, la quitamos y la reinsertamos vacía de materia
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

  // Reinsertar sin materia
  if (!sendAlumnoRegistro(uid, name, account, String(), created)) {
    Serial.println("WARN: no se pudo reinsertar alumno en Oracle sin materia");
    return false;
  }

  Serial.println("DB_SYNC: alumno reinsertado en Oracle sin materia");
  return true;
}

// ============================================================
// GET /students?materia=...
// ============================================================
void handleStudentsForMateria() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : String();
  bool hideCaptureButtons = server.hasArg("hide_capture") && server.arg("hide_capture") == "1";

  String html = htmlHeader(("Alumnos - " + materia).c_str());
  html += "<div class='card'><h2>Alumnos - " + materia + "</h2>";

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

  html += "<div class='filters'><input id='sf_name' placeholder='Filtrar Nombre'><input id='sf_acc' placeholder='Filtrar Cuenta'><button class='search-btn btn btn-blue' onclick='applyStudentFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearStudentFilters()'>Limpiar</button></div>";

  auto users = usersForMateria(materia);
  if (users.size() == 0) {
    html += "<p>No hay alumnos registrados para esta materia.</p>";
  } else {
    html += "<table id='students_mat_table'><tr><th>Nombre</th><th>Cuenta</th><th>Registro</th><th>Acciones</th></tr>";
    for (auto &ln : users) {
      auto c = parseQuotedCSVLine(ln);
      String uid = (c.size() > 0 ? c[0] : "");
      String name = (c.size() > 1 ? c[1] : "");
      String acc = (c.size() > 2 ? c[2] : "");
      String created = (c.size() > 4 ? c[4] : nowISO());

      html += "<tr><td>" + name + "</td><td>" + acc + "</td><td>" + created + "</td>";

      html += "<td>";
      html += "<form method='POST' action='/student_remove_course' style='display:inline' onsubmit='return confirm(\"Eliminar este alumno de la materia?\");'>";
      html += "<input type='hidden' name='uid' value='" + uid + "'>";
      html += "<input type='hidden' name='materia' value='" + materia + "'>";
      if (profesor.length()) html += "<input type='hidden' name='profesor' value='" + profesor + "'>";
      if (return_to.length()) html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
      html += "<input type='hidden' name='hide_capture' value='" + String(hideCaptureButtons ? "1" : "0") + "'>";
      html += "<input class='btn btn-red' type='submit' value='Eliminar del curso'>";
      html += "</form>";
      html += "</td></tr>";
    }
    html += "</table>";

    html += "<script>"
            "function applyStudentFilters(){ const table=document.getElementById('students_mat_table'); if(!table) return; const f1=document.getElementById('sf_name').value.trim().toLowerCase(); const f2=document.getElementById('sf_acc').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; if(row.cells.length<3) continue; const name=row.cells[0].textContent.toLowerCase(); const acc=row.cells[1].textContent.toLowerCase(); const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1); row.style.display = ok ? '' : 'none'; } }"
            "function clearStudentFilters(){ document.getElementById('sf_name').value=''; document.getElementById('sf_acc').value=''; applyStudentFilters(); }"
            "</script>";
  }

  String backTarget = return_to.length() ? return_to : String("/materias");
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='" + backTarget + "'>Volver</a> <a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

// ============================================================
// GET /students_all
// ============================================================
void handleStudentsAll() {
  String searchUid = server.hasArg("search_uid") ? server.arg("search_uid") : String();

  String html = htmlHeader("Alumnos - Todos");
  html += "<div class='card'><h2>Todos los alumnos</h2>";

  html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px;gap:8px;'>";
  html += "<a class='btn btn-blue' href='/capture_individual?return_to=/students_all&target=students'>Capturar individual</a>";
  html += "<a class='btn btn-blue' href='/capture_batch?return_to=/students_all'>Capturar lote</a>";
  html += "</div>";

  html += "<div class='filters'><input id='sa_name' placeholder='Filtrar Nombre'><input id='sa_acc' placeholder='Filtrar Cuenta'><input id='sa_mat' placeholder='Filtrar Materia'><button class='search-btn btn btn-blue' onclick='applyAllStudentFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearAllStudentFilters()'>Limpiar</button></div>";

  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { html += "<p>No hay archivo de usuarios.</p>"; html += htmlFooter(); server.send(200,"text/html",html); return; }

  String header = f.readStringUntil('\n');
  struct SRec { String name; String acc; std::vector<String> mats; String created; String uid; };
  std::vector<String> uids;
  std::vector<SRec> recs;

  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 3) {
      String uid = c[0];
      String name = c[1];
      String acc = c[2];
      String mat = (c.size() > 3 ? c[3] : "");
      String created = (c.size() > 4 ? c[4] : nowISO());

      int idx = -1;
      for (int i = 0; i < (int)uids.size(); i++) if (uids[i] == uid) { idx = i; break; }

      if (idx == -1) {
        uids.push_back(uid);
        SRec r;
        r.name = name;
        r.acc = acc;
        r.created = created;
        r.uid = uid;
        if (mat.length()) r.mats.push_back(mat);
        recs.push_back(r);
      } else {
        if (mat.length()) recs[idx].mats.push_back(mat);
      }
    }
  }
  f.close();

  if (searchUid.length()) {
    bool foundAny = false;
    for (size_t i = 0; i < recs.size(); ++i) {
      if (recs[i].uid == searchUid) {
        SRec &r = recs[i];
        html += "<table id='students_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
        String mats = "";
        for (size_t j = 0; j < r.mats.size(); ++j) { if (j) mats += "; "; mats += r.mats[j]; }
        if (mats.length() == 0) mats = "-";
        html += "<tr><td>" + r.name + "</td><td>" + r.acc + "</td><td>" + mats + "</td><td>" + r.created + "</td><td>";
        html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncode(r.uid) + "&return_to=" + urlEncode(String("/students_all")) + "'>✏️ Editar</a> ";
        html += "<form method='POST' action='/student_delete' style='display:inline' onsubmit='return confirm(\"Eliminar totalmente este alumno?\");'>";
        html += "<input type='hidden' name='uid' value='" + r.uid + "'>";
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
    if (uids.size() == 0) html += "<p>No hay alumnos registrados.</p>";
    else {
      html += "<table id='students_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
      for (int i = 0; i < (int)uids.size(); i++) {
        SRec &r = recs[i];
        String mats = "";
        for (int j = 0; j < (int)r.mats.size(); j++) { if (j) mats += "; "; mats += r.mats[j]; }
        if (mats.length() == 0) mats = "-";
        html += "<tr><td>" + r.name + "</td><td>" + r.acc + "</td><td>" + mats + "</td><td>" + r.created + "</td><td>";

        html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncode(r.uid) + "&return_to=" + urlEncode(String("/students_all")) + "'>✏️ Editar</a> ";

        html += "<form method='POST' action='/student_delete' style='display:inline' onsubmit='return confirm(\"Eliminar totalmente este alumno?\");'>";
        html += "<input type='hidden' name='uid' value='" + uids[i] + "'>";
        html += "<input class='btn btn-red' type='submit' value='Eliminar totalmente'>";
        html += "</form>";

        html += "</td></tr>";
      }
      html += "</table>";

      html += "<script>"
              "function applyAllStudentFilters(){ const table=document.getElementById('students_all_table'); if(!table) return; const f1=document.getElementById('sa_name').value.trim().toLowerCase(); const f2=document.getElementById('sa_acc').value.trim().toLowerCase(); const f3=document.getElementById('sa_mat').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; if(row.cells.length<4) continue; const name=row.cells[0].textContent.toLowerCase(); const acc=row.cells[1].textContent.toLowerCase(); const mats=row.cells[2].textContent.toLowerCase(); const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1)&&(mats.indexOf(f3)!==-1); row.style.display = ok ? '' : 'none'; } }"
              "function clearAllStudentFilters(){ document.getElementById('sa_name').value=''; document.getElementById('sa_acc').value=''; document.getElementById('sa_mat').value=''; applyAllStudentFilters(); }"
              "</script>";
    }
  }

  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

// ============================================================
// POST /student_remove_course
// ============================================================
void handleStudentRemoveCourse() {
  if (!server.hasArg("uid") || !server.hasArg("materia")) { server.send(400,"text/plain","faltan"); return; }

  String uid = server.arg("uid");
  String materia = server.arg("materia");
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : String();
  bool hideCapture = server.hasArg("hide_capture") && server.arg("hide_capture") == "1";

  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }

  String header = f.readStringUntil('\n');
  std::vector<String> origLines;
  String removedName = "";
  String removedAcc = "";
  String removedCreated = "";

  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;

    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 4 && c[0] == uid) {
      String localMat = c[3];
      String match1 = materia;
      String match2 = "";
      if (profesor.length()) match2 = materia + String("||") + profesor;

      bool isTarget = (localMat == match1) || (match2.length() && localMat == match2);
      if (isTarget && removedName.length() == 0) {
        removedName = (c.size() > 1 ? c[1] : "");
        removedAcc = (c.size() > 2 ? c[2] : "");
        removedCreated = (c.size() > 4 ? c[4] : nowISO());
      }
    }

    origLines.push_back(l);
  }
  f.close();

  int uidCount = 0;
  for (auto &l : origLines) {
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 1 && c[0] == uid) uidCount++;
  }

  String match1 = materia;
  String match2 = "";
  if (profesor.length()) match2 = materia + String("||") + profesor;

  std::vector<String> outLines;
  outLines.push_back(header);

  for (auto &l : origLines) {
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 4 && c[0] == uid && (c[3] == match1 || (match2.length() && c[3] == match2))) {
      if (uidCount > 1) {
        continue;
      } else {
        String created = (c.size() > 4 ? c[4] : "");
        outLines.push_back("\"" + c[0] + "\"," + "\"" + c[1] + "\"," + "\"" + c[2] + "\"," + "\"\"" + "," + "\"" + created + "\"");
        continue;
      }
    }
    outLines.push_back(l);
  }

  writeAllLines(USERS_FILE, outLines);

  // Sincronización Oracle
  if (!oracleRemoveStudentCourse(uid, materia, removedName, removedAcc, removedCreated)) {
    Serial.println("WARN: no se pudo sincronizar la eliminación de materia del alumno en Oracle");
  }

  String redirect = String("/students?materia=") + urlEncode(materia);
  if (profesor.length()) redirect += "&profesor=" + urlEncode(profesor);
  if (return_to.length()) redirect += "&return_to=" + urlEncode(return_to);
  if (hideCapture) redirect += "&hide_capture=1";

  server.sendHeader("Location", redirect);
  server.send(303,"text/plain","Removed");
}

// ============================================================
// POST /student_delete
// ============================================================
void handleStudentDelete() {
  if (!server.hasArg("uid")) { server.send(400,"text/plain","faltan"); return; }

  String uid = server.arg("uid");
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }

  std::vector<String> lines;
  String header = f.readStringUntil('\n');
  lines.push_back(header);

  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 1 && c[0] == uid) continue;
    lines.push_back(l);
  }
  f.close();

  writeAllLines(USERS_FILE, lines);

  // Sincronización Oracle
  if (!oracleDeleteStudentRowsByUID(uid)) {
    Serial.println("WARN: no se pudo borrar completamente el alumno en Oracle");
  }

  server.sendHeader("Location","/students_all");
  server.send(303,"text/plain","Deleted");
}