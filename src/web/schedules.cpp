// src/web/schedules.cpp
// Reemplazar completamente por este archivo.
// Implementa fallback: si /profesores_for devuelve 404 o no-JSON, intenta parsear /teachers?materia=...
#include "schedules.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>
#include <vector>

// ---------- utilitarios locales ----------
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

static std::vector<String> getUniqueMateriaNamesLocal() {
  std::vector<String> out;
  auto courses = loadCourses();
  for (auto &c : courses) {
    bool found = false;
    for (auto &x : out) if (x == c.materia) { found = true; break; }
    if (!found) out.push_back(c.materia);
  }
  return out;
}

static std::vector<String> getProfessorsForMateriaLocal(const String &materia) {
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

static bool courseExistsLocal(const String &materia) {
  auto courses = loadCourses();
  for (auto &c : courses) if (c.materia == materia) return true;
  return false;
}

static int countProfessorsForMateriaLocal(const String &materia) {
  return (int)getProfessorsForMateriaLocal(materia).size();
}

static bool slotOccupiedSched(const String &day, const String &start, String *ownerOut = nullptr) {
  auto sched = loadSchedules();
  for (auto &e : sched) {
    if (e.day == day && e.start == start) {
      if (ownerOut) *ownerOut = e.materia;
      return true;
    }
  }
  return false;
}

// ---------- Vistas ----------

void handleSchedulesGrid() {
  auto schedules = loadSchedules();
  String html = htmlHeader("Horarios - Grilla");
  html += "<div class='card'><h2>Horarios del Laboratorio (LUN - SAB)</h2>";
  html += "<p class='small'>Vista de la grilla de horarios. Para editar/agregar/quitar horarios pulsa <b>Editar Horarios</b> arriba.</p>";
  html += "<table><tr><th>Hora</th>";
  for (int d = 0; d < 6; d++) html += "<th>" + String(DAYS[d]) + "</th>";
  html += "</tr>";

  for (int s = 0; s < SLOT_COUNT; s++) {
    int h = SLOT_STARTS[s];
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "%02d:00 - %02d:00", h, h + 2);
    html += "<tr><th>" + String(lbl) + "</th>";
    for (int d = 0; d < 6; d++) {
      String day = DAYS[d];
      String start = String(h) + ":00";
      String ownerMat = "";
      String ownerProf = "";
      for (auto &e : schedules) {
        if (e.day == day && e.start == start) {
          int idx = e.materia.indexOf("||");
          if (idx >= 0) {
            ownerMat = e.materia.substring(0, idx);
            ownerProf = e.materia.substring(idx + 2);
          } else {
            ownerMat = e.materia;
            ownerProf = "";
          }
          break;
        }
      }

      if (ownerMat.length() == 0) {
        html += "<td style='min-width:140px'>-</td>";
      } else {
        html += "<td style='min-width:140px'>";
        html += "<div style='display:flex;flex-direction:column;gap:4px;align-items:flex-start;'>";
        html += "<span style='padding:4px 8px;border-radius:8px;background:#eef7ed;'>" + ownerMat + "</span>";
        if (ownerProf.length()) html += "<span style='padding:3px 7px;border-radius:8px;background:#eef5ff;margin-top:2px;'>" + ownerProf + "</span>";
        html += "</div></td>";
      }
    }
    html += "</tr>";
  }

  html += "</table>";
  html += "<p style='margin-top:12px'>"
          "<a class='btn btn-green' href='/schedules/edit'>✏️ Editar Horarios</a> "
          "<a class='btn btn-blue' href='/'>Inicio</a>"
          "</p></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

void handleSchedulesEditGrid() {
  auto schedules = loadSchedules();
  String html = htmlHeader("Horarios - Editar (Global)");
  html += "<div class='card'><h2>Editar horarios (Global)</h2>";
  html += "<p class='small'>Seleccione una materia registrada para asignar al slot vacío, o elimine materias asignadas. La columna <b>Profesor</b> siempre está visible. Si una materia tiene varios profesores, deberá escoger uno; si tiene uno solo, se rellenará automáticamente.</p>";

  html += "<table><tr><th>Hora</th>";
  for (int d = 0; d < 6; d++) html += "<th>" + String(DAYS[d]) + "</th>";
  html += "</tr>";

  auto uniqueMat = getUniqueMateriaNamesLocal();

  for (int s = 0; s < SLOT_COUNT; s++) {
    int h = SLOT_STARTS[s];
    char lbl[32];
    snprintf(lbl, sizeof(lbl), "%02d:00 - %02d:00", h, h + 2);
    html += "<tr><th>" + String(lbl) + "</th>";

    for (int d = 0; d < 6; d++) {
      String day = DAYS[d];
      String start = String(h) + ":00";
      String end = String(h + 2) + ":00";
      String cellOwner;
      bool occupied = false;
      for (auto &e : schedules) {
        if (e.day == day && e.start == start) { occupied = true; cellOwner = e.materia; break; }
      }

      html += "<td style='min-width:170px;vertical-align:top;'>";

      if (occupied) {
        int idx = cellOwner.indexOf("||");
        if (idx >= 0) {
          String mat = cellOwner.substring(0, idx);
          String prof = cellOwner.substring(idx + 2);
          html += "<div style='display:flex;flex-direction:column;gap:6px;'>";
          html += "<div><strong>" + mat + "</strong></div>";
          html += "<div style='color:#114b8b;'>" + prof + "</div>";
          html += "<div style='margin-top:6px'>"
                  "<form method='POST' action='/schedules_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>"
                  "<input type='hidden' name='materia' value='" + mat + "'>"
                  "<input type='hidden' name='profesor' value='" + prof + "'>"
                  "<input type='hidden' name='day' value='" + day + "'>"
                  "<input type='hidden' name='start' value='" + start + "'>"
                  "<input class='btn btn-red' type='submit' value='Eliminar'></form></div>";
          html += "</div>";
        } else {
          html += "<div style='display:flex;flex-direction:column;gap:6px;'>";
          html += "<div><strong>" + cellOwner + "</strong></div>";
          html += "<div style='margin-top:6px'>"
                  "<form method='POST' action='/schedules_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>"
                  "<input type='hidden' name='materia' value='" + cellOwner + "'>"
                  "<input type='hidden' name='day' value='" + day + "'>"
                  "<input type='hidden' name='start' value='" + start + "'>"
                  "<input class='btn btn-red' type='submit' value='Eliminar'></form></div>";
          html += "</div>";
        }
      } else {
        html += "<form method='POST' action='/schedules_add_slot' style='display:flex;flex-direction:column;gap:6px;' onsubmit='return validateSchedForm(this)'>";
        html += "<input type='hidden' name='day' value='" + day + "'>";
        html += "<input type='hidden' name='start' value='" + start + "'>";
        html += "<input type='hidden' name='end' value='" + end + "'>";

        html += "<select name='materia' class='sched_materia_select' onchange='onSchedMateriaChange(this)'>";
        html += "<option value=''>-- Seleccionar materia --</option>";
        for (auto &m : uniqueMat) html += "<option value='" + m + "'>" + m + "</option>";
        html += "</select>";

        html += "<select name='profesor' class='sched_prof_select'><option value=''>-- Profesor --</option></select>";

        html += "<div style='display:flex;justify-content:center;'><input class='btn btn-green sched_add_btn' type='submit' value='Agregar'></div>";

        html += "</form>";
      }

      html += "</td>";
    }

    html += "</tr>";
  }

  html += "</table>";

  // JS: poblado de profesores con fallback parsing de /teachers si /profesores_for falla
  html += R"rawliteral(
    <script>
    function validateSchedForm(form){
      try {
        var mat = form.querySelector('select[name="materia"]');
        var prof = form.querySelector('select[name="profesor"]');
        if (!mat || !mat.value || mat.value.trim() === '') { alert('Seleccione una materia.'); return false; }
        if (prof && !prof.disabled) {
          if (!prof.value || prof.value.trim() === '') { alert('Seleccione un profesor para esta materia.'); return false; }
        } else {
          if (prof && (prof.value === '' || prof.value.trim() === '')) { alert('No hay profesor asignado para esta materia.'); return false; }
        }
      } catch(e){}
      return true;
    }

    function parseTeachersFromHTML(htmlText){
      try {
        var parser = new DOMParser();
        var doc = parser.parseFromString(htmlText, 'text/html');
        // la página /teachers?materia=... usa tabla id='teachers_mat_table'
        var table = doc.getElementById('teachers_mat_table');
        var out = [];
        if (table) {
          for (var r=1; r<table.rows.length; r++){
            var row = table.rows[r];
            if (!row) continue;
            var nameCell = row.cells[0];
            if (nameCell) out.push(nameCell.textContent.trim());
          }
        } else {
          // si no hay tabla, intentar extraer desde filas <tr> en la card
          var trs = doc.querySelectorAll('.card table tr');
          for (var i=1;i<trs.length;i++){
            var c = trs[i].cells[0];
            if (c) out.push(c.textContent.trim());
          }
        }
        return out;
      } catch(e) { return []; }
    }

    function onSchedMateriaChange(selectEl) {
      try {
        var mat = selectEl.value || '';
        var form = selectEl.closest('form');
        if (!form) return;
        var profSel = form.querySelector('.sched_prof_select');
        var addBtn = form.querySelector('.sched_add_btn');

        profSel.innerHTML = '<option value="">-- Profesor --</option>';
        profSel.disabled = true;
        if (addBtn) addBtn.disabled = true;

        if (!mat) return;

        // primero intentamos el endpoint JSON
        fetch('/profesores_for?materia=' + encodeURIComponent(mat), { method: 'GET' })
          .then(function(resp){
            if (!resp.ok) {
              // fallback a /teachers HTML
              return fetch('/teachers?materia=' + encodeURIComponent(mat)).then(function(r){ if (!r.ok) return null; return r.text(); });
            }
            var ct = resp.headers.get('content-type') || '';
            if (ct.indexOf('application/json') === -1) {
              return resp.text().then(function(t){ return t; });
            }
            return resp.json();
          })
          .then(function(result){
            if (!result) {
              // algo falló -> tratar como "no profesores"
              profSel.innerHTML = '<option value="">-- No hay profesores --</option>';
              profSel.disabled = true;
              if (addBtn) addBtn.disabled = true;
              return;
            }

            // si result es texto (HTML) -> parsear
            if (typeof result === 'string') {
              var profs = parseTeachersFromHTML(result || '');
              if (profs.length === 0) {
                profSel.innerHTML = '<option value="">-- No hay profesores --</option>';
                profSel.disabled = true;
                if (addBtn) addBtn.disabled = true;
                return;
              } else if (profs.length === 1) {
                profSel.innerHTML = '<option value="">-- Profesor --</option>';
                var o = document.createElement('option'); o.value = profs[0]; o.textContent = profs[0]; o.selected = true;
                profSel.appendChild(o);
                profSel.disabled = true;
                if (addBtn) addBtn.disabled = false;
                return;
              } else {
                profSel.innerHTML = '<option value="">-- Profesor --</option>';
                profs.forEach(function(p){ var o = document.createElement('option'); o.value = p; o.textContent = p; profSel.appendChild(o); });
                profSel.disabled = false;
                if (addBtn) addBtn.disabled = true;
                profSel.onchange = function(){ if (addBtn) addBtn.disabled = (this.value === '' || this.value.trim() === ''); };
                return;
              }
            }

            // si result es JSON (objeto)
            if (result.profesores !== undefined) {
              var profs = result.profesores;
              if (!profs || profs.length === 0) {
                profSel.innerHTML = '<option value="">-- No hay profesores --</option>';
                profSel.disabled = true;
                if (addBtn) addBtn.disabled = true;
              } else if (profs.length === 1) {
                profSel.innerHTML = '<option value="">-- Profesor --</option>';
                var o = document.createElement('option'); o.value = profs[0]; o.textContent = profs[0]; o.selected = true;
                profSel.appendChild(o);
                profSel.disabled = true;
                if (addBtn) addBtn.disabled = false;
              } else {
                profSel.innerHTML = '<option value="">-- Profesor --</option>';
                profs.forEach(function(p){ var o = document.createElement('option'); o.value = p; o.textContent = p; profSel.appendChild(o); });
                profSel.disabled = false;
                if (addBtn) addBtn.disabled = true;
                profSel.onchange = function(){ if (addBtn) addBtn.disabled = (this.value === '' || this.value.trim() === ''); };
              }
            } else {
              // estructura desconocida -> marcar como no profesores
              profSel.innerHTML = '<option value="">-- No hay profesores --</option>';
              profSel.disabled = true;
              if (addBtn) addBtn.disabled = true;
            }
          })
          .catch(function(err){
            // fallback final
            profSel.innerHTML = '<option value="">-- No hay profesores (error) --</option>';
            profSel.disabled = true;
            if (addBtn) addBtn.disabled = true;
          });

      } catch(e) {}
    }

    document.addEventListener('DOMContentLoaded', function(){
      var forms = document.querySelectorAll('form');
      forms.forEach(function(f){
        var btn = f.querySelector('.sched_add_btn');
        if (btn) btn.disabled = true;
      });
    });
    </script>
  )rawliteral";

  html += "<p style='margin-top:12px'>"
          "<a class='btn btn-red' href='/schedules'>Ver Horarios (solo lectura)</a> "
          "<a class='btn btn-blue' href='/'>Inicio</a>"
          "</p></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

// POST /schedules_add_slot
void handleSchedulesAddSlot() {
  if (!server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end") || !server.hasArg("materia")) {
    server.send(400, "text/plain", "faltan parametros"); return;
  }
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();
  String materia = server.arg("materia"); materia.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  profesor.trim();

  if (day.length() == 0 || start.length() == 0 || end.length() == 0 || materia.length() == 0) {
    server.send(400, "text/plain", "datos invalidos"); return;
  }

  if (!courseExistsLocal(materia)) {
    server.send(400, "text/plain", "Materia no registrada"); return;
  }

  int profCount = countProfessorsForMateriaLocal(materia);
  if (profCount > 1) {
    if (profesor.length() == 0) {
      server.send(400, "text/plain", "Seleccione un profesor para esta materia (la materia tiene varios profesores).");
      return;
    }
    bool pairExists = false;
    auto courses = loadCourses();
    for (auto &c : courses) if (c.materia == materia && c.profesor == profesor) { pairExists = true; break; }
    if (!pairExists) {
      server.send(400, "text/plain", "Curso (materia+profesor) no registrado"); return;
    }
  } else if (profCount == 1) {
    if (profesor.length() == 0) {
      auto ps = getProfessorsForMateriaLocal(materia);
      if (ps.size() == 1) profesor = ps[0];
    }
  } else {
    server.send(400, "text/plain", "No hay profesores registrados para esta materia; no se puede asignar.");
    return;
  }

  String owner;
  if (slotOccupiedSched(day, start, &owner)) {
    server.sendHeader("Location", "/schedules?msg=ocupado");
    server.send(303, "text/plain", "Slot ocupado");
    return;
  }

  String ownerKey = materia;
  if (profesor.length() > 0) ownerKey = materia + String("||") + profesor;

  addScheduleSlot(ownerKey, day, start, end);
  server.sendHeader("Location", "/schedules/edit");
  server.send(303, "text/plain", "Agregado");
}

void handleSchedulesDel() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) {
    server.send(400, "text/plain", "faltan parametros"); return;
  }
  String mat = server.arg("materia"); mat.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String profesor = server.hasArg("profesor") ? server.arg("profesor") : String();
  profesor.trim();

  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  if (!f) { server.send(500, "text/plain", "no file"); return; }

  std::vector<String> lines;
  String header = f.readStringUntil('\n');
  lines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 4) {
      String owner = c[0];
      String dayc = c[1];
      String startc = c[2];
      if (dayc == day && startc == start) {
        if (profesor.length() > 0) {
          String key = mat + String("||") + profesor;
          if (owner == key) continue;
        } else {
          if (owner == mat) continue;
        }
      }
    }
    lines.push_back(l);
  }
  f.close();
  writeAllLines(SCHEDULES_FILE, lines);
  server.sendHeader("Location", "/schedules/edit");
  server.send(303, "text/plain", "Borrado");
}

void handleSchedulesForMateriaGET() {
  if (!server.hasArg("materia")) { server.send(400, "text/plain", "materia required"); return; }
  String materia = server.arg("materia"); materia.trim();
  if (!courseExistsLocal(materia)) { server.send(404, "text/plain", "Materia no encontrada"); return; }

  auto schedules = loadSchedules();
  String title = "Horarios - " + materia;
  String html = htmlHeader(title.c_str());
  html += "<div class='card'><h2>Horarios para: " + materia + "</h2>";
  html += "<p class='small'>Aquí puede agregar o eliminar horarios únicamente por materia.</p>";

  html += "<div class='filters'>"
          "<input id='sf_day' placeholder='Filtrar día (LUN/MAR/...)'>"
          "<input id='sf_time' placeholder='Filtrar hora (07:00)'>"
          "<button class='search-btn btn btn-blue' onclick='applySchedFilters()'>Buscar</button>"
          "<button class='search-btn btn btn-green' onclick='clearSchedFilters()'>Limpiar</button>"
          "</div>";

  html += "<table id='sched_mat_table'><tr><th>Día</th><th>Inicio</th><th>Fin</th><th>Acción</th></tr>";
  for (auto &s : schedules) {
    String ownerMat = s.materia;
    int idx = ownerMat.indexOf("||");
    if (idx >= 0) ownerMat = ownerMat.substring(0, idx);
    if (ownerMat != materia) continue;
    html += "<tr><td>" + s.day + "</td><td>" + s.start + "</td><td>" + s.end + "</td>";
    html += "<td><form method='POST' action='/schedules_for_del' style='display:inline' onsubmit='return confirm(\"Eliminar este horario?\");'>"
            "<input type='hidden' name='materia' value='" + materia + "'>"
            "<input type='hidden' name='day' value='" + s.day + "'>"
            "<input type='hidden' name='start' value='" + s.start + "'>"
            "<input class='btn btn-red' type='submit' value='Eliminar'></form></td></tr>";
  }
  html += "</table>";

  html += "<h3>Añadir horario para " + materia + "</h3>";
  html += "<form method='POST' action='/schedules_for_add'>";
  html += "<input type='hidden' name='materia' value='" + materia + "'>";
  html += "Día: <select name='day'>";
  for (int d = 0; d < 6; d++) html += "<option value='" + DAYS[d] + "'>" + DAYS[d] + "</option>";
  html += "</select> Inicio (HH:MM): <input name='start' placeholder='07:00'> Fin (HH:MM): <input name='end' placeholder='09:00'> ";
  html += "<input class='btn btn-green' type='submit' value='Agregar horario'>";
  html += "</form>";

  html += "<script>"
          "function applySchedFilters(){ const table=document.getElementById('sched_mat_table'); if(!table) return; const fd=document.getElementById('sf_day').value.trim().toLowerCase(); const ft=document.getElementById('sf_time').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; const day=row.cells[0].textContent.toLowerCase(); const start=row.cells[1].textContent.toLowerCase(); const ok=(day.indexOf(fd)!==-1)&&(start.indexOf(ft)!==-1); row.style.display = ok ? '' : 'none'; } }"
          "function clearSchedFilters(){ document.getElementById('sf_day').value=''; document.getElementById('sf_time').value=''; applySchedFilters(); }"
          "</script>";

  html += "<p style='margin-top:12px'><a class='btn btn-blue' href='/'>Menu</a></p></div>" + htmlFooter();

  server.send(200, "text/html", html);
}

// POST /schedules_for_add (legacy)
void handleSchedulesForMateriaAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end")) {
    server.send(400, "text/plain", "faltan parametros"); return;
  }
  String materia = server.arg("materia"); materia.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();

  if (materia.length() == 0 || day.length() == 0 || start.length() == 0 || end.length() == 0) {
    server.send(400, "text/plain", "datos invalidos"); return;
  }
  if (!courseExistsLocal(materia)) { server.send(400, "text/plain", "Materia no registrada"); return; }

  String owner;
  if (slotOccupiedSched(day, start, &owner)) {
    server.sendHeader("Location", "/schedules?msg=ocupado");
    server.send(303, "text/plain", "Slot ocupado");
    return;
  }

  addScheduleSlot(materia, day, start, end);
  server.sendHeader("Location", "/schedules_for?materia=" + urlEncodeLocal(materia));
  server.send(303, "text/plain", "Agregado");
}

// POST /schedules_for_del
void handleSchedulesForMateriaDelPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) {
    server.send(400, "text/plain", "faltan"); return;
  }
  String mat = server.arg("materia"); mat.trim();
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();

  String owner;
  if (!slotOccupiedSched(day, start, &owner)) {
    server.send(400, "text/plain", "El horario no existe");
    return;
  }
  bool allowed = false;
  if (owner == mat) allowed = true;
  int idx = owner.indexOf("||");
  if (idx >= 0) {
    String ownerMat = owner.substring(0, idx);
    if (ownerMat == mat) allowed = true;
  }

  if (!allowed) {
    server.send(400, "text/plain", "El horario no pertenece a esta materia");
    return;
  }

  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  if (!f) { server.send(500, "text/plain", "no file"); return; }

  std::vector<String> lines;
  String header = f.readStringUntil('\n');
  lines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 4) {
      String ownerc = c[0];
      String dayc = c[1];
      String startc = c[2];
      if (dayc == day && startc == start) {
        continue;
      }
    }
    lines.push_back(l);
  }
  f.close();
  writeAllLines(SCHEDULES_FILE, lines);
  server.sendHeader("Location", "/schedules_for?materia=" + urlEncodeLocal(mat));
  server.send(303, "text/plain", "Borrado");
}
