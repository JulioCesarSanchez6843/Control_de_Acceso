#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "students.h"
#include "globals.h"
#include "web_common.h"

// Mostrar alumnos por materia (solo ver y eliminar del curso)
void handleStudentsForMateria() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  String html = htmlHeader(("Alumnos - " + materia).c_str());
  html += "<div class='card'><h2>Alumnos - " + materia + "</h2>";

  // filtros encima de la tabla (eliminado UID)
  html += "<div class='filters'><input id='sf_name' placeholder='Filtrar Nombre'><input id='sf_acc' placeholder='Filtrar Cuenta'><button class='search-btn btn btn-blue' onclick='applyStudentFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearStudentFilters()'>Limpiar</button></div>";

  auto users = usersForMateria(materia);
  if (users.size() == 0) html += "<p>No hay alumnos registrados para esta materia.</p>";
  else {
    html += "<table id='students_mat_table'><tr><th>Nombre</th><th>Cuenta</th><th>Registro</th><th>Acciones</th></tr>";
    for (auto &ln : users) {
      auto c = parseQuotedCSVLine(ln);
      String name = (c.size() > 1 ? c[1] : "");
      String acc = (c.size() > 2 ? c[2] : "");
      String created = (c.size() > 4 ? c[4] : nowISO());
      html += "<tr><td>" + name + "</td><td>" + acc + "</td><td>" + created + "</td>";

      // Solo eliminar del curso
      html += "<td>";
      html += "<form method='POST' action='/student_remove_course' style='display:inline' onsubmit='return confirm(\"Eliminar este alumno de la materia?\");'>";
      html += "<input type='hidden' name='uid' value='" + c[0] + "'>";
      html += "<input type='hidden' name='materia' value='" + materia + "'>";
      html += "<input class='btn btn-red' type='submit' value='Eliminar del curso'>";
      html += "</form></td></tr>";
    }
    html += "</table>";

    // Scripts para filtros (solo Nombre y Cuenta)
    html += "<script>"
            "function applyStudentFilters(){ const table=document.getElementById('students_mat_table'); if(!table) return; const f1=document.getElementById('sf_name').value.trim().toLowerCase(); const f2=document.getElementById('sf_acc').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; if(row.cells.length<3) continue; const name=row.cells[0].textContent.toLowerCase(); const acc=row.cells[1].textContent.toLowerCase(); const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1); row.style.display = ok ? '' : 'none'; } }"
            "function clearStudentFilters(){ document.getElementById('sf_name').value=''; document.getElementById('sf_acc').value=''; applyStudentFilters(); }"
            "</script>";
  }

  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a> <a class='btn btn-blue' href='/'>Menu</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

// Mostrar todos los alumnos (completo, con edición y eliminación)
void handleStudentsAll() {
  String html = htmlHeader("Alumnos - Todos");
  html += "<div class='card'><h2>Todos los alumnos</h2>";

  // filtros (eliminado UID)
  html += "<div class='filters'><input id='sa_name' placeholder='Filtrar Nombre'><input id='sa_acc' placeholder='Filtrar Cuenta'><input id='sa_mat' placeholder='Filtrar Materia'><button class='search-btn btn btn-blue' onclick='applyAllStudentFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearAllStudentFilters()'>Limpiar</button></div>";

  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { html += "<p>No hay archivo de usuarios.</p>"; html += htmlFooter(); server.send(200,"text/html",html); return; }

  String header = f.readStringUntil('\n');
  struct SRec { String name; String acc; std::vector<String> mats; String created; };
  std::vector<String> uids;
  std::vector<SRec> recs;

  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 3) {
      String uid = c[0]; String name = c[1]; String acc = c[2]; String mat = (c.size() > 3 ? c[3] : ""); String created = (c.size() > 4 ? c[4] : nowISO());
      int idx=-1; for (int i=0;i<(int)uids.size();i++) if (uids[i]==uid) { idx=i; break; }
      if (idx==-1) { uids.push_back(uid); SRec r; r.name=name; r.acc=acc; r.created = created; if (mat.length()) r.mats.push_back(mat); recs.push_back(r); }
      else { if (mat.length()) recs[idx].mats.push_back(mat); }
    }
  }
  f.close();

  if (uids.size()==0) html += "<p>No hay alumnos registrados.</p>";
  else {
    html += "<table id='students_all_table'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
    for (int i=0;i<(int)uids.size();i++) {
      SRec &r = recs[i];
      String mats="";
      for (int j=0;j<(int)r.mats.size();j++) { if (j) mats += "; "; mats += r.mats[j]; }
      if (mats.length()==0) mats = "-";
      html += "<tr><td>" + r.name + "</td><td>" + r.acc + "</td><td>" + mats + "</td><td>" + r.created + "</td><td>";
      html += "<a class='btn btn-blue' href='/edit?uid=" + uids[i] + "'>✏️ Editar</a> ";
      html += "<form method='POST' action='/student_delete' style='display:inline' onsubmit='return confirm(\"Eliminar totalmente este alumno?\");'><input type='hidden' name='uid' value='" + uids[i] + "'><input class='btn btn-red' type='submit' value='Eliminar totalmente'></form>";
      html += "</td></tr>";
    }
    html += "</table>";

    // Scripts para filtros (solo Nombre, Cuenta y Materia)
    html += "<script>"
            "function applyAllStudentFilters(){ const table=document.getElementById('students_all_table'); if(!table) return; const f1=document.getElementById('sa_name').value.trim().toLowerCase(); const f2=document.getElementById('sa_acc').value.trim().toLowerCase(); const f3=document.getElementById('sa_mat').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; if(row.cells.length<4) continue; const name=row.cells[0].textContent.toLowerCase(); const acc=row.cells[1].textContent.toLowerCase(); const mats=row.cells[2].textContent.toLowerCase(); const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1)&&(mats.indexOf(f3)!==-1); row.style.display = ok ? '' : 'none'; } }"
            "function clearAllStudentFilters(){ document.getElementById('sa_name').value=''; document.getElementById('sa_acc').value=''; document.getElementById('sa_mat').value=''; applyAllStudentFilters(); }"
            "</script>";
  }

  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a> <a class='btn btn-blue' href='/'>Menu</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

// Remove a student from a course
void handleStudentRemoveCourse() {
  if (!server.hasArg("uid") || !server.hasArg("materia")) { server.send(400,"text/plain","faltan"); return; }
  String uid = server.arg("uid"); String materia = server.arg("materia");
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; String header = f.readStringUntil('\n'); lines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4 && c[0]==uid && c[3]==materia) continue;
    lines.push_back(l);
  }
  f.close();
  writeAllLines(USERS_FILE, lines);
  server.sendHeader("Location","/students?materia=" + materia);
  server.send(303,"text/plain","Removed");
}

// Delete student entirely
void handleStudentDelete() {
  if (!server.hasArg("uid")) { server.send(400,"text/plain","faltan"); return; }
  String uid = server.arg("uid");
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; String header = f.readStringUntil('\n'); lines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=1 && c[0]==uid) continue;
    lines.push_back(l);
  }
  f.close();
  writeAllLines(USERS_FILE, lines);
  server.sendHeader("Location","/students_all"); server.send(303,"text/plain","Deleted");
}
