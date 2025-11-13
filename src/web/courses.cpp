#include "courses.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>

// /materias (GET) - listar materias y acciones
void handleMaterias() {
  String html = htmlHeader("Materias");
  html += "<div class='card'><h2>Materias disponibles</h2>";
  std::vector<Course> courses = loadCourses();
  html += "<p class='small'>Pulse 'Agregar nueva materia' para registrar una materia. Desde aquí puede administrar estudiantes o abrir el historial por días.</p>";

  html += "<div class='filters'><input id='f_mat' placeholder='Filtrar por materia'><input id='f_prof' placeholder='Filtrar por profesor'><input id='f_date' placeholder='Filtrar por fecha (YYYY-MM-DD)'><button class='search-btn btn btn-blue' onclick='applyMateriaFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearMateriaFilters()'>Limpiar</button></div>";

  if (courses.size()==0) {
    html += "<p>No hay materias registradas.</p>";
  } else {
    auto schedules = loadSchedules();
    html += "<table id='materias_table'><tr><th>Materia</th><th>Profesor</th><th>Creado</th><th>Horarios</th><th>Acción</th></tr>";
    for (auto &c : courses) {
      String schedStr = "";
      for (auto &s : schedules) {
        if (s.materia == c.materia) {
          if (schedStr.length()) schedStr += "; ";
          schedStr += s.day + " " + s.start + "-" + s.end;
        }
      }
      if (schedStr.length()==0) schedStr = "-";
      html += "<tr><td>" + c.materia + "</td><td>" + c.profesor + "</td><td>" + c.created_at + "</td><td>" + schedStr + "</td>";
      html += "<td>";
      html += "<a class='btn btn-blue' href='/materias/edit?materia=" + c.materia + "'>✏️ Editar</a> ";
      html += "<a class='btn btn-blue' href='/schedules_for?materia=" + c.materia + "'>📅 Horarios</a> ";
      html += "<a class='btn btn-blue' href='/students?materia=" + c.materia + "'>👥 Administrar Estudiantes</a> ";
      html += "<a class='btn btn-blue' href='/materia_history?materia=" + c.materia + "' title='Historial por días'>📆 Historial</a> ";
      html += "<a class='btn btn-blue' href='/history.csv?materia=" + c.materia + "' title='Descargar historial de esta materia'>⬇️ CSV</a> ";
      html += "<form method='POST' action='/materias_delete' style='display:inline' onsubmit='return confirm(\"Eliminar materia y sus horarios/usuarios? Esta acción es irreversible.\");'><input type='hidden' name='materia' value='" + c.materia + "'><input class='btn btn-red' type='submit' value='Eliminar'></form>";
      html += "</td></tr>";
    }
    html += "</table>";

    html += "<script>"
            "function applyMateriaFilters(){ const table=document.getElementById('materias_table'); if(!table) return; const fmat=document.getElementById('f_mat').value.trim().toLowerCase(); const fprof=document.getElementById('f_prof').value.trim().toLowerCase(); const fdate=document.getElementById('f_date').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; if(row.cells.length<4) continue; const mat=row.cells[0].textContent.toLowerCase(); const prof=row.cells[1].textContent.toLowerCase(); const date=row.cells[2].textContent.toLowerCase(); const ok = (mat.indexOf(fmat)!==-1) && (prof.indexOf(fprof)!==-1) && (date.indexOf(fdate)!==-1); row.style.display = ok ? '' : 'none'; } }"
            "function clearMateriaFilters(){ document.getElementById('f_mat').value=''; document.getElementById('f_prof').value=''; document.getElementById('f_date').value=''; applyMateriaFilters(); }"
            "</script>";
  }

  html += "<p style='margin-top:8px'><a class='btn btn-green' href='/materias/new'>➕ Agregar nueva materia</a> <a class='btn btn-blue' href='/'>Volver</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleMateriasNew() {
  String html = htmlHeader("Agregar Materia");
  html += "<div class='card'><h2>Agregar nueva materia</h2>";
  html += "<form method='POST' action='/materias_add'>";
  html += "Nombre materia:<br><input name='materia' required><br>";
  html += "Profesor:<br><input name='profesor' required><br><br>";
  html += "<p class='small'>Luego de crear la materia, será redirigido a asignar horarios para esa materia.</p>";
  html += "<input class='btn btn-green' type='submit' value='Agregar materia'>";
  html += "</form>";
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}

void handleMateriasAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400,"text/plain","materia y profesor requeridos"); return; }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length()==0) { server.send(400,"text/plain","materia vacia"); return; }
  if (courseExists(mat)) { server.sendHeader("Location","/materias?msg=duplicado"); server.send(303,"text/plain","Materia duplicada"); return; }
  addCourse(mat, prof);

  // redirect to per-materia schedule editor so user can add slots for the new materia
  server.sendHeader("Location","/schedules_for?materia=" + mat);
  server.send(303,"text/plain","Agregado - Asignar horarios");
}

void handleMateriasEditGET() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String mat = server.arg("materia");
  auto courses = loadCourses();
  int idx=-1;
  for (int i=0;i<(int)courses.size();i++) if (courses[i].materia == mat) { idx=i; break; }
  if (idx==-1) { server.send(404,"text/plain","Materia no encontrada"); return; }
  String html = htmlHeader("Editar Materia");
  html += "<div class='card'><h2>Editar materia</h2>";
  html += "<form method='POST' action='/materias_edit'>";
  html += "<input type='hidden' name='orig_materia' value='" + mat + "'>";
  html += "Nombre materia:<br><input name='materia' value='" + courses[idx].materia + "' required><br>";
  html += "Profesor:<br><input name='profesor' value='" + courses[idx].profesor + "' required><br><br>";
  html += "<input class='btn btn-green' type='submit' value='Guardar cambios'>";
  html += "</form>";
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}

void handleMateriasEditPOST() {
  if (!server.hasArg("orig_materia") || !server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400,"text/plain","faltan"); return; }
  String orig = server.arg("orig_materia"); orig.trim();
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length()==0) { server.send(400,"text/plain","materia vacia"); return; }

  auto courses = loadCourses();
  bool found=false;
  for (int i=0;i<(int)courses.size();i++) {
    if (courses[i].materia == orig) {
      if (mat != orig && courseExists(mat)) { server.send(400,"text/plain","Nombre de materia ya existe"); return; }
      courses[i].materia = mat;
      courses[i].profesor = prof;
      found = true;
      break;
    }
  }
  if (!found) { server.send(404,"text/plain","Materia no encontrada"); return; }
  writeCourses(courses);

  // Update schedules and users to replace materia name
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines; String header = f.readStringUntil('\n'); slines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4) {
      String mm = c[0]; if (mm == orig) mm = mat;
      String newline = "\"" + mm + "\"," + "\"" + c[1] + "\"," + "\"" + c[2] + "\"," + "\"" + c[3] + "\"";
      slines.push_back(newline);
    }
  }
  f.close();
  writeAllLines(SCHEDULES_FILE, slines);

  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<String> ulines; String uheader = fu.readStringUntil('\n'); ulines.push_back(uheader);
  while (fu.available()) {
    String l = fu.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4) {
      String uid = c[0], name = c[1], acc = c[2], mm = c[3];
      String created = (c.size()>4?c[4]:"");
      if (mm == orig) mm = mat;
      ulines.push_back("\"" + uid + "\"," + "\"" + name + "\"," + "\"" + acc + "\"," + "\"" + mm + "\"," + "\"" + created + "\"");
    } else ulines.push_back(l);
  }
  fu.close();
  writeAllLines(USERS_FILE, ulines);

  server.sendHeader("Location","/materias");
  server.send(303,"text/plain","Editado");
}

void handleMateriasDeletePOST() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String mat = server.arg("materia"); mat.trim();
  if (mat.length()==0) { server.send(400,"text/plain","materia vacia"); return; }

  auto courses = loadCourses();
  std::vector<Course> newCourses;
  for (auto &c : courses) if (c.materia != mat) newCourses.push_back(c);
  writeCourses(newCourses);

  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines; String header = f.readStringUntil('\n'); slines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4 && c[0]==mat) continue;
    slines.push_back(l);
  }
  f.close();
  writeAllLines(SCHEDULES_FILE, slines);

  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<String> ulines; String uheader = fu.readStringUntil('\n'); ulines.push_back(uheader);
  while (fu.available()) {
    String l = fu.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4 && c[3]==mat) continue;
    ulines.push_back(l);
  }
  fu.close();
  writeAllLines(USERS_FILE, ulines);

  server.sendHeader("Location","/materias");
  server.send(303,"text/plain","Deleted");
}
