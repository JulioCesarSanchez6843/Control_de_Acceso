// src/web/courses.cpp
#include "courses.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>
#include <vector>

// ---------- Helpers locales ----------

// Separador usado para crear una clave única de curso (materia + profesor)
static const char *COURSE_KEY_SEP = "||";

// Construye clave única "Materia||Profesor"
static String makeCourseKey(const String &materia, const String &profesor) {
  return materia + String(COURSE_KEY_SEP) + profesor;
}

// Si key contiene el separador, separa en materiaOut/profesorOut y devuelve true.
// Si no contiene, devuelve false.
static bool splitCourseKey(const String &key, String &materiaOut, String &profesorOut) {
  int idx = key.indexOf(String(COURSE_KEY_SEP));
  if (idx < 0) return false;
  materiaOut = key.substring(0, idx);
  profesorOut = key.substring(idx + strlen(COURSE_KEY_SEP));
  return true;
}

// URL-encode pequeño helper
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

// Cuenta cuántos cursos existen con el mismo nombre de materia
static int countCoursesWithName(const String &materia) {
  auto courses = loadCourses();
  int cnt = 0;
  for (auto &c : courses) if (c.materia == materia) cnt++;
  return cnt;
}

// Comprueba existencia de combinación materia+profesor (ya la tenías)
static bool coursePairExists(const String &materia, const String &profesor) {
  auto courses = loadCourses();
  for (auto &c : courses) {
    if (c.materia == materia && c.profesor == profesor) return true;
  }
  return false;
}

// Comprueba si un slot está ocupado y devuelve el dueño (la cadena tal y como está en schedules file).
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
// Ahora recibe la clave de curso (courseKey), que puede ser legacy (solo materia) o compuesta "materia||profesor".
static bool addScheduleSlotSafeLocalKey(const String &courseKey, const String &day, const String &start, const String &end, String *err = nullptr) {
  String owner;
  if (slotOccupiedLocal(day, start, &owner)) {
    if (owner != courseKey) {
      if (err) *err = "ocupado por otra materia";
      return false;
    }
    if (err) *err = "ya asignado";
    return false;
  }
  // addScheduleSlot guarda exactamente lo que le pases en 'materia' campo de schedule.
  addScheduleSlot(courseKey, day, start, end);
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
      // Recorremos schedules y asignamos solo los que pertenezcan a esta pareja (o legacy si es único)
      for (auto &s : schedules) {
        String schedOwner = s.materia;
        String ownerMat, ownerProf;
        if (splitCourseKey(schedOwner, ownerMat, ownerProf)) {
          // schedule con clave compuesta -> comparar ambos
          if (ownerMat == c.materia && ownerProf == c.profesor) {
            if (schedStr.length()) schedStr += "; ";
            schedStr += s.day + " " + s.start + "-" + s.end;
          }
        } else {
          // legacy: solo materia. Solo asociarlo si existe una única materia con ese nombre (evita compartirlo entre varios profesores)
          if (schedOwner == c.materia) {
            if (countCoursesWithName(c.materia) == 1) {
              if (schedStr.length()) schedStr += "; ";
              schedStr += s.day + " " + s.start + "-" + s.end;
            }
            // si hay >1 curso con mismo nombre, no lo asociamos automáticamente (ambigüedad).
          }
        }
      }
      if (schedStr.length() == 0) schedStr = "-";
      html += "<tr><td>" + c.materia + "</td><td>" + c.profesor + "</td><td>" + c.created_at + "</td><td>" + schedStr + "</td>";

      html += "<td>";
      // Editar -> verde (no destructivo). Pasamos materia+profesor en la query para identificar el registro exacto.
      html += "<a class='btn btn-green' href='/materias/edit?materia=" + urlEncode(c.materia) + "&profesor=" + urlEncode(c.profesor) + "'>✏️ Editar</a> ";

      // Horarios -> amarillo (edición de horarios) : enviamos materia+profesor para que los horarios se manejen por pareja única.
      html += "<a class='btn btn-yellow' href='/materias_new_schedule?materia=" + urlEncode(c.materia) + "&profesor=" + urlEncode(c.profesor) + "' style='background:#f1c40f;color:#111;padding:6px 10px;border-radius:6px;text-decoration:none;margin-left:6px;'>📅 Horarios</a> ";

      // Administrar estudiantes -> morado/clarito
      html += "<a class='btn btn-purple' href='/students?materia=" + urlEncode(c.materia) + "&profesor=" + urlEncode(c.profesor) + "' style='background:#6dd3d0;color:#000;padding:6px 10px;border-radius:6px;text-decoration:none;margin-left:6px;'>👥 Administrar Estudiantes</a> ";

      // Historial por días -> naranja
      html += "<a class='btn btn-orange' href='/materia_history?materia=" + urlEncode(c.materia) + "&profesor=" + urlEncode(c.profesor) + "' title='Historial por días' style='background:#e67e22;color:#fff;padding:6px 10px;border-radius:6px;text-decoration:none;margin-left:6px;'>📆 Historial</a> ";

      // Eliminar (peligroso) -> rojo: incluimos profesor para eliminar solo este registro
      html += "<form method='POST' action='/materias_delete' style='display:inline' onsubmit='return confirm(\"Eliminar materia y sus horarios/usuarios? Esta acción es irreversible.\");'>"
              "<input type='hidden' name='materia' value='" + c.materia + "'>"
              "<input type='hidden' name='profesor' value='" + c.profesor + "'>"
              "<input class='btn btn-red' type='submit' value='Eliminar'></form>";
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

  // Botones inferiores: Agregar nueva materia (verde) + Inicio (azul)
  html += "<p style='margin-top:8px'><a class='btn btn-green' href='/materias/new'>➕ Agregar nueva materia</a> <a class='btn btn-blue' href='/'>Inicio</a></p>";
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
  if (!server.hasArg("materia") || !server.hasArg("profesor")) {
    server.send(400, "text/plain", "materia y profesor requeridos");
    return;
  }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }
  if (prof.length() == 0) { server.send(400, "text/plain", "profesor vacio"); return; }

  if (coursePairExists(mat, prof)) {
    String html = htmlHeader("Operación inválida");
    html += "<div class='card'><h3>Operación inválida — duplicado de materia y profesor</h3>";
    html += "<p class='small'>No se puede registrar la misma materia con el mismo profesor porque ya existe una entrada idéntica en el sistema.</p>";
    html += "<p class='small'><b>Materia:</b> " + mat + " &nbsp; <b>Profesor:</b> " + prof + "</p>";
    html += "<p class='small'>Si desea asociar esta materia a otro profesor, vuelva al formulario de registro y cambie el nombre del profesor. Si lo que desea es ver o editar materias existentes, use la lista de materias.</p>";
    html += "<p style='margin-top:8px'><a class='btn btn-green' href='/materias/new'>Regresar</a> <a class='btn btn-blue' href='/materias'>Lista de materias</a></p>";
    html += htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  addCourse(mat, prof);

  // Redirige a la pantalla de asignar horarios para la nueva materia (opcional)
  // Enviamos tanto materia como profesor
  server.sendHeader("Location", "/materias_new_schedule?materia=" + urlEncode(mat) + "&profesor=" + urlEncode(prof) + "&new=1");
  server.send(303, "text/plain", "Continuar a asignar horarios (opcional)");
}

// GET /materias_new_schedule?materia=...&profesor=...
void handleMateriasNewScheduleGET() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400, "text/plain", "materia y profesor requeridos"); return; }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length() == 0 || prof.length() == 0) { server.send(400, "text/plain", "materia o profesor invalidos"); return; }
  if (!coursePairExists(mat, prof)) { server.send(404, "text/plain", "Curso no encontrado"); return; }

  bool fromNewFlow = (server.hasArg("new") && server.arg("new") == "1");

  auto schedules = loadSchedules();
  String headerTitle = String("Asignar horarios - ") + mat + " (" + prof + ")";
  String html = htmlHeader(headerTitle.c_str());
  html += "<div class='card'><h2>Horarios para: " + mat + " — " + prof + "</h2>";

  // Tabla tipo grilla, editable para la materia indicada (usaremos la clave compuesta)
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
      bool occ = slotOccupiedLocal(day, start, &owner);
      html += "<td style='min-width:150px'>";
      if (occ) {
        // Si owner es composite, comparar con courseKey
        String ownerMat, ownerProf;
        if (splitCourseKey(owner, ownerMat, ownerProf)) {
          if (owner == courseKey) {
            html += "<div>" + ownerMat + " (" + ownerProf + ")</div>";
            html += "<div style='margin-top:6px'><form method='POST' action='/materias_new_schedule_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>"
                    "<input type='hidden' name='materia' value='" + mat + "'>"
                    "<input type='hidden' name='profesor' value='" + prof + "'>"
                    "<input type='hidden' name='day' value='" + day + "'>"
                    "<input type='hidden' name='start' value='" + start + "'>"
                    "<input class='btn btn-red' type='submit' value='Eliminar'>"
                    "</form></div>";
          } else {
            // ocupado por otra pareja -> mostrar bloqueado (solo nombre)
            html += "<div class='occupied-other'>" + ownerMat + " (" + ownerProf + ")</div>";
          }
        } else {
          // legacy owner (solo materia)
          if (owner == mat && countCoursesWithName(mat) == 1) {
            html += "<div>" + owner + "</div>";
            html += "<div style='margin-top:6px'><form method='POST' action='/materias_new_schedule_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>"
                    "<input type='hidden' name='materia' value='" + mat + "'>"
                    "<input type='hidden' name='profesor' value='" + prof + "'>"
                    "<input type='hidden' name='day' value='" + day + "'>"
                    "<input type='hidden' name='start' value='" + start + "'>"
                    "<input class='btn btn-red' type='submit' value='Eliminar'>"
                    "</form></div>";
          } else {
            html += "<div class='occupied-other'>" + owner + "</div>";
          }
        }
      } else {
        // libre -> permitir agregar con clave compuesta
        html += "<form method='POST' action='/materias_new_schedule_add' style='display:inline'>";
        html += "<input type='hidden' name='materia' value='" + mat + "'>";
        html += "<input type='hidden' name='profesor' value='" + prof + "'>";
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

  html += "<p style='margin-top:12px'>";
  if (fromNewFlow) {
    html += "<form method='GET' action='/materias' style='display:inline'><button class='btn btn-green'>Continuar</button></form> ";
    html += "<form method='POST' action='/materias_delete' style='display:inline' onsubmit='return confirm(\"Cancelar registro y eliminar la materia? Esta acción borrará la materia y sus horarios.\");'>"; 
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

// POST /materias_new_schedule_add
void handleMateriasNewScheduleAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor") || !server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end")) {
    server.send(400, "text/plain", "faltan"); return;
  }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();
  if (mat.length() == 0 || prof.length() == 0) { server.send(400, "text/plain", "materia o profesor vacio"); return; }
  if (!coursePairExists(mat, prof)) { server.send(400, "text/plain", "Curso no registrado"); return; }

  String courseKey = makeCourseKey(mat, prof);
  String err;
  addScheduleSlotSafeLocalKey(courseKey, day, start, end, &err);

  server.sendHeader("Location", "/materias_new_schedule?materia=" + urlEncode(mat) + "&profesor=" + urlEncode(prof));
  server.send(303, "text/plain", "Agregado");
}

// POST /materias_new_schedule_del
void handleMateriasNewScheduleDelPOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor") || !server.hasArg("day") || !server.hasArg("start")) {
    server.send(400, "text/plain", "faltan"); return;
  }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  if (mat.length() == 0 || prof.length() == 0) { server.send(400, "text/plain", "materia/profesor vacio"); return; }

  String courseKey = makeCourseKey(mat, prof);

  // Solo eliminamos el horario si pertenece a esta pareja (o si es legacy y no hay ambigüedad)
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
        String owner = c[0];
        String dayc = c[1];
        String startc = c[2];
        if (dayc == day && startc == start) {
          String ownerMat, ownerProf;
          if (splitCourseKey(owner, ownerMat, ownerProf)) {
            // composite: eliminar solo si coincide exactamente con courseKey
            if (owner == courseKey) continue; // skip -> removed
          } else {
            // legacy: eliminar solo si owner equals mat AND there is exactly one course with that name
            if (owner == mat && countCoursesWithName(mat) == 1) continue;
          }
        }
      }
      slines.push_back(l);
    }
    f.close();
    writeAllLines(SCHEDULES_FILE, slines);
  }

  server.sendHeader("Location", "/materias_new_schedule?materia=" + urlEncode(mat) + "&profesor=" + urlEncode(prof));
  server.send(303, "text/plain", "Eliminado");
}

// GET /materias/edit (renderizado)
// Ahora requiere materia + profesor para identificar el registro único
void handleMateriasEditGET() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400, "text/plain", "materia y profesor requeridos"); return; }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  auto courses = loadCourses();
  int idx = -1;
  for (int i = 0; i < (int)courses.size(); i++) {
    if (courses[i].materia == mat && courses[i].profesor == prof) { idx = i; break; }
  }
  if (idx == -1) { server.send(404, "text/plain", "Materia no encontrada"); return; }

  String html = htmlHeader("Editar Materia");
  html += "<div class='card'><h2>Editar materia</h2>";
  html += "<form method='POST' action='/materias_edit'>";
  html += "<input type='hidden' name='orig_materia' value='" + mat + "'>";
  html += "<input type='hidden' name='orig_profesor' value='" + prof + "'>";
  html += "Nombre materia:<br><input name='materia' value='" + courses[idx].materia + "' required><br>";
  html += "Profesor:<br><input name='profesor' value='" + courses[idx].profesor + "' required><br><br>";
  html += "<input class='btn btn-green' type='submit' value='Guardar cambios'>"; // guardar verde
  html += "</form>";
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// POST /materias_edit
void handleMateriasEditPOST() {
  if (!server.hasArg("orig_materia") || !server.hasArg("orig_profesor") || !server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400, "text/plain", "faltan"); return; }
  String orig = server.arg("orig_materia"); orig.trim();
  String origProf = server.arg("orig_profesor"); origProf.trim();
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length() == 0) { server.send(400, "text/plain", "materia vacia"); return; }
  if (prof.length() == 0) { server.send(400, "text/plain", "profesor vacio"); return; }

  auto courses = loadCourses();
  int origIndex = -1;
  for (int i = 0; i < (int)courses.size(); i++) {
    if (courses[i].materia == orig && courses[i].profesor == origProf) { origIndex = i; break; }
  }
  if (origIndex == -1) { server.send(404, "text/plain", "Materia no encontrada"); return; }

  // Evitar duplicado materia+profesor (mismo chequeo que antes)
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

  // Actualizamos el curso
  String oldMat = courses[origIndex].materia;
  String oldProf = courses[origIndex].profesor;
  courses[origIndex].materia = mat;
  courses[origIndex].profesor = prof;
  writeCourses(courses);

  // Actualiza schedules: reemplazamos entradas que pertenecen a ESTE curso concreto
  // (tanto composite keys como legacy si no hay ambigüedad)
  String oldKey = makeCourseKey(oldMat, oldProf);
  String newKey = makeCourseKey(mat, prof);
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines;
  if (f) {
    String header = f.readStringUntil('\n'); slines.push_back(header);
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4) {
        String owner = c[0];
        String day = c[1];
        String start = c[2];
        String rest = c[3];
        // Si owner es composite y coincide con oldKey => sustituir
        if (owner == oldKey) {
          String newline = "\"" + newKey + "\"," + "\"" + day + "\"," + "\"" + start + "\"," + "\"" + rest + "\"";
          slines.push_back(newline);
          continue;
        }
        // Si owner es legacy (solo materia) y owner == oldMat:
        // Solo lo reemplazamos si no hay ambigüedad (es decir, countCoursesWithName(oldMat) == 1 BEFORE change).
        // Pero ya cambiamos courses; así que para comprobar la situación 'antes del cambio' es complejo.
        // He elegido: si owner == oldMat and after the edit there is exactly one course with that name (mat),
        // then we'll update it (this mirrors previous behaviour in simple cases).
        if (owner == oldMat) {
          // if currently there is exactly one course with owner name -> update; else keep legacy as-is
          if (countCoursesWithName(oldMat) == 1) {
            String newline = "\"" + newKey + "\"," + "\"" + day + "\"," + "\"" + start + "\"," + "\"" + rest + "\"";
            slines.push_back(newline);
            continue;
          }
        }
      }
      slines.push_back(l);
    }
    f.close();
    writeAllLines(SCHEDULES_FILE, slines);
  }

  // Actualiza usuarios (reemplaza materia asociada si aplica)
  // Nota: usuarios sólo guardan el nombre de materia sin profesor. Si existen múltiples cursos con mismo nombre
  // este reemplazo afectará a todos los usuarios con ese nombre de materia (comportamiento previo).
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
        if (mm == oldMat) mm = mat; // cambiar el campo materia textual
        ulines.push_back("\"" + uid + "\"," + "\"" + name + "\"," + "\"" + acc + "\"," + "\"" + mm + "\"," + "\"" + created + "\"");
      } else ulines.push_back(l);
    }
    fu.close();
    writeAllLines(USERS_FILE, ulines);
  }

  server.sendHeader("Location", "/materias");
  server.send(303, "text/plain", "Editado");
}

// POST /materias_delete - elimina materia y asociados (solo la pareja materia+profesor enviada)
void handleMateriasDeletePOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400, "text/plain", "materia y profesor requeridos"); return; }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length() == 0 || prof.length() == 0) { server.send(400, "text/plain", "materia/profesor vacio"); return; }

  auto courses = loadCourses();
  std::vector<Course> newCourses;
  for (auto &c : courses) {
    if (!(c.materia == mat && c.profesor == prof)) newCourses.push_back(c);
  }
  writeCourses(newCourses);

  // eliminar horarios: solo aquellos que pertenezcan a la pareja exacta (composite key),
  // y legacy solo si no hay ambigüedad (es decir, si había única materia con ese nombre).
  String targetKey = makeCourseKey(mat, prof);

  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines;
  if (f) {
    String header = f.readStringUntil('\n'); slines.push_back(header);
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4) {
        String owner = c[0];
        if (owner == targetKey) continue; // borrar composite exact match
        // legacy owner (solo materia): borrar solo si countCoursesWithName(mat) == 0 AFTER deletion
        if (owner == mat) {
          // if after deletion no courses with that name remain -> safe to delete legacy schedule
          if (countCoursesWithName(mat) == 0) continue;
        }
      }
      slines.push_back(l);
    }
    f.close();
    writeAllLines(SCHEDULES_FILE, slines);
  }

  // eliminar usuarios: los usuarios guardan materia textual; eliminamos solo si su materia coincide
  // con mat y, dado que estamos borrando solo una pareja profesor, dejamos la lógica previa: eliminar usuarios
  // asociadas si su materia coincide y NO existen otros cursos con el mismo nombre (evitar borrar usuarios equivocadamente).
  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<String> ulines;
  if (fu) {
    String uheader = fu.readStringUntil('\n'); ulines.push_back(uheader);
    while (fu.available()) {
      String l = fu.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4) {
        String uid = c[0], name = c[1], acc = c[2], mm = c[3];
        // eliminar usuarios solo si mm == mat AND no quedan cursos con nombre mat
        if (mm == mat && countCoursesWithName(mat) == 0) continue;
        ulines.push_back(l);
      } else ulines.push_back(l);
    }
    fu.close();
    writeAllLines(USERS_FILE, ulines);
  }

  server.sendHeader("Location", "/materias");
  server.send(303, "text/plain", "Eliminado");
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
