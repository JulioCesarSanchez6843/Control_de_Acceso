#include "schedules.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>

// /schedules (GET) - mostrar grilla
void handleSchedulesGrid() {
  auto schedules = loadSchedules();
  String html = htmlHeader("Horarios - Grilla");
  html += "<div class='card'><h2>Horarios del Laboratorio (LUN - SAB)</h2>";
  html += "<p class='small'>Vista de la grilla de horarios. Para editar/agregar/quitar horarios pulsa <b>Editar Horarios</b> arriba o selecciona una materia desde Materias para editar solo esa materia.</p>";
  html += "<table><tr><th>Hora</th>";
  for (int d=0; d<6; d++) html += "<th>" + DAYS[d] + "</th>";
  html += "</tr>";
  for (int s=0; s<SLOT_COUNT; s++) {
    int h = SLOT_STARTS[s];
    char lbl[16];
    snprintf(lbl, sizeof(lbl), "%02d:00 - %02d:00", h, h+2);
    html += "<tr><th>" + String(lbl) + "</th>";
    for (int d=0; d<6; d++) {
      String day = DAYS[d];
      String start = String(h) + ":00";
      String cell = "-";
      for (auto &e : schedules) {
        if (e.day == day && e.start == start) { cell = e.materia; break; }
      }
      html += "<td>" + cell + "</td>";
    }
    html += "</tr>";
  }
  html += "</table>";
  html += "<p style='margin-top:12px'><a class='btn btn-blue' href='/schedules/edit'>✏️ Editar Horarios</a> <a class='btn btn-blue' href='/materias'>Ver Materias</a> <a class='btn btn-blue' href='/'>Volver</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}

// /schedules/edit (GET) - editor global
void handleSchedulesEditGrid() {
  auto schedules = loadSchedules();
  String html = htmlHeader("Horarios - Editar (Global)");
  html += "<div class='card'><h2>Editar horarios (Global)</h2>";
  html += "<p class='small'>Seleccione una materia registrada para asignar al slot vacío, o elimine materias asignadas. Aquí puede editar cualquier slot globalmente.</p>";
  html += "<table><tr><th>Hora</th>";
  for (int d=0; d<6; d++) html += "<th>" + DAYS[d] + "</th>";
  html += "</tr>";
  auto courses = loadCourses();
  for (int s=0; s<SLOT_COUNT; s++) {
    int h = SLOT_STARTS[s];
    char lbl[16];
    snprintf(lbl, sizeof(lbl), "%02d:00 - %02d:00", h, h+2);
    html += "<tr><th>" + String(lbl) + "</th>";
    for (int d=0; d<6; d++) {
      String day = DAYS[d];
      String start = String(h) + ":00";
      String end = String(h+2) + ":00";
      String cell = "";
      bool occupied = false;
      for (auto &e : schedules) {
        if (e.day == day && e.start == start) { occupied = true; cell = e.materia; break; }
      }
      html += "<td>";
      if (occupied) {
        html += "<div>" + cell + "</div><div style='margin-top:6px'><form method='POST' action='/schedules_del' style='display:inline'><input type='hidden' name='materia' value='" + cell + "'><input type='hidden' name='day' value='" + day + "'><input type='hidden' name='start' value='" + start + "'><input class='btn btn-red' type='submit' value='Eliminar'></form></div>";
      } else {
        html += "<form method='POST' action='/schedules_add_slot' style='display:inline'>";
        html += "<input type='hidden' name='day' value='" + day + "'>";
        html += "<input type='hidden' name='start' value='" + start + "'>";
        html += "<input type='hidden' name='end' value='" + end + "'>";
        html += "<select name='materia'>";
        html += "<option value=''>-- Seleccionar materia --</option>";
        for (auto &c : courses) html += "<option value='" + c.materia + "'>" + c.materia + " (" + c.profesor + ")</option>";
        html += "</select> ";
        html += " <input class='btn btn-green' type='submit' value='Agregar'>";
        html += "</form>";
      }
      html += "</td>";
    }
    html += "</tr>";
  }
  html += "</table>";
  html += "<p style='margin-top:12px'><a class='btn btn-blue' href='/schedules'>Ver Horarios (solo lectura)</a> <a class='btn btn-blue' href='/'>Volver</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}

// /schedules_add_slot (POST)
void handleSchedulesAddSlot() {
  if (!server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end") || !server.hasArg("materia")) {
    server.send(400,"text/plain","faltan parametros"); return;
  }
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();
  String materia = server.arg("materia"); materia.trim();

  if (day.length()==0 || start.length()==0 || end.length()==0 || materia.length()==0) { server.send(400,"text/plain","datos invalidos"); return; }

  if (!courseExists(materia)) {
    server.send(400,"text/plain","Materia no registrada. Registre la materia en Materias antes de asignarla a un horario.");
    return;
  }

  if (slotOccupied(day, start)) {
    server.sendHeader("Location","/schedules?msg=ocupado");
    server.send(303,"text/plain","Slot ocupado");
    return;
  }

  addScheduleSlot(materia, day, start, end);
  server.sendHeader("Location","/schedules/edit");
  server.send(303,"text/plain","Agregado");
}

// /schedules_del (POST)
void handleSchedulesDel() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) { server.send(400,"text/plain","faltan"); return; }
  String mat = server.arg("materia"); String day = server.arg("day"); String start = server.arg("start");
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines;
  String header = f.readStringUntil('\n');
  lines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4 && c[0]==mat && c[1]==day && c[2]==start) continue;
    lines.push_back(l);
  }
  f.close();
  writeAllLines(SCHEDULES_FILE, lines);
  server.sendHeader("Location","/schedules/edit"); server.send(303,"text/plain","Borrado");
}

// /schedules_for (GET)
void handleSchedulesForMateriaGET() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  if (!courseExists(materia)) { server.send(404,"text/plain","Materia no encontrada"); return; }

  auto schedules = loadSchedules();
  String html = htmlHeader(("Horarios - " + materia).c_str());
  html += "<div class='card'><h2>Horarios para: " + materia + "</h2>";
  html += "<p class='small'>Aquí puede agregar o eliminar horarios únicamente para la materia seleccionada.</p>";

  html += "<div class='filters'><input id='sf_day' placeholder='Filtrar día (LUN/MAR/...)'><input id='sf_time' placeholder='Filtrar hora (07:00)'><button class='search-btn btn btn-blue' onclick='applySchedFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearSchedFilters()'>Limpiar</button></div>";

  html += "<table id='sched_mat_table'><tr><th>Día</th><th>Inicio</th><th>Fin</th><th>Acción</th></tr>";
  for (auto &s : schedules) {
    if (s.materia != materia) continue;
    html += "<tr><td>" + s.day + "</td><td>" + s.start + "</td><td>" + s.end + "</td>";
    html += "<td><form method='POST' action='/schedules_for_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'><input type='hidden' name='materia' value='" + materia + "'><input type='hidden' name='day' value='" + s.day + "'><input type='hidden' name='start' value='" + s.start + "'><input class='btn btn-red' type='submit' value='Eliminar'></form></td></tr>";
  }
  html += "</table>";

  html += "<h3>Añadir horario para " + materia + "</h3>";
  html += "<form method='POST' action='/schedules_for_add'>";
  html += "<input type='hidden' name='materia' value='" + materia + "'>";
  html += "Día: <select name='day'>";
  for (int d=0; d<6; d++) html += "<option value='" + DAYS[d] + "'>" + DAYS[d] + "</option>";
  html += "</select> Inicio (HH:MM): <input name='start' placeholder='07:00'> Fin (HH:MM): <input name='end' placeholder='09:00'> ";
  html += "<input class='btn btn-green' type='submit' value='Agregar horario'>";
  html += "</form>";

  html += "<script>"
          "function applySchedFilters(){ const table=document.getElementById('sched_mat_table'); if(!table) return; const fd=document.getElementById('sf_day').value.trim().toLowerCase(); const ft=document.getElementById('sf_time').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; const day=row.cells[0].textContent.toLowerCase(); const start=row.cells[1].textContent.toLowerCase(); const ok=(day.indexOf(fd)!==-1)&&(start.indexOf(ft)!==-1); row.style.display = ok ? '' : 'none'; } }"
          "function clearSchedFilters(){ document.getElementById('sf_day').value=''; document.getElementById('sf_time').value=''; applySchedFilters(); }"
          "</script>";

  html += "<p style='margin-top:12px'><a class='btn btn-blue' href='/materias'>Volver</a> <a class='btn btn-blue' href='/'>Menu</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}

void handleSchedulesForMateriaAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end")) { server.send(400,"text/plain","faltan parametros"); return; }
  String materia = server.arg("materia"); String day = server.arg("day"); String start = server.arg("start"); String end = server.arg("end");
  materia.trim(); day.trim(); start.trim(); end.trim();
  if (materia.length()==0 || day.length()==0 || start.length()==0 || end.length()==0) { server.send(400,"text/plain","datos invalidos"); return; }
  if (!courseExists(materia)) { server.send(400,"text/plain","Materia no registrada"); return; }
  if (slotOccupied(day, start)) {
    server.sendHeader("Location","/schedules_for?materia=" + materia + "&msg=ocupado");
    server.send(303,"text/plain","Slot ocupado");
    return;
  }
  addScheduleSlot(materia, day, start, end);
  server.sendHeader("Location","/schedules_for?materia=" + materia);
  server.send(303,"text/plain","Agregado");
}

void handleSchedulesForMateriaDelPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) { server.send(400,"text/plain","faltan"); return; }
  String mat = server.arg("materia"); String day = server.arg("day"); String start = server.arg("start");
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines;
  String header = f.readStringUntil('\n');
  lines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4 && c[0]==mat && c[1]==day && c[2]==start) continue;
    lines.push_back(l);
  }
  f.close();
  writeAllLines(SCHEDULES_FILE, lines);
  server.sendHeader("Location","/schedules_for?materia=" + mat); server.send(303,"text/plain","Borrado");
}
