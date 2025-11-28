// src/web/edit.cpp
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>
#include <vector>

#include "globals.h"
#include "web_common.h"
#include "files_utils.h"
#include "edit.h"
#include "courses.h"    // loadCourses(), writeCourses()
#include "schedules.h"  // SCHEDULES_FILE (si lo usas) - opcional, solo para consistencia

// ---------------- utilidades locales ----------------
static String htmlEscapeLocal(const String &s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

static String sanitizeReturnToLocal(const String &rt) {
  if (rt.length() > 0 && rt[0] == '/') return rt;
  return String("/students_all");
}

static bool uidExistsInFile(const char* filename, const String &uid) {
  if (!uid.length()) return false;
  if (!SPIFFS.exists(filename)) return false;
  File f = SPIFFS.open(filename, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 1 && c[0] == uid) { f.close(); return true; }
  }
  f.close();
  return false;
}
static bool uidExistsInUsers(const String &uid) { return uidExistsInFile(USERS_FILE, uid); }
static bool uidExistsInTeachers(const String &uid) { return uidExistsInFile(TEACHERS_FILE, uid); }

static std::pair<String,String> findByAccountLocal(const String &account) {
  if (account.length() == 0) return std::make_pair(String(""), String(""));

  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  if (fu) {
    while (fu.available()) {
      String l = fu.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
      auto p = parseQuotedCSVLine(l);
      if (p.size() >= 3) {
        if (p[2] == account) { fu.close(); return std::make_pair(p[0], String("users")); }
      }
    }
    fu.close();
  }

  File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (ft) {
    while (ft.available()) {
      String l = ft.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
      auto p = parseQuotedCSVLine(l);
      if (p.size() >= 3) {
        if (p[2] == account) { ft.close(); return std::make_pair(p[0], String("teachers")); }
      }
    }
    ft.close();
  }

  return std::make_pair(String(""), String(""));
}

// ---------------- render / lógica compartida ----------------
// Renderiza la página de edición (utilizada por /edit)
static void renderEditPage(const String &uid, const String &return_to_in, const String &origin_path) {
  String return_to = sanitizeReturnToLocal(return_to_in);

  // Buscar en users y teachers
  bool found = false;
  String foundName = "", foundAccount = "", foundCreated = "";
  String source = "users";
  std::vector<String> foundMaterias; // para alumnos: todas las materias asociadas

  // leer USERS_FILE (acumular todas las filas que tengan uid)
  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  if (fu) {
    String header = fu.readStringUntil('\n'); (void)header;
    while (fu.available()) {
      String l = fu.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 1 && c[0] == uid) {
        if (!found) {
          foundName = (c.size() > 1 ? c[1] : "");
          foundAccount = (c.size() > 2 ? c[2] : "");
          foundCreated = (c.size() > 4 ? c[4] : nowISO());
          found = true;
          source = "users";
        }
        String mat = (c.size() > 3 ? c[3] : "");
        foundMaterias.push_back(mat);
      }
    }
    fu.close();
  }

  // si no encontrado en users, buscar en teachers (única fila esperada)
  if (!found) {
    File ft = SPIFFS.open(TEACHERS_FILE, FILE_READ);
    if (ft) {
      String header = ft.readStringUntil('\n'); (void)header;
      while (ft.available()) {
        String l = ft.readStringUntil('\n'); l.trim();
        if (!l.length()) continue;
        auto c = parseQuotedCSVLine(l);
        if (c.size() >= 1 && c[0] == uid) {
          foundName = (c.size() > 1 ? c[1] : "");
          foundAccount = (c.size() > 2 ? c[2] : "");
          foundCreated = (c.size() > 4 ? c[4] : nowISO());
          found = true;
          source = "teachers";
          break;
        }
      }
      ft.close();
    }
  }

  if (!found) { server.send(404, "text/plain", "Usuario no encontrado"); return; }

  // cargar materias disponibles
  auto courses = loadCourses();
  std::vector<String> materias;
  for (auto &c : courses) {
    bool ok = true;
    for (auto &m : materias) if (m == c.materia) { ok = false; break; }
    if (ok) materias.push_back(c.materia);
  }

  // Normalizar foundMaterias (eliminar vacíos duplicados)
  std::vector<String> normalMaterias;
  for (auto &m : foundMaterias) {
    if (m.length() == 0) continue;
    bool ok = true;
    for (auto &x : normalMaterias) if (x == m) { ok = false; break; }
    if (ok) normalMaterias.push_back(m);
  }

  // Determinar action del formulario según la ruta de llamada (default /edit_post)
  String formAction = "/edit_post";
  if (origin_path == "/capture_edit") formAction = "/capture_edit_post";

  // Preparar posible mensaje inicial (err param)
  String initialWarnJS = "";
  if (server.hasArg("err")) {
    String e = server.arg("err");
    String msg = "";
    if (e == "prof_required") msg = "Una de las materias seleccionadas no tiene profesor asignado. Por favor seleccione un profesor para cada materia.";
    else if (e == "materia_required") msg = "Debe seleccionar al menos una materia antes de guardar.";
    if (msg.length()) {
      // escape for JS string
      msg.replace("\\", "\\\\");
      msg.replace("\"", "\\\"");
      msg.replace("\n", "\\n");
      initialWarnJS = msg;
    }
  }

  // Construir HTML (estilo similar a capture_individual)
  String html = htmlHeader("Editar Usuario");
  html += R"rawliteral(
<style>
/* Card / grid */
.edit-card { max-width:900px; margin:10px auto; padding:16px; }
.form-grid { display:grid; grid-template-columns: 1fr 1fr; gap:12px; align-items:start; }
.form-row{ display:flex; flex-direction:column; }
.form-row.full{ grid-column:1 / -1; }

/* Labels / inputs */
label.small{ font-size:0.9rem; color:#1f2937; margin-bottom:6px; font-weight:600; }
input[type="text"], input[type="tel"], select, input[readonly] { padding:10px 12px; border-radius:8px; border:1px solid #e6eef6; background:#fff; font-size:0.95rem; }
input[readonly]{ background:#f8fafc; color:#274151; }

/* Materia rows */
.materia-list { margin-top:8px; display:flex; flex-direction:column; gap:8px; }
.materia-row { display:flex; gap:8px; align-items:center; }
.materia-row select { min-width:160px; }

/* Small inline buttons */
.smallbtn { padding:6px 8px; border-radius:6px; text-decoration:none; cursor:pointer; }

/* Warn box */
.warn { display:none; border-radius:8px; padding:10px; margin-top:10px; font-weight:600; max-width:100%; box-sizing:border-box; }

/* Bottom actions: smaller buttons like capture */
.form-actions { display:flex; gap:8px; justify-content:center; margin-top:14px; grid-column:1 / -1; }
.form-actions .btn { padding:8px 10px; font-size:0.92rem; border-radius:6px; min-width:110px; }
@media (max-width:720px) { .form-grid { grid-template-columns:1fr; } .edit-card{ margin:8px; } .materia-row { flex-direction:column; align-items:stretch; } }
</style>
)rawliteral";

  html += "<div class='card edit-card'><h2>Editar Usuario</h2>";
  html += "<form method='POST' action='" + htmlEscapeLocal(formAction) + "' class='form-grid' id='editForm' novalidate>";

  html += "<div class='form-row full'><label class='small'>UID (no editable):</label>";
  html += "<input readonly value='" + htmlEscapeLocal(uid) + "'></div>";

  html += "<div class='form-row'><label class='small'>Nombre:</label>";
  html += "<input id='fld_name' name='name' required value='" + htmlEscapeLocal(foundName) + "'></div>";

  html += "<div class='form-row'><label class='small'>Cuenta (7 dígitos):</label>";
  html += "<input id='fld_account' name='account' required maxlength='7' minlength='7' value='" + htmlEscapeLocal(foundAccount) + "'></div>";

  if (source == "users") {
    html += "<div class='form-row full'><label class='small'>Materias asignadas (puede agregar varias):</label>";
    html += "<div id='materias_container' class='materia-list'></div>";
    html += "<div style='margin-top:8px;display:flex;gap:8px;align-items:center;'><button type='button' id='addMateriaBtn' class='btn btn-blue'>➕ Agregar materia</button><span class='small' style='margin-left:8px;color:#475569;'>Seleccione al menos una materia antes de guardar.</span></div>";
    html += "</div>";

    // contador y template data
    html += "<input type='hidden' id='mat_count' name='materias_count' value='0'>";
    // JS array con materias disponibles
    html += "<script>var __availableMaterias = [";
    for (size_t i = 0; i < materias.size(); ++i) {
      if (i) html += ",";
      html += "\"" + htmlEscapeLocal(materias[i]) + "\"";
    }
    html += "];\n</script>";
  } else {
    html += "<input type='hidden' name='materia' value=''>\n";
    html += "<input type='hidden' name='profesor' value=''>\n";
    html += "<div class='form-row full'><p class='small'>Este usuario es un maestro; las materias se gestionan por separado.</p></div>";
  }

  // campo warn (para mensajes en cliente)
  html += "<div id='warn' class='warn'></div>";

  // hidden meta fields
  html += "<input type='hidden' name='orig_uid' value='" + htmlEscapeLocal(uid) + "'>";
  html += "<input type='hidden' name='source' value='" + htmlEscapeLocal(source) + "'>";
  html += "<input type='hidden' name='return_to' value='" + htmlEscapeLocal(return_to) + "'>";

  html += "<div class='form-row full'><label class='small'>Registrado:</label>";
  html += "<div style='padding:8px;background:#f5f7f5;border-radius:6px;'>" + htmlEscapeLocal(foundCreated) + "</div></div>";

  // bottom actions (small)
  html += "<div class='form-actions'>";
  html += "<button type='submit' id='saveBtn' class='btn btn-green'>Guardar</button>";
  html += "<a class='btn btn-red' href='" + htmlEscapeLocal(return_to) + "'>Cancelar</a>";
  html += "</div>";

  html += "</form></div>" + htmlFooter();

  // inyectar flags JS: si es edición de alumno (users) o no, y mensaje inicial si viene por err
  {
    String jsFlags = "<script>\n";
    jsFlags += "var __isUserEdit = ";
    jsFlags += (source == "users") ? "true" : "false";
    jsFlags += ";\n";
    if (initialWarnJS.length()) {
      jsFlags += "var __initial_warn = \"" + initialWarnJS + "\";\n";
    } else {
      jsFlags += "var __initial_warn = null;\n";
    }
    jsFlags += "</script>\n";
    html += jsFlags;
  }

  // JS dinámico: gestionar filas de materia y profesores + validación obligatoria de al menos 1 materia
  html += R"rawliteral(
<script>
function createElem(tag, attrs, text) {
  var e = document.createElement(tag);
  if (attrs) {
    for (var k in attrs) {
      if (k === 'class') e.className = attrs[k];
      else if (k === 'html') e.innerHTML = attrs[k];
      else e.setAttribute(k, attrs[k]);
    }
  }
  if (text) e.textContent = text;
  return e;
}
function fetchProfesores(materia, cb) {
  fetch('/profesores_for?materia=' + encodeURIComponent(materia))
    .then(r => r.json())
    .then(j => { if (cb) cb(j && j.profesores ? j.profesores : []); })
    .catch(e => { if (cb) cb([]); });
}
var materiasContainer=null, matCountInput=null, saveBtn=null, warnBox=null;
function setWarn(msg){
  if(!warnBox) return;
  if(msg && msg.length){
    warnBox.textContent = msg;
    warnBox.style.display = 'block';
    warnBox.style.background = '#fff7ed';
    warnBox.style.border = '1px solid #ffd8a8';
    warnBox.style.color = '#7b2e00';
    warnBox.style.padding = '10px';
    warnBox.style.borderRadius = '6px';
  } else {
    warnBox.style.display = 'none';
    warnBox.textContent = '';
  }
}

function updateSaveButtonState(){
  // Si no es edición de alumno, saltar validación de materias
  if (typeof __isUserEdit === 'undefined' || !__isUserEdit) {
    if (saveBtn) saveBtn.disabled = false;
    setWarn('');
    return;
  }

  var ok = true;
  var rows = materiasContainer ? materiasContainer.querySelectorAll('.materia-row') : [];
  var selectedCount = 0;
  var pending = 0;

  for (var i=0;i<rows.length;i++){
    (function(r){
      var matSel = r.querySelector('select[name^="materia_"]');
      var profSel = r.querySelector('select[name^="profesor_"]');
      if (!matSel) return;
      var mv = matSel.value || '';
      if (!mv) return; // empty materia row - ignored
      selectedCount++;
      var c = profSel ? profSel.getAttribute('data-prof-count') : null;
      if (c !== null) {
        if (parseInt(c,10) >= 2 && !profSel.value) ok = false;
      } else {
        pending++;
        fetchProfesores(mv, function(list){
          pending--;
          if (list.length >= 2) {
            profSel.setAttribute('data-prof-count', String(list.length));
            if (!profSel.value) ok = false;
          } else if (list.length === 1) {
            // single professor: set it but DO NOT disable the select
            profSel.innerHTML = '';
            var o = createElem('option',{ 'value': list[0] }, list[0]);
            profSel.appendChild(o);
            profSel.value = list[0];
            profSel.setAttribute('data-prof-count','1');
          } else {
            profSel.innerHTML = '';
            profSel.appendChild(createElem('option',{ 'value':'' }, '-- Ninguno --'));
            profSel.setAttribute('data-prof-count','0');
            ok = false;
          }
          if (pending === 0) {
            saveBtn.disabled = !(ok && selectedCount > 0);
            if (!saveBtn.disabled) setWarn('');
            else if (selectedCount === 0) setWarn('Debe seleccionar al menos una materia antes de guardar.');
            else setWarn('Complete los profesores requeridos para las materias seleccionadas.');
          }
        });
      }
    })(rows[i]);
  }

  if (pending === 0) {
    saveBtn.disabled = !(ok && selectedCount > 0);
    if (!saveBtn.disabled) setWarn('');
    else if (selectedCount === 0) setWarn('Debe seleccionar al menos una materia antes de guardar.');
    else setWarn('Complete los profesores requeridos para las materias seleccionadas.');
  } else {
    saveBtn.disabled = true;
    if (selectedCount === 0) setWarn('Debe seleccionar al menos una materia antes de guardar.');
  }
}

function addMateriaRow(materia, profesor){
  var idx = parseInt(matCountInput.value||"0",10);
  var row = createElem('div',{ 'class':'materia-row' });
  var matSel = createElem('select',{ 'name':'materia_' + idx });
  matSel.appendChild(createElem('option',{ 'value':''}, '-- Ninguna --'));
  if (typeof __availableMaterias !== 'undefined') {
    for (var i=0;i<__availableMaterias.length;i++){
      var o = createElem('option',{ 'value': __availableMaterias[i] }, __availableMaterias[i]);
      if (materia && materia === __availableMaterias[i]) o.selected = true;
      matSel.appendChild(o);
    }
  }
  var profSel = createElem('select',{ 'name':'profesor_' + idx });
  profSel.appendChild(createElem('option',{ 'value':'' }, '-- Ninguno --'));
  // NOTE: do NOT disable profSel when single professor; keep enabled so it is submitted

  var rm = createElem('button',{ 'type':'button', 'class':'smallbtn btn btn-red' }, 'Eliminar');
  rm.addEventListener('click', function(){
    row.remove();
    var rows = materiasContainer.querySelectorAll('.materia-row');
    for (var r=0;r<rows.length;r++){
      var ms = rows[r].querySelector('select[name^="materia_"]');
      var ps = rows[r].querySelector('select[name^="profesor_"]');
      if (ms) ms.name = 'materia_' + r;
      if (ps) ps.name = 'profesor_' + r;
    }
    matCountInput.value = String(rows.length);
    updateSaveButtonState();
  });

  matSel.addEventListener('change', function(){
    var val = matSel.value || '';
    profSel.innerHTML = '';
    profSel.appendChild(createElem('option',{ 'value':'' }, '-- Ninguno --'));
    profSel.removeAttribute('data-prof-count');
    if (!val) { updateSaveButtonState(); return; }
    fetchProfesores(val, function(list){
      if (!list || list.length === 0) {
        profSel.disabled = true;
        profSel.setAttribute('data-prof-count','0');
      } else if (list.length === 1) {
        // single professor: assign it but DO NOT disable the select
        profSel.innerHTML = '';
        var o = createElem('option',{ 'value': list[0] }, list[0]);
        profSel.appendChild(o);
        profSel.value = list[0];
        profSel.removeAttribute('disabled');
        profSel.setAttribute('data-prof-count','1');
      } else {
        profSel.innerHTML = '';
        for (var i=0;i<list.length;i++){
          var o = createElem('option',{ 'value': list[i] }, list[i]);
          profSel.appendChild(o);
        }
        profSel.removeAttribute('disabled');
        profSel.setAttribute('data-prof-count', String(list.length));
      }
      if (profesor && profesor.length) {
        for (var i=0;i<profSel.options.length;i++) {
          if (profSel.options[i].value == profesor) { profSel.value = profesor; break; }
        }
      }
      updateSaveButtonState();
    });
  });
  profSel.addEventListener('change', updateSaveButtonState);

  row.appendChild(matSel);
  row.appendChild(profSel);
  row.appendChild(rm);
  if (materiasContainer) materiasContainer.appendChild(row);
  matCountInput.value = String(parseInt(matCountInput.value||"0",10) + 1);

  if (matSel.value) {
    matSel.dispatchEvent(new Event('change'));
  } else updateSaveButtonState();
}

document.addEventListener('DOMContentLoaded', function(){
  materiasContainer = document.getElementById('materias_container');
  matCountInput = document.getElementById('mat_count');
  saveBtn = document.getElementById('saveBtn');
  warnBox = document.getElementById('warn');
  var addBtn = document.getElementById('addMateriaBtn');
  if (addBtn) addBtn.addEventListener('click', function(){ addMateriaRow('', ''); });

  // existing materias provided by server are embedded below (server will inject array)
  var existing = [
)rawliteral";

  // embed existing materias as JS array
  for (size_t i = 0; i < normalMaterias.size(); ++i) {
    String m = normalMaterias[i];
    String esc = m;
    esc.replace("\\", "\\\\");
    esc.replace("\"", "\\\"");
    html += "\"" + esc + "\"";
    if (i + 1 < normalMaterias.size()) html += ",";
  }

  html += R"rawliteral(
  ];

  // If edit is for students, populate existing materia rows; otherwise skip
  if (typeof __isUserEdit !== 'undefined' && __isUserEdit) {
    for (var i=0;i<existing.length;i++){
      addMateriaRow(existing[i], '');
    }
    updateSaveButtonState();
  } else {
    // For teachers: ensure Save enabled and no warnings
    if (saveBtn) saveBtn.disabled = false;
    setTimeout(function(){ setWarn(''); }, 20);
  }

  // if server indicated initial warning, show it
  if (typeof __initial_warn !== 'undefined' && __initial_warn) {
    setTimeout(function(){ setWarn(__initial_warn); }, 50);
  }

  var form = document.getElementById('editForm');
  form.addEventListener('submit', function(ev){
    // If editing teachers skip materia validation
    if (typeof __isUserEdit === 'undefined' || !__isUserEdit) {
      return; // allow submit
    }
    updateSaveButtonState();
    // if saveBtn disabled: show client-side HTML message (warn box) and prevent submit
    if (saveBtn.disabled) {
      ev.preventDefault();
      setWarn('No puede guardar: seleccione al menos 1 materia y complete profesores requeridos.');
      if (materiasContainer) materiasContainer.scrollIntoView({behavior:'smooth', block:'center'});
      return false;
    }
    // otherwise submit normally
  });
});
</script>
)rawliteral";

  server.send(200, "text/html", html);
}

// ---------------- handlers públicos ----------------

// GET /edit
void handleEditGet() {
  if (!server.hasArg("uid")) { server.send(400, "text/plain", "uid required"); return; }
  String uid = server.arg("uid");
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : String();
  renderEditPage(uid, return_to, server.uri());
}

// POST shared: procesar el formulario (tanto /edit_post)
static void processEditPostAndRedirect(const String &redirect_to) {
  // aceptar orig_uid o uid para compatibilidad
  String uid;
  if (server.hasArg("orig_uid")) uid = server.arg("orig_uid");
  else if (server.hasArg("uid")) uid = server.arg("uid");
  uid.trim();

  String name = server.hasArg("name") ? server.arg("name") : String(); name.trim();
  String account = server.hasArg("account") ? server.arg("account") : String(); account.trim();
  String source = server.hasArg("source") ? server.arg("source") : String(); source.trim();
  String return_to = server.hasArg("return_to") ? server.arg("return_to") : String(); return_to = sanitizeReturnToLocal(return_to);

  if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }
  if (name.length() == 0) { server.send(400, "text/plain", "Nombre vacío"); return; }
  if (account.length() != 7) { server.send(400, "text/plain", "Cuenta inválida"); return; }
  for (size_t i = 0; i < account.length(); ++i) if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }

  if (!(source == "users" || source == "teachers")) {
    // intentar inferir por archivos
    if (uidExistsInTeachers(uid)) source = "teachers";
    else source = "users";
  }

  // comprobar duplicado de cuenta
  auto accFound = findByAccountLocal(account);
  if (accFound.first.length() && accFound.first != uid) {
    server.send(400, "text/plain", "Cuenta duplicada con otro usuario");
    return;
  }

  // si users: procesar materias múltiples
  if (source == "users") {
    int mcount = 0;
    if (server.hasArg("materias_count")) {
      mcount = server.arg("materias_count").toInt();
      if (mcount < 0) mcount = 0;
      if (mcount > 200) mcount = 200;
    }

    std::vector<std::pair<String,String>> materias;
    for (int i = 0; i < mcount; ++i) {
      String mk = String("materia_") + String(i);
      String pk = String("profesor_") + String(i);
      String mval = server.hasArg(mk) ? server.arg(mk) : String();
      String pval = server.hasArg(pk) ? server.arg(pk) : String();
      mval.trim(); pval.trim();
      if (mval.length() == 0) continue;
      // Si profesor vacío -> redirigir a edición con mensaje (evitamos la página emergente "Falta profesor")
      if (pval.length() == 0) {
        String loc = "/edit?uid=" + uid + "&err=prof_required";
        server.sendHeader("Location", loc);
        server.send(303, "text/plain", "prof required");
        return;
      }
      materias.push_back(std::make_pair(mval, pval));
    }

    // En EDIT de alumno: es obligatorio tener al menos UNA materia seleccionada
    if (materias.size() == 0) {
      String loc = "/edit?uid=" + uid + "&err=materia_required";
      server.sendHeader("Location", loc);
      server.send(303, "text/plain", "materia required");
      return;
    }

    // leer USERS_FILE y construir nuevo contenido
    File f = SPIFFS.open(USERS_FILE, FILE_READ);
    std::vector<String> lines;
    String header = "";
    if (f) {
      header = f.readStringUntil('\n');
      lines.push_back(header);
      while (f.available()) {
        String l = f.readStringUntil('\n'); l.trim();
        if (!l.length()) continue;
        auto c = parseQuotedCSVLine(l);
        if (c.size() >= 1 && c[0] == uid) {
          // omitimos las filas antiguas de este uid
          continue;
        }
        lines.push_back(l);
      }
      f.close();
    } else {
      header = "\"uid\",\"name\",\"account\",\"materia\",\"created\"";
      lines.push_back(header);
    }

    // intentar conservar created timestamp si existía
    String created = nowISO();
    File fu2 = SPIFFS.open(USERS_FILE, FILE_READ);
    if (fu2) {
      String h = fu2.readStringUntil('\n'); (void)h;
      while (fu2.available()) {
        String l = fu2.readStringUntil('\n'); l.trim();
        if (!l.length()) continue;
        auto c = parseQuotedCSVLine(l);
        if (c.size() >= 1 && c[0] == uid) {
          if (c.size() > 4 && c[4].length()) { created = c[4]; break; }
        }
      }
      fu2.close();
    }

    // agregar filas por materia
    for (auto &mp : materias) {
      String newline = "\"" + uid + "\",\"" + name + "\",\"" + account + "\",\"" + mp.first + "\",\"" + created + "\"";
      lines.push_back(newline);
    }

    if (!writeAllLines(USERS_FILE, lines)) { server.send(500, "text/plain", "Error guardando usuarios"); return; }

    server.sendHeader("Location", return_to);
    server.send(303, "text/plain", "Updated");
    return;
  }

  // flujo teachers (única fila)
  if (source == "teachers") {
    const char* targetFile = TEACHERS_FILE;
    File f = SPIFFS.open(targetFile, FILE_READ);
    if (!f) { server.send(500, "text/plain", "no file"); return; }
    std::vector<String> lines;
    String header = f.readStringUntil('\n'); lines.push_back(header);
    bool updated = false;
    String oldName = "";

    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 1 && c[0] == uid) {
        if (c.size() > 1) oldName = c[1];
        String created = (c.size() > 4 ? c[4] : nowISO());
        String newline = "\"" + uid + "\",\"" + name + "\",\"" + account + "\",\"\",\"" + created + "\"";
        lines.push_back(newline);
        updated = true;
      } else lines.push_back(l);
    }
    f.close();

    if (!updated) { server.send(404, "text/plain", "Usuario no encontrado"); return; }
    if (!writeAllLines(targetFile, lines)) { server.send(500, "text/plain", "Error guardando"); return; }

    // Propagar renombre si es necesario
    if (oldName.length() && oldName != name) {
      // update courses
      auto courses = loadCourses();
      bool changed = false;
      for (auto &c : courses) {
        if (c.profesor == oldName) { c.profesor = name; changed = true; }
      }
      if (changed) writeCourses(courses);

      // update schedules file owner fields if format "materia||profesor"
      File fs = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
      if (fs) {
        std::vector<String> slines;
        String sheader = fs.readStringUntil('\n'); slines.push_back(sheader);
        while (fs.available()) {
          String l = fs.readStringUntil('\n'); l.trim();
          if (!l.length()) continue;
          auto p = parseQuotedCSVLine(l);
          if (p.size() >= 4) {
            String owner = p[0];
            int idx = owner.indexOf("||");
            if (idx >= 0) {
              String ownerMat = owner.substring(0, idx);
              String ownerProf = owner.substring(idx + 2);
              if (ownerProf == oldName) {
                String newOwner = ownerMat + String("||") + name;
                String day = p[1];
                String start = p[2];
                String rest = p[3];
                String newline = "\"" + newOwner + "\"," + "\"" + day + "\"," + "\"" + start + "\"," + "\"" + rest + "\"";
                slines.push_back(newline);
                continue;
              }
            }
          }
          slines.push_back(l);
        }
        fs.close();
        writeAllLines(SCHEDULES_FILE, slines);
      }
    }

    server.sendHeader("Location", return_to);
    server.send(303, "text/plain", "Updated");
    return;
  }

  server.send(400, "text/plain", "source inválido");
}

// POST /edit_post
void handleEditPost() {
  processEditPostAndRedirect("/students_all");
}
