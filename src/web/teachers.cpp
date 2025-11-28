// src/web/teachers.cpp
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>
#include <vector>

#include "teachers.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"

// Pequeña función de escape HTML
static String htmlEscape(const String &s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

// simple url encode (reutilizable)
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

// -------------------------------
// Small helpers and types
// -------------------------------
struct MetaRec {
  String uid;
  String name;
  String acc;
  String created;
};

static std::vector<String> profsFromCoursesForMateria(const String &materia) {
  std::vector<String> out;
  auto courses = loadCourses();
  for (auto &c : courses) {
    if (c.materia == materia) {
      bool found = false;
      for (auto &p : out) if (p == c.profesor) { found = true; break; }
      if (!found) out.push_back(c.profesor);
    }
  }
  return out;
}

// Nota: usamos teachersForMateriaFile(...) DECLARADA en files_utils.h y DEFINIDA en files_utils.cpp
static std::vector<String> getProfessorsForMateriaCombined(const String &materia) {
  std::vector<String> out;
  auto fromCourses = profsFromCoursesForMateria(materia);
  for (auto &p : fromCourses) {
    bool f = false;
    for (auto &x : out) if (x == p) { f = true; break; }
    if (!f) out.push_back(p);
  }
  auto fromFile = teachersForMateriaFile(materia); // usa la función centralizada
  for (auto &ln : fromFile) {
    auto c = parseQuotedCSVLine(ln);
    if (c.size() >= 2) {
      String name = c[1];
      bool f = false;
      for (auto &x : out) if (x == name) { f = true; break; }
      if (!f) out.push_back(name);
    }
  }
  return out;
}

// build list of MetaRec from TEACHERS_FILE
static std::vector<MetaRec> buildTeacherMetaList() {
  std::vector<MetaRec> out;
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (!f) return out;
  String header = f.readStringUntil('\n'); (void)header;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 2) {
      MetaRec r;
      r.uid = (c.size() > 0 ? c[0] : "");
      r.name = (c.size() > 1 ? c[1] : "");
      r.acc = (c.size() > 2 ? c[2] : "-");
      r.created = (c.size() > 4 ? c[4] : nowISO());
      out.push_back(r);
    }
  }
  f.close();
  return out;
}

static bool findMetaByName(const std::vector<MetaRec> &meta, const String &name, MetaRec &out) {
  for (auto &m : meta) {
    if (m.name == name) { out = m; return true; }
  }
  return false;
}

// -------------------------------
// Handlers
// -------------------------------

void handleTeachersForMateria() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  String html = htmlHeader(("Maestros - " + materia).c_str());
  html += "<div class='card'><h2>Maestros - " + materia + "</h2>";

  String rt = String("/teachers?materia=") + urlEncodeLocal(materia);
  html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px;gap:8px;'>";
  html += "<a class='btn btn-blue' href='/capture_individual?return_to=" + urlEncodeLocal(rt) + "&target=teachers'>Capturar Maestro</a>";
  html += "</div>";

  html += "<div class='filters'><input id='tf_name' placeholder='Filtrar Nombre'><input id='tf_acc' placeholder='Filtrar Cuenta'><button class='search-btn btn btn-blue' onclick='applyTeacherFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearTeacherFilters()'>Limpiar</button></div>";

  auto profs = getProfessorsForMateriaCombined(materia);
  auto meta = buildTeacherMetaList();

  if (profs.size() == 0) {
    html += "<p>No hay maestros registrados para esta materia.</p>";
  } else {
    html += "<table id='teachers_mat_table'><tr><th>Nombre</th><th>Cuenta</th><th>Registro</th><th>Acciones</th></tr>";
    for (auto &name : profs) {
      MetaRec mr; String uid=""; String acc="-"; String created = nowISO();
      if (findMetaByName(meta, name, mr)) {
        uid = mr.uid; acc = mr.acc; created = mr.created;
      }
      html += "<tr><td>" + name + "</td><td>" + acc + "</td><td>" + created + "</td>";
      html += "<td>";
      if (uid.length()) {
        html += "<a class='btn btn-green' href='/capture_edit?uid=" + uid + "&return_to=" + urlEncodeLocal(rt) + "'>✏️ Editar</a> ";
      } else {
        html += "<a class='btn btn-blue' href='/capture_individual?return_to=" + urlEncodeLocal(rt) + "&target=teachers'>Capturar Maestro</a> ";
      }
      html += "<form method='POST' action='/teacher_remove_course' style='display:inline;margin-left:6px;' onsubmit='return confirm(\"Eliminar este maestro de la materia?\");'>";
      html += "<input type='hidden' name='uid' value='" + uid + "'>";
      html += "<input type='hidden' name='materia' value='" + materia + "'>";
      html += "<input class='btn btn-red' type='submit' value='Eliminar del curso'>";
      html += "</form>";
      html += "</td></tr>";
    }
    html += "</table>";

    html += "<script>"
            "function applyTeacherFilters(){ const table=document.getElementById('teachers_mat_table'); if(!table) return; const f1=document.getElementById('tf_name').value.trim().toLowerCase(); const f2=document.getElementById('tf_acc').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; if(row.cells.length<3) continue; const name=row.cells[0].textContent.toLowerCase(); const acc=row.cells[1].textContent.toLowerCase(); const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1); row.style.display = ok ? '' : 'none'; } }"
            "function clearTeacherFilters(){ document.getElementById('tf_name').value=''; document.getElementById('tf_acc').value=''; applyTeacherFilters(); }"
            "</script>";
  }

  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleTeachersAll() {
  String searchUid = server.hasArg("search_uid") ? server.arg("search_uid") : String();

  String html = htmlHeader("Maestros - Todos");
  html += "<div class='card'><h2>Todos los maestros</h2>";

  html += "<div style='display:flex;justify-content:flex-end;margin-bottom:8px;gap:8px;'>";
  html += "<a class='btn btn-blue' href='/capture_individual?return_to=/teachers_all&target=teachers'>Capturar Maestro</a>";
  html += "</div>";

  html += "<div class='filters'><input id='ta_name' placeholder='Filtrar Nombre'><input id='ta_acc' placeholder='Filtrar Cuenta'><input id='ta_mat' placeholder='Filtrar Materia'><button class='search-btn btn btn-blue' onclick='applyAllTeacherFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearAllTeacherFilters()'>Limpiar</button></div>";

  // Build list combining TEACHERS_FILE rows and courses
  std::vector<MetaRec> recs = buildTeacherMetaList();

  // Ensure any professors in courses that are not in recs get added
  auto courses = loadCourses();
  for (auto &c : courses) {
    bool found = false;
    for (auto &r : recs) {
      if (r.name == c.profesor) {
        found = true; break;
      }
    }
    if (!found) {
      MetaRec r; r.uid = ""; r.name = c.profesor; r.acc = "-"; r.created = nowISO();
      recs.push_back(r);
    }
  }

  if (recs.size() == 0) {
    html += "<p>No hay maestros registrados.</p>";
  } else {
    if (searchUid.length()) {
      bool foundAny = false;
      for (auto &r : recs) {
        if (r.uid == searchUid) {
          foundAny = true;
          // compute materias for this teacher
          std::vector<String> mats;
          File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
          if (f) {
            String header = f.readStringUntil('\n'); (void)header;
            while (f.available()) {
              String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
              auto c = parseQuotedCSVLine(l);
              if (c.size() >= 4) {
                String rowUid = c[0];
                String name = (c.size() > 1 ? c[1] : "");
                String mat  = (c.size() > 3 ? c[3] : "");
                if (rowUid == r.uid || name == r.name) {
                  if (mat.length()) {
                    bool fnd = false;
                    for (auto &m : mats) if (m == mat) { fnd = true; break; }
                    if (!fnd) mats.push_back(mat);
                  }
                }
              }
            }
            f.close();
          }
          for (auto &c : courses) if (c.profesor == r.name) {
            bool fnd = false;
            for (auto &m : mats) if (m == c.materia) { fnd = true; break; }
            if (!fnd) mats.push_back(c.materia);
          }

          String matsStr = "-";
          if (mats.size()) { matsStr = ""; for (size_t i=0;i<mats.size();++i){ if (i) matsStr += "; "; matsStr += mats[i]; } }

          html += "<table id='teachers_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
          html += "<tr><td>" + r.name + "</td><td>" + r.acc + "</td><td>" + matsStr + "</td><td>" + r.created + "</td><td>";
          if (r.uid.length()) {
            html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncodeLocal(r.uid) + "&return_to=" + urlEncodeLocal(String("/teachers_all")) + "'>✏️ Editar</a> ";
            html += "<form method='POST' action='/teacher_delete' style='display:inline;margin-left:6px;' onsubmit='return confirm(\"Eliminar totalmente este maestro? Esto puede eliminar materias y horarios asociados.\");'>";
            html += "<input type='hidden' name='uid' value='" + r.uid + "'>";
            html += "<input class='btn btn-red' type='submit' value='Eliminar totalmente'>";
            html += "</form>";
          } else {
            html += "<a class='btn btn-blue' href='/capture_individual?return_to=/teachers_all&target=teachers'>Capturar Maestro</a>";
          }
          html += "</td></tr></table>";
          break;
        }
      }
      if (!foundAny) html += "<p>No se encontró maestro con UID " + htmlEscape(searchUid) + ".</p>";
    } else {
      html += "<table id='teachers_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";

      // For each rec, compute materias: from TEACHERS_FILE and from courses where name==profesor
      for (auto &r : recs) {
        // collect materias
        std::vector<String> mats;
        // from TEACHERS_FILE rows
        File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
        if (f) {
          String header = f.readStringUntil('\n'); (void)header;
          while (f.available()) {
            String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
            auto c = parseQuotedCSVLine(l);
            if (c.size() >= 4) {
              String uid = c[0];
              String name = (c.size() > 1 ? c[1] : "");
              String mat  = (c.size() > 3 ? c[3] : "");
              // match by uid if available, otherwise by name
              if ((r.uid.length() && uid == r.uid) || (r.uid.length()==0 && name == r.name)) {
                if (mat.length()) {
                  bool fnd = false;
                  for (auto &m : mats) if (m == mat) { fnd = true; break; }
                  if (!fnd) mats.push_back(mat);
                }
              }
            }
          }
          f.close();
        }
        // also add materias from courses where profesor == name
        for (auto &c : courses) if (c.profesor == r.name) {
          bool fnd = false;
          for (auto &m : mats) if (m == c.materia) { fnd = true; break; }
          if (!fnd) mats.push_back(c.materia);
        }

        String matsStr = "-";
        if (mats.size()) {
          matsStr = "";
          for (size_t i=0;i<mats.size();++i) {
            if (i) matsStr += "; ";
            matsStr += mats[i];
          }
        }

        html += "<tr><td>" + r.name + "</td><td>" + r.acc + "</td><td>" + matsStr + "</td><td>" + r.created + "</td><td>";
        if (r.uid.length()) {
          html += "<a class='btn btn-green' href='/capture_edit?uid=" + urlEncodeLocal(r.uid) + "&return_to=" + urlEncodeLocal(String("/teachers_all")) + "'>✏️ Editar</a> ";
          html += "<form method='POST' action='/teacher_delete' style='display:inline;margin-left:6px;' onsubmit='return confirm(\"Eliminar totalmente este maestro? Esto puede eliminar materias y horarios asociados.\");'>";
          html += "<input type='hidden' name='uid' value='" + r.uid + "'>";
          html += "<input class='btn btn-red' type='submit' value='Eliminar totalmente'>";
          html += "</form>";
        } else {
          html += "<a class='btn btn-blue' href='/capture_individual?return_to=/teachers_all&target=teachers'>Capturar Maestro</a>";
        }
        html += "</td></tr>";
      }
      html += "</table>";

      html += "<script>"
              "function applyAllTeacherFilters(){ const table=document.getElementById('teachers_all_table'); if(!table) return; const f1=document.getElementById('ta_name').value.trim().toLowerCase(); const f2=document.getElementById('ta_acc').value.trim().toLowerCase(); const f3=document.getElementById('ta_mat').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; if(row.cells.length<4) continue; const name=row.cells[0].textContent.toLowerCase(); const acc=row.cells[1].textContent.toLowerCase(); const mats=row.cells[2].textContent.toLowerCase(); const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1)&&(mats.indexOf(f3)!==-1); row.style.display = ok ? '' : 'none'; } }"
              "function clearAllTeacherFilters(){ document.getElementById('ta_name').value=''; document.getElementById('ta_acc').value=''; document.getElementById('ta_mat').value=''; applyAllTeacherFilters(); }"
              "</script>";
    }
  }

  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleTeacherRemoveCourse() {
  if (!server.hasArg("uid") || !server.hasArg("materia")) { server.send(400,"text/plain","faltan"); return; }
  String uid = server.arg("uid"); String materia = server.arg("materia");
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; String header = f.readStringUntil('\n'); lines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4) {
      String rowUid = c[0];
      String rowMat = c[3];
      if (rowUid == uid && rowMat == materia) continue;
    }
    lines.push_back(l);
  }
  f.close();
  writeAllLines(TEACHERS_FILE, lines);
  server.sendHeader("Location","/teachers?materia=" + urlEncodeLocal(materia));
  server.send(303,"text/plain","Removed");
}

void handleTeacherDelete() {
  if (!server.hasArg("uid")) { server.send(400,"text/plain","faltan"); return; }
  String uid = server.arg("uid");

  // 1) Find teacher name (if any) and collect materias taught by that teacher (from courses)
  String teacherName = "";
  File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (ft) {
    String header = ft.readStringUntil('\n'); (void)header;
    while (ft.available()) {
      String l = ft.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 2 && c[0] == uid) {
        teacherName = c[1];
        break;
      }
    }
    ft.close();
  }

  // Build list of materias that will be removed because professor==teacherName
  std::vector<Course> courses = loadCourses();
  std::vector<String> materiasToRemove;
  for (auto &c : courses) {
    if (teacherName.length() && c.profesor == teacherName) {
      bool fnd = false;
      for (auto &m : materiasToRemove) if (m == c.materia) { fnd = true; break; }
      if (!fnd) materiasToRemove.push_back(c.materia);
    }
  }

  // 2) Remove TEACHERS_FILE rows with that uid
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  std::vector<String> newTeacherLines;
  if (f) {
    String header = f.readStringUntil('\n'); newTeacherLines.push_back(header);
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 1) {
        String rowUid = c[0];
        if (rowUid == uid) continue; // skip this teacher
      }
      newTeacherLines.push_back(l);
    }
    f.close();
    writeAllLines(TEACHERS_FILE, newTeacherLines);
  }

  // 3) Remove courses where profesor == teacherName
  if (teacherName.length()) {
    std::vector<Course> newCourses;
    for (auto &c : courses) {
      if (c.profesor == teacherName) continue;
      newCourses.push_back(c);
    }
    writeCourses(newCourses);
  }

  // 4) Clean SCHEDULES_FILE: remove schedules that belong to removed course keys or materias that no longer exist
  File fs = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines;
  if (fs) {
    String header = fs.readStringUntil('\n'); slines.push_back(header);
    while (fs.available()) {
      String l = fs.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4) {
        String owner = c[0];
        bool skip = false;
        for (auto &m : materiasToRemove) {
          String key = m + String("||") + teacherName;
          if (owner == key) { skip = true; break; }
        }
        if (skip) continue;
        for (auto &m : materiasToRemove) {
          bool still = false;
          auto remCourses = loadCourses();
          for (auto &rc : remCourses) if (rc.materia == m) { still = true; break; }
          if (!still && owner == m) { skip = true; break; }
        }
        if (skip) continue;
      }
      slines.push_back(l);
    }
    fs.close();
    writeAllLines(SCHEDULES_FILE, slines);
  }

  // 5) Clean USERS_FILE: if a materia got fully removed (no courses remain for it), remove users with that materia
  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<String> ulines;
  if (fu) {
    String uheader = fu.readStringUntil('\n'); ulines.push_back(uheader);
    while (fu.available()) {
      String l = fu.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4) {
        String uid_u = c[0], name = c[1], acc = c[2], mm = c[3];
        bool removeUser = false;
        for (auto &m : materiasToRemove) {
          if (mm == m) {
            bool still = false;
            auto remCourses = loadCourses();
            for (auto &rc : remCourses) if (rc.materia == m) { still = true; break; }
            if (!still) removeUser = true;
            break;
          }
        }
        if (removeUser) {
          addNotification(uid_u, name, acc, String("Cuenta eliminada: materia removida al borrar maestro ") + teacherName);
          continue; // skip this user row
        }
        ulines.push_back("\"" + uid_u + "\"," + "\"" + name + "\"," + "\"" + acc + "\"," + "\"" + mm + "\"," + "\"" + (c.size()>4?c[4]:"") + "\"");
      } else ulines.push_back(l);
    }
    fu.close();
    writeAllLines(USERS_FILE, ulines);
  }

  server.sendHeader("Location","/teachers_all");
  server.send(303,"text/plain","Deleted");
}
