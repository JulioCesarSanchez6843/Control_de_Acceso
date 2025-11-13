// src/web/courses.cpp
#include "courses.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>
#include <vector>

// ---------- Utilitarios locales ----------

// Comprueba existencia de combinación materia+profesor
static bool coursePairExists(const String &materia, const String &profesor) {
  auto courses = loadCourses();
  for (auto &c : courses) {
    if (c.materia == materia && c.profesor == profesor) return true;
  }
  return false;
}

// Comprueba si un slot está ocupado y devuelve el dueño si ownerOut != nullptr
static bool slotOccupiedLocal(const String &day, const String &start, String *ownerOut = nullptr) {
  auto schedules = loadSchedules();
  for (auto &s : schedules) {
    if (s.day == day && s.start == start) {
      if (ownerOut) *ownerOut = s.materia;
      return true;
    }
  }
  return false;
}

// Añade slot si está libre (usa addScheduleSlot() existente)
// Devuelve true si se agregó, false y err si falló
static bool addScheduleSlotSafeLocal(const String &materia, const String &day, const String &start, const String &end, String *err = nullptr) {
  String owner;
  if (slotOccupiedLocal(day, start, &owner)) {
    if (owner != materia) {
      if (err) *err = "ocupado por otra materia";
      return false;
    }
    if (err) *err = "ya asignado";
    return false;
  }
  // addScheduleSlot es la función que ya tienes; se asume que guarda en SCHEDULES_FILE
  addScheduleSlot(materia, day, start, end);
  return true;
}

// ---------- Handlers: materias ----------

// /materias (GET) - listar materias y acciones
void handleMaterias() {
  String html = htmlHeader("Materias");
  html += "<div class='card'><h2>Materias disponibles</h2>";
  auto courses = loadCourses();
  html += "<p class='small'>Pulse 'Agregar nueva materia' para registrar una materia. Desde aquí puede administrar estudiantes o ver el historial por días.</p>";

  // Filtros: solo materia y profesor
  html += "<div class='filters'><input id='f_mat' placeholder='Filtrar por materia'><input id='f_prof' placeholder='Filtrar por profesor'><button class='search-btn btn btn-blue' onclick='applyMateriaFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearMateriaFilters()'>Limpiar</button></div>";

  if (courses.size() == 0) {
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
      if (schedStr.length() == 0) schedStr = "-";
      html += "<tr><td>" + c.materia + "</td><td>" + c.profesor + "</td><td>" + c.created_at + "</td><td>" + schedStr + "</td>";

      html += "<td>";
      html += "<a class='btn btn-blue' href='/materias/edit?materia=" + c.materia + "'>✏️ Editar</a> ";

      // <-- CAMBIO: botón Horarios apunta al submenú/flujo de "materias_new_schedule" que edita SOLO esa materia
      html += "<a class='btn btn-blue' href='/materias_new_schedule?materia=" + c.materia + "'>📅 Horarios</a> ";

      html += "<a class='btn btn-blue' href='/students?materia=" + c.materia + "'>👥 Administrar Estudiantes</a> ";
      html += "<a class='btn btn-blue' href='/materia_history?materia=" + c.materia + "' title='Historial por días'>📆 Historial</a> ";
      html += "<form method='POST' action='/materias_delete' style='display:inline' onsubmit='return confirm(\"Eliminar materia y sus horarios/usuarios? Esta acción es irreversible.\");'><input type='hidden' name='materia' value='" + c.materia + "'><input class='btn btn-red' type='submit' value='Eliminar'></form>";
      html += "</td></tr>";
    }
    html += "</table>";

    // Script filtros: solo materia y profesor
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

  html += "<p style='margin-top:8px'><a class='btn btn-green' href='/materias/new'>➕ Agregar nueva materia</a> <a class='btn btn-blue' href='/'>Volver</a></p>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

// Página para agregar nueva materia (GET)
void handleMateriasNew() {
  String html = htmlHeader("Agregar Materia");
  html += "<div class='card'><h2>Agregar nueva materia</h2>";
  html += "<form method='POST' action='/materias_add'>";
  html += "Nombre materia:<br><input name='materia' required><br>";
  html += "Profesor:<br><input name='profesor' required><br><br>";
  html += "<p class='small'>Opcional: después de crear la materia podrás asignarle horarios (solo podrás agregar horarios para la materia que acabas de crear).</p>";
  html += "<input class='btn btn-green' type='submit' value='Agregar materia'> ";
  html += "<a class='btn btn-red' href='/materias'>Cancelar</a>";
  html += "</form></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// POST /materias_add
void handleMateriasAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400, "text/plain", "materia y profesor requeridos"); return; }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }
  if (prof.length() == 0) { server.send(400, "text/plain", "profesor vacio"); return; }

  if (coursePairExists(mat, prof)) {
    String html = htmlHeader("Duplicado");
    html += "<div class='card'><h3>No se puede crear: materia y profesor ya registrados.</h3>";
    html += "<p class='small'>Ya existe una entrada con <b>" + mat + "</b> y el profesor <b>" + prof + "</b>. Si deseas la misma materia con otro profesor, cambia el nombre del profesor antes de crear.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias/new'>Volver</a> <a class='btn btn-blue' href='/materias'>Lista de materias</a></p>";
    html += htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  addCourse(mat, prof);

  // Redirige a la pantalla de asignar horarios para la nueva materia (opcional)
  // Esta página permite editar SOLO los slots de la materia recién creada.
  server.sendHeader("Location", "/materias_new_schedule?materia=" + mat);
  server.send(303, "text/plain", "Continuar a asignar horarios (opcional)");
}

// GET /materias_new_schedule?materia=...
void handleMateriasNewScheduleGET() {
  if (!server.hasArg("materia")) { server.send(400, "text/plain", "materia required"); return; }
  String mat = server.arg("materia"); mat.trim();
  if (mat.length() == 0) { server.send(400, "text/plain", "materia invalida"); return; }
  if (!courseExists(mat)) { server.send(404, "text/plain", "Materia no encontrada"); return; }

  auto schedules = loadSchedules();
  String headerTitle = String("Asignar horarios - ") + mat;
  String html = htmlHeader(headerTitle.c_str());
  html += "<div class='card'><h2>Horarios para: " + mat + "</h2>";
  html += "<p class='small'>Solo puedes agregar horarios libres para esta materia o eliminar los que ya hayas agregado. Los horarios ocupados por otras materias están bloqueados.</p>";

  // Tabla tipo grilla, solo editable para la materia nueva
  html += "<table><tr><th>Hora</th>";
  for (int d = 0; d < 6; d++) html += "<th>" + String(DAYS[d]) + "</th>";
  html += "</tr>";

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
      bool occ = slotOccupiedLocal(day, start, &owner);
      html += "<td style='min-width:150px'>";
      if (occ && owner != mat) {
        // ocupado por otra materia -> mostrar bloqueado
        html += "<div class='occupied-other'>" + owner + "</div>";
      } else if (occ && owner == mat) {
        // pertenece a la materia nueva -> permitir eliminar
        html += "<div>" + owner + "</div>";
        html += "<div style='margin-top:6px'><form method='POST' action='/materias_new_schedule_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>";
        html += "<input type='hidden' name='materia' value='" + mat + "'>";
        html += "<input type='hidden' name='day' value='" + day + "'>";
        html += "<input type='hidden' name='start' value='" + start + "'>";
        html += "<input class='btn btn-red' type='submit' value='Eliminar'>";
        html += "</form></div>";
      } else {
        // libre -> permitir agregar SOLO para la materia nueva
        html += "<form method='POST' action='/materias_new_schedule_add' style='display:inline'>";
        html += "<input type='hidden' name='materia' value='" + mat + "'>";
        html += "<input type='hidden' name='day' value='" + day + "'>";
        html += "<input type='hidden' name='start' value='" + start + "'>";
        html += "<input type='hidden' name='end' value='" + end + "'>";
        html += "<input class='btn btn-green' type='submit' value='Agregar'>";
        html += "</form>";
      }
      html += "</td>";
    }
    html += "</tr>";
  }

  html += "</table>";

  // Botones Continuar / Cancelar
  html += "<p style='margin-top:12px'>";
  // Continuar: vuelve a la lista de materias (la materia ya está guardada)
  html += "<form method='GET' action='/materias' style='display:inline'><button class='btn btn-green'>Continuar</button></form> ";
  // Cancelar: hacemos POST a /materias_delete para eliminar la materia nueva
  html += "<form method='POST' action='/materias_delete' style='display:inline' onsubmit='return confirm(\"Cancelar registro y eliminar la materia? Esta acción borrará la materia y sus horarios.\");'>";
  html += "<input type='hidden' name='materia' value='" + mat + "'>";
  html += "<button class='btn btn-red' type='submit'>Cancelar registro</button>";
  html += "</form>";
  html += "</p></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

// POST /materias_new_schedule_add
void handleMateriasNewScheduleAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end")) { server.send(400, "text/plain", "faltan"); return; }
  String mat = server.arg("materia"); mat.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();
  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }
  if (!courseExists(mat)) { server.send(400, "text/plain", "Materia no registrada"); return; }

  String err;
  addScheduleSlotSafeLocal(mat, day, start, end, &err);
  server.sendHeader("Location", "/materias_new_schedule?materia=" + mat);
  server.send(303, "text/plain", "Agregado");
}

// POST /materias_new_schedule_del
void handleMateriasNewScheduleDelPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) { server.send(400, "text/plain", "faltan"); return; }
  String mat = server.arg("materia"); mat.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }

  // solo borrar si pertenece a la materia
  String owner;
  if (slotOccupiedLocal(day, start, &owner) && owner == mat) {
    File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
    std::vector<String> slines;
    if (f) {
      String header = f.readStringUntil('\n');
      slines.push_back(header);
      while (f.available()) {
        String l = f.readStringUntil('\n'); l.trim();
        if (!l.length()) continue;
        auto c = parseQuotedCSVLine(l);
        if (c.size() >= 4 && c[0] == mat && c[1] == day && c[2] == start) continue; // saltar -> eliminado
        slines.push_back(l);
      }
      f.close();
      writeAllLines(SCHEDULES_FILE, slines);
    }
  }

  server.sendHeader("Location", "/materias_new_schedule?materia=" + mat);
  server.send(303, "text/plain", "Eliminado");
}

// GET /materias/edit (renderizado)
void handleMateriasEditGET() {
  if (!server.hasArg("materia")) { server.send(400, "text/plain", "materia required"); return; }
  String mat = server.arg("materia");
  auto courses = loadCourses();
  int idx = -1;
  for (int i = 0; i < (int)courses.size(); i++) if (courses[i].materia == mat) { idx = i; break; }
  if (idx == -1) { server.send(404, "text/plain", "Materia no encontrada"); return; }

  String html = htmlHeader("Editar Materia");
  html += "<div class='card'><h2>Editar materia</h2>";
  html += "<form method='POST' action='/materias_edit'>";
  html += "<input type='hidden' name='orig_materia' value='" + mat + "'>";
  html += "Nombre materia:<br><input name='materia' value='" + courses[idx].materia + "' required><br>";
  html += "Profesor:<br><input name='profesor' value='" + courses[idx].profesor + "' required><br><br>";
  html += "<input class='btn btn-green' type='submit' value='Guardar cambios'>";
  html += "</form>";
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// POST /materias_edit
void handleMateriasEditPOST() {
  if (!server.hasArg("orig_materia") || !server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400, "text/plain", "faltan"); return; }
  String orig = server.arg("orig_materia"); orig.trim();
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }
  if (prof.length() == 0) { server.send(400, "text/plain", "profesor vacio"); return; }

  auto courses = loadCourses();
  int origIndex = -1;
  for (int i = 0; i < (int)courses.size(); i++) {
    if (courses[i].materia == orig) { origIndex = i; break; }
  }
  if (origIndex == -1) { server.send(404, "text/plain", "Materia no encontrada"); return; }

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

  courses[origIndex].materia = mat;
  courses[origIndex].profesor = prof;
  writeCourses(courses);

  // Actualiza schedules (cambia nombre materia en horarios)
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines;
  if (f) {
    String header = f.readStringUntil('\n');
    slines.push_back(header);
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4) {
        String mm = c[0];
        if (mm == orig) mm = mat;
        String newline = "\"" + mm + "\"," + "\"" + c[1] + "\"," + "\"" + c[2] + "\"," + "\"" + c[3] + "\"";
        slines.push_back(newline);
      }
    }
    f.close();
    writeAllLines(SCHEDULES_FILE, slines);
  }

  // Actualiza usuarios (reemplaza materia asociada si aplica)
  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<String> ulines;
  if (fu) {
    String uheader = fu.readStringUntil('\n');
    ulines.push_back(uheader);
    while (fu.available()) {
      String l = fu.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4) {
        String uid = c[0], name = c[1], acc = c[2], mm = c[3];
        String created = (c.size() > 4 ? c[4] : "");
        if (mm == orig) mm = mat;
        ulines.push_back("\"" + uid + "\"," + "\"" + name + "\"," + "\"" + acc + "\"," + "\"" + mm + "\"," + "\"" + created + "\"");
      } else ulines.push_back(l);
    }
    fu.close();
    writeAllLines(USERS_FILE, ulines);
  }

  server.sendHeader("Location", "/materias");
  server.send(303, "text/plain", "Editado");
}

// POST /materias_delete - elimina materia y asociados
void handleMateriasDeletePOST() {
  if (!server.hasArg("materia")) { server.send(400, "text/plain", "materia required"); return; }
  String mat = server.arg("materia"); mat.trim();
  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }

  auto courses = loadCourses();
  std::vector<Course> newCourses;
  for (auto &c : courses) if (c.materia != mat) newCourses.push_back(c);
  writeCourses(newCourses);

  // eliminar horarios
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines;
  if (f) {
    String header = f.readStringUntil('\n'); slines.push_back(header);
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4 && c[0] == mat) continue;
      slines.push_back(l);
    }
    f.close();
    writeAllLines(SCHEDULES_FILE, slines);
  }

  // eliminar usuarios
  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<String> ulines;
  if (fu) {
    String uheader = fu.readStringUntil('\n'); ulines.push_back(uheader);
    while (fu.available()) {
      String l = fu.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4 && c[3] == mat) continue;
      ulines.push_back(l);
    }
    fu.close();
    writeAllLines(USERS_FILE, ulines);
  }

  server.sendHeader("Location", "/materias");
  server.send(303, "text/plain", "Deleted");
}

// ---------------- Registro rutas -----------------
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
}

