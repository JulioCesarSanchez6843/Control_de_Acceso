// src/web_routes.cpp
#include <Arduino.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <vector>

#include "web_routes.h"
#include "files_utils.h"
#include "display.h"
#include "rfid_handler.h"
#include "globals.h"

// Helper para render header/footer (simple)
String htmlHeader(const char* title) {
  int nCount = notifCount();
  String h = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>"; h += title; h += "</title>";
  h += "<style>body{font-family:Arial;margin:0;padding:0;background:#f3f6fb}.topbar{background:#0369a1;color:#fff;padding:12px}.card{background:#fff;margin:12px;padding:12px;border-radius:8px}.btn{padding:6px 10px;border-radius:6px;text-decoration:none;color:#fff;background:#06b6d4}</style>";
  h += "</head><body><div class='topbar'><span>Control de Acceso - Laboratorio</span> <span style='float:right'><a style='color:#fff' href='/notifications'>🔔";
  if (nCount>0) h += " (" + String(nCount) + ")";
  h += "</a></span></div><div style='padding:12px'>";
  return h;
}
String htmlFooter() {
  return String("</div><div style='text-align:center;color:#777;padding:8px'>ESP32 Registro Asistencia</div></body></html>");
}

// ---------- Root ----------
void handleRoot() {
  // Aseguramos salir del modo captura por seguridad
  captureMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  String html = htmlHeader("Inicio");
  html += "<div class='card'><h2>Control del Laboratorio</h2>";
  html += "<p>Gestiona accesos, materias y horarios desde la interfaz web.</p>";
  // usamos location.replace para evitar que la página de captura quede en el historial
  html += "<a class='btn' href='#' onclick=\"window.location.replace('/capture');return false;\">Capturar tarjeta</a> ";
  html += "<a class='btn' href='/schedules'>Horarios</a> ";
  html += "<a class='btn' href='/materias'>Materias</a> ";
  html += "<a class='btn' href='/students_all'>Alumnos</a> ";
  html += "<a class='btn' href='/history'>Historial</a> ";
  html += "</div>" + htmlFooter();
  server.send(200,"text/html",html);
}

// ---------- Materias pages ----------
void handleMaterias() {
  String html = htmlHeader("Materias");
  html += "<div class='card'><h2>Materias disponibles</h2>";
  auto courses = loadCourses();
  if (courses.size()==0) html += "<p>No hay materias registradas.</p>";
  else {
    html += "<table border='1' cellpadding='6'><tr><th>Materia</th><th>Profesor</th><th>Creado</th><th>Horarios</th><th>Acción</th></tr>";
    auto schedules = loadSchedules();
    for (auto &c : courses) {
      String schedStr = "";
      for (auto &s : schedules) if (s.materia == c.materia) {
        if (schedStr.length()) schedStr += "; ";
        schedStr += s.day + " " + s.start + "-" + s.end;
      }
      if (schedStr.length()==0) schedStr = "-";
      String matE = urlEncode(c.materia);
      String matH = escapeHtml(c.materia);
      html += "<tr><td>" + matH + "</td><td>" + escapeHtml(c.profesor) + "</td><td>" + escapeHtml(c.created_at) + "</td><td>" + escapeHtml(schedStr) + "</td>";
      html += "<td><a href='/materias/edit?materia=" + matE + "'>Editar</a> ";
      html += "<a href='/schedules_for?materia=" + matE + "'>Horarios</a> ";
      html += "<a href='/students?materia=" + matE + "'>Administrar</a> ";
      html += "<form method='POST' action='/materias_delete' style='display:inline'><input type='hidden' name='materia' value='" + escapeHtml(c.materia) + "'><input type='submit' value='Eliminar'></form></td></tr>";
    }
    html += "</table>";
  }
  html += "<p><a class='btn' href='/materias/new'>Agregar nueva materia</a> <a class='btn' href='/'>Volver</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleMateriasNew() {
  String html = htmlHeader("Agregar Materia");
  html += "<div class='card'><h2>Agregar nueva materia</h2>";
  html += "<form method='POST' action='/materias_add'>Nombre materia:<br><input name='materia' required><br>Profesor:<br><input name='profesor' required><br><br><input type='submit' value='Agregar materia'></form>";
  html += "<p><a href='/materias'>Volver</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}

void handleMateriasAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("profesor")) { server.send(400,"text/plain","materia y profesor requeridos"); return; }
  String mat = server.arg("materia"); mat.trim();
  String prof = server.arg("profesor"); prof.trim();
  if (mat.length()==0) { server.send(400,"text/plain","materia vacia"); return; }
  if (courseExists(mat)) { server.sendHeader("Location","/materias?msg=duplicado"); server.send(303,"text/plain","Materia duplicada"); return; }
  addCourse(mat, prof);
  server.sendHeader("Location","/schedules_for?materia=" + urlEncode(mat));
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
  html += "<form method='POST' action='/materias_edit'><input type='hidden' name='orig_materia' value='" + escapeHtml(mat) + "'>";
  html += "Nombre materia:<br><input name='materia' value='" + escapeHtml(courses[idx].materia) + "' required><br>";
  html += "Profesor:<br><input name='profesor' value='" + escapeHtml(courses[idx].profesor) + "' required><br><br>";
  html += "<input type='submit' value='Guardar cambios'></form>";
  html += "<p><a href='/materias'>Volver</a></p></div>" + htmlFooter();
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

  // Update schedules & users
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines; slines.push_back("\"materia\",\"day\",\"start\",\"end\"");
  if (f) {
    String header = f.readStringUntil('\n');
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
  }
  writeAllLines(SCHEDULES_FILE, slines);

  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<String> ulines; ulines.push_back("\"uid\",\"name\",\"account\",\"materia\",\"created_at\"");
  if (fu) {
    String uheader = fu.readStringUntil('\n');
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
  }
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
  // convert Course to our local struct and write
  std::vector<String> cl;
  cl.push_back("\"materia\",\"profesor\",\"created_at\"");
  for (auto &c : newCourses) cl.push_back("\"" + c.materia + "\"," + "\"" + c.profesor + "\"," + "\"" + c.created_at + "\"");
  writeAllLines(COURSES_FILE, cl);

  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  std::vector<String> slines; slines.push_back("\"materia\",\"day\",\"start\",\"end\"");
  if (f) {
    String header = f.readStringUntil('\n');
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size()>=4 && c[0]==mat) continue;
      slines.push_back(l);
    }
    f.close();
  }
  writeAllLines(SCHEDULES_FILE, slines);

  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<String> ulines; ulines.push_back("\"uid\",\"name\",\"account\",\"materia\",\"created_at\"");
  if (fu) {
    String header = fu.readStringUntil('\n');
    while (fu.available()) {
      String l = fu.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size()>=4 && c[3]==mat) continue;
      ulines.push_back(l);
    }
    fu.close();
  }
  writeAllLines(USERS_FILE, ulines);

  server.sendHeader("Location","/materias");
  server.send(303,"text/plain","Deleted");
}

// ---------- Students ----------
void handleStudentsForMateria() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  String html = htmlHeader(("Alumnos - " + materia).c_str());
  html += "<div class='card'><h2>Alumnos - " + escapeHtml(materia) + "</h2>";
  auto users = usersForMateria(materia);
  if (users.size()==0) html += "<p>No hay alumnos registrados para esta materia.</p>";
  else {
    html += "<table border='1' cellpadding='6'><tr><th>UID</th><th>Nombre</th><th>Cuenta</th><th>Registro</th><th>Acciones</th></tr>";
    for (auto &ln : users) {
      auto c = parseQuotedCSVLine(ln);
      String uid = (c.size()>0?c[0]:"");
      String name = (c.size()>1?c[1]:"");
      String acc = (c.size()>2?c[2]:"");
      String created = (c.size()>4?c[4]:"");
      html += "<tr><td><pre style='margin:0'>" + escapeHtml(uid) + "</pre></td><td>" + escapeHtml(name) + "</td><td>" + escapeHtml(acc) + "</td><td>" + escapeHtml(created) + "</td>";
      html += "<td><a href='/edit?uid=" + urlEncode(uid) + "'>Editar</a> ";
      html += "<form method='POST' action='/student_remove_course' style='display:inline'><input type='hidden' name='uid' value='" + escapeHtml(uid) + "'><input type='hidden' name='materia' value='" + escapeHtml(materia) + "'><input type='submit' value='Eliminar del curso'></form></td></tr>";
    }
    html += "</table>";
  }
  html += "<p><a href='/materias'>Volver</a> <a href='/'>Menu</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleStudentsAll() {
  String html = htmlHeader("Alumnos - Todos");
  html += "<div class='card'><h2>Todos los alumnos</h2>";
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { html += "<p>No hay archivo de usuarios.</p>" + htmlFooter(); server.send(200,"text/html",html); return; }
  String header = f.readStringUntil('\n');
  struct SRec { String name; String acc; std::vector<String> mats; String created; };
  std::vector<String> uids;
  std::vector<SRec> recs;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=3) {
      String uid = c[0]; String name = c[1]; String acc = c[2]; String mat = (c.size()>3?c[3]:""); String created = (c.size()>4?c[4]:"");
      int idx=-1; for (int i=0;i<(int)uids.size();i++) if (uids[i]==uid) { idx=i; break; }
      if (idx==-1) { uids.push_back(uid); SRec r; r.name=name; r.acc=acc; r.created = created; if (mat.length()) r.mats.push_back(mat); recs.push_back(r); }
      else { if (mat.length()) recs[idx].mats.push_back(mat); }
    }
  }
  f.close();
  if (uids.size()==0) html += "<p>No hay alumnos registrados.</p>";
  else {
    html += "<table border='1' cellpadding='6'><tr><th>UID</th><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
    for (int i=0;i<(int)uids.size();i++) {
      String uid = uids[i]; SRec &r = recs[i];
      String mats="";
      for (int j=0;j<(int)r.mats.size();j++) { if (j) mats += "; "; mats += r.mats[j]; }
      if (mats.length()==0) mats = "-";
      html += "<tr><td><pre style='margin:0'>" + escapeHtml(uid) + "</pre></td><td>" + escapeHtml(r.name) + "</td><td>" + escapeHtml(r.acc) + "</td><td>" + escapeHtml(mats) + "</td><td>" + escapeHtml(r.created) + "</td><td>";
      html += "<a href='/edit?uid=" + urlEncode(uid) + "'>Editar</a> ";
      html += "<form method='POST' action='/student_delete' style='display:inline'><input type='hidden' name='uid' value='" + escapeHtml(uid) + "'><input type='submit' value='Eliminar totalmente'></form>";
      html += "</td></tr>";
    }
    html += "</table>";
  }
  html += "<p><a href='/materias'>Volver</a> <a href='/'>Menu</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleStudentRemoveCourse() {
  if (!server.hasArg("uid") || !server.hasArg("materia")) { server.send(400,"text/plain","faltan"); return; }
  String uid = server.arg("uid"); String materia = server.arg("materia");
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; lines.push_back("\"uid\",\"name\",\"account\",\"materia\",\"created_at\"");
  bool deleted=false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4 && c[0]==uid && c[3]==materia) { deleted=true; continue; }
    lines.push_back(l);
  }
  f.close();
  writeAllLines(USERS_FILE, lines);
  server.sendHeader("Location","/students?materia=" + urlEncode(materia));
  server.send(303,"text/plain","Removed");
}

void handleStudentDelete() {
  if (!server.hasArg("uid")) { server.send(400,"text/plain","faltan"); return; }
  String uid = server.arg("uid");
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; lines.push_back("\"uid\",\"name\",\"account\",\"materia\",\"created_at\"");
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

// ---------- Capture ----------
void handleCapturePage() {
  captureMode = true;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  String html = htmlHeader("Capturar Tarjeta (escuchando)");
  html += "<div class='card'><h2>Captura automática</h2>";
  html += "<p>Acerca la tarjeta ahora; si ya existe en otra materia se autocompletan Nombre y Cuenta. Selecciona Materia (obligatorio). Si la tarjeta no existe, ingresa Nombre y Cuenta (7 dígitos) y selecciona Materia.</p>";
  html += "<form id='capForm' method='POST' action='/capture_confirm'>";
  html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input id='name' name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7'><br>";
  auto courses = loadCourses();
  html += "Materia (seleccionar):<br><select id='materia' name='materia'><option value=''>-- Seleccionar materia --</option>";
  for (auto &c : courses) html += "<option value='" + escapeHtml(c.materia) + "'>" + escapeHtml(c.materia) + " (" + escapeHtml(c.profesor) + ")</option>";
  html += "</select><br>";
  html += "<div id='newMatDiv' style='display:none;margin-top:6px'>Nueva materia:<br><input id='newMateria' name='newMateria' disabled></div><br>";
  html += "<a class='btn' href='/' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</form>";

  // Script: polling + auto-submit + mejoras para cancelar captura cuando el usuario sale (pagehide/visibilitychange)
  html += "<script>\n"
          "let saved=false;\n"
          "function isAccountValid(s){ return /^[0-9]{7}$/.test(s); }\n"
          "function tryAutoSubmit(){ if(saved) return; const uid=document.getElementById('uid').value.trim(); const name=document.getElementById('name').value.trim(); const account=document.getElementById('account').value.trim(); let materia=document.getElementById('materia').value.trim(); if(materia=='__new__'){ materia=document.getElementById('newMateria').value.trim(); }\n"
          " if(uid.length==0) return; if(name.length>0 && isAccountValid(account) && materia.length>0){ saved=true; let form=new FormData(); form.append('uid',uid); form.append('name',name); form.append('account',account); form.append('materia',materia); fetch('/capture_confirm',{method:'POST',body:form}).then(r=>{ window.location='/'; }).catch(e=>{ alert('Error al guardar: '+e); saved=false; }); } }\n"
          "function poll(){ fetch('/capture_poll').then(r=>r.json()).then(j=>{ if(j.status=='waiting'){ setTimeout(poll,700); } else if(j.status=='found'){ document.getElementById('uid').value=j.uid; if(j.name) document.getElementById('name').value=j.name; if(j.account) document.getElementById('account').value=j.account; tryAutoSubmit(); setTimeout(poll,700); } else { setTimeout(poll,700); } }).catch(e=>setTimeout(poll,1200)); }\n"
          "document.addEventListener('input', function(e){ setTimeout(tryAutoSubmit,300); }); var sel=document.getElementById('materia'); if(sel) sel.addEventListener('change', function(){ if(this.value=='__new__'){ document.getElementById('newMatDiv').style.display='block'; document.getElementById('newMateria').disabled=false; } else { document.getElementById('newMatDiv').style.display='none'; document.getElementById('newMateria').disabled=true; } setTimeout(tryAutoSubmit,200); }); poll();\n"
          // stopCaptureBeacon + event listeners
          "function stopCaptureBeacon(){ try{ if(navigator.sendBeacon){ navigator.sendBeacon('/capture_stop'); } else { fetch('/capture_stop',{method:'GET', keepalive:true}).catch(()=>{}); } }catch(e){} }\n"
          "window.addEventListener('pagehide', function(){ stopCaptureBeacon(); }, false);\n"
          "document.addEventListener('visibilitychange', function(){ if(document.hidden) stopCaptureBeacon(); });\n"
          "window.addEventListener('beforeunload', function(){ stopCaptureBeacon(); });\n"
          "</script>";

  html += "</div>" + htmlFooter();
  server.send(200,"text/html",html);
}

void handleCapturePoll() {
  if (captureUID.length() == 0) {
    server.send(200,"application/json","{\"status\":\"waiting\"}");
    return;
  }
  String j = "{\"status\":\"found\",\"uid\":\"" + escapeHtml(captureUID) + "\"";
  if (captureName.length()) j += ",\"name\":\"" + escapeHtml(captureName) + "\"";
  if (captureAccount.length()) j += ",\"account\":\"" + escapeHtml(captureAccount) + "\"";
  j += "}";
  server.send(200,"application/json", j);
}

void handleCaptureConfirm() {
  if (!server.hasArg("uid") || !server.hasArg("name") || !server.hasArg("account") || !server.hasArg("materia")) {
    server.send(400,"text/plain","Faltan parametros"); return;
  }
  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.arg("materia"); materia.trim();

  if (server.hasArg("newMateria")) {
    String nm = server.arg("newMateria"); nm.trim();
    if (nm.length()>0) materia = nm;
  }

  if (uid.length()==0) { server.send(400,"text/plain","UID vacio"); return; }
  bool ok=true;
  if (account.length()!=7) ok=false;
  for (size_t i=0;i<account.length();i++) if (!isDigit(account[i])) ok=false;
  if (!ok) { server.send(400,"text/plain","Cuenta invalida"); return; }
  if (materia.length()==0) { server.send(400,"text/plain","Materia vacia"); return; }

  if (!courseExists(materia)) {
    addCourse(materia, "");
  }

  if (existsUserUidMateria(uid,materia)) {
    captureMode = false;
    server.sendHeader("Location","/capture?msg=duplicado");
    server.send(303,"text/plain","Duplicado");
    return;
  }
  String created = nowISO();
  String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"" + created + "\"";
  appendLineToFile(USERS_FILE, line);
  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura\"";
  appendLineToFile(ATT_FILE, rec);
  captureMode = false; captureUID=""; captureName=""; captureAccount=""; captureDetectedAt=0;
  server.sendHeader("Location","/");
  server.send(303,"text/plain","Usuario registrado");
}

void handleCaptureStopGET() {
  captureMode = false; captureUID=""; captureName=""; captureAccount=""; captureDetectedAt=0;
  server.sendHeader("Location","/"); server.send(303,"text/plain","stopped");
}

// ---------- Schedules (grid) ----------
void handleSchedulesGrid() {
  auto schedules = loadSchedules();
  String html = htmlHeader("Horarios - Grilla");
  html += "<div class='card'><h2>Horarios del Laboratorio (LUN - SAB)</h2>";
  html += "<table border='1' cellpadding='6'><tr><th>Hora</th>";
  const String DAYS[6] = {"LUN","MAR","MIE","JUE","VIE","SAB"};
  const int SLOT_STARTS[] = {7,9,11,13,15,17};
  for (int d=0; d<6; d++) html += "<th>" + DAYS[d] + "</th>";
  html += "</tr>";
  for (int s=0; s<6; s++) {
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
      html += "<td>" + escapeHtml(cell) + "</td>";
    }
    html += "</tr>";
  }
  html += "</table>";
  html += "<p><a href='/schedules/edit'>Editar Horarios</a> <a href='/materias'>Ver Materias</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}

// ---------- Schedules edit/add/del (global) ----------
void handleSchedulesEditGrid() {
  auto schedules = loadSchedules();
  String html = htmlHeader("Horarios - Editar (Global)");
  html += "<div class='card'><h2>Editar horarios (Global)</h2>";
  html += "<table border='1' cellpadding='6'><tr><th>Hora</th>";
  const String DAYS[6] = {"LUN","MAR","MIE","JUE","VIE","SAB"};
  const int SLOT_STARTS[] = {7,9,11,13,15,17};
  for (int d=0; d<6; d++) html += "<th>" + DAYS[d] + "</th>";
  html += "</tr>";
  auto courses = loadCourses();
  for (int s=0; s<6; s++) {
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
        html += escapeHtml(cell) + "<div style='margin-top:6px'><form method='POST' action='/schedules_del'><input type='hidden' name='materia' value='" + escapeHtml(cell) + "'><input type='hidden' name='day' value='" + escapeHtml(day) + "'><input type='hidden' name='start' value='" + escapeHtml(start) + "'><input type='submit' value='Eliminar'></form></div>";
      } else {
        html += "<form method='POST' action='/schedules_add_slot'><input type='hidden' name='day' value='" + escapeHtml(day) + "'><input type='hidden' name='start' value='" + escapeHtml(start) + "'><input type='hidden' name='end' value='" + escapeHtml(end) + "'>";
        html += "<select name='materia'><option value=''>-- Seleccionar materia --</option>";
        for (auto &c : courses) html += "<option value='" + escapeHtml(c.materia) + "'>" + escapeHtml(c.materia) + " (" + escapeHtml(c.profesor) + ")</option>";
        html += "</select> <input type='submit' value='Agregar'></form>";
      }
      html += "</td>";
    }
    html += "</tr>";
  }
  html += "</table>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleSchedulesAddSlot() {
  if (!server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end") || !server.hasArg("materia")) {
    server.send(400,"text/plain","faltan parametros"); return;
  }
  String day = server.arg("day"); day.trim();
  String start = server.arg("start"); start.trim();
  String end = server.arg("end"); end.trim();
  String materia = server.arg("materia"); materia.trim();
  if (day.length()==0 || start.length()==0 || end.length()==0 || materia.length()==0) { server.send(400,"text/plain","datos invalidos"); return; }
  if (!courseExists(materia)) { server.send(400,"text/plain","Materia no registrada. Registre la materia en Materias antes de asignarla a un horario."); return; }
  if (slotOccupied(day, start)) { server.sendHeader("Location","/schedules?msg=ocupado"); server.send(303,"text/plain","Slot ocupado"); return; }
  addScheduleSlot(materia, day, start, end);
  server.sendHeader("Location","/schedules/edit");
  server.send(303,"text/plain","Agregado");
}

void handleSchedulesDel() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) { server.send(400,"text/plain","faltan"); return; }
  String mat = server.arg("materia"); String day = server.arg("day"); String start = server.arg("start");
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; lines.push_back("\"materia\",\"day\",\"start\",\"end\"");
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

// ---------- Schedules per-materia ----------
void handleSchedulesForMateriaGET() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  if (!courseExists(materia)) { server.send(404,"text/plain","Materia no encontrada"); return; }
  auto schedules = loadSchedules();
  String html = htmlHeader(("Horarios - " + materia).c_str());
  html += "<div class='card'><h2>Horarios para: " + escapeHtml(materia) + "</h2>";
  html += "<table border='1' cellpadding='6'><tr><th>Día</th><th>Inicio</th><th>Fin</th><th>Acción</th></tr>";
  for (auto &s : schedules) {
    if (s.materia != materia) continue;
    html += "<tr><td>" + escapeHtml(s.day) + "</td><td>" + escapeHtml(s.start) + "</td><td>" + escapeHtml(s.end) + "</td>";
    html += "<td><form method='POST' action='/schedules_for_del' style='display:inline'><input type='hidden' name='materia' value='" + escapeHtml(materia) + "'><input type='hidden' name='day' value='" + escapeHtml(s.day) + "'><input type='hidden' name='start' value='" + escapeHtml(s.start) + "'><input type='submit' value='Eliminar'></form></td></tr>";
  }
  html += "</table>";
  html += "<h3>Añadir horario para " + escapeHtml(materia) + "</h3>";
  html += "<form method='POST' action='/schedules_for_add'><input type='hidden' name='materia' value='" + escapeHtml(materia) + "'>Día: <select name='day'>";
  const String DAYS2[6] = {"LUN","MAR","MIE","JUE","VIE","SAB"};
  for (int d=0; d<6; d++) html += "<option value='" + DAYS2[d] + "'>" + DAYS2[d] + "</option>";
  html += "</select> Inicio (HH:MM): <input name='start' placeholder='07:00'> Fin (HH:MM): <input name='end' placeholder='09:00'> <input type='submit' value='Agregar horario'></form>";
  html += "<p><a href='/materias'>Volver</a> <a href='/'>Menu</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleSchedulesForMateriaAddPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start") || !server.hasArg("end")) { server.send(400,"text/plain","faltan parametros"); return; }
  String materia = server.arg("materia"); String day = server.arg("day"); String start = server.arg("start"); String end = server.arg("end");
  materia.trim(); day.trim(); start.trim(); end.trim();
  if (materia.length()==0 || day.length()==0 || start.length()==0 || end.length()==0) { server.send(400,"text/plain","datos invalidos"); return; }
  if (!courseExists(materia)) { server.send(400,"text/plain","Materia no registrada"); return; }
  if (slotOccupied(day, start)) {
    server.sendHeader("Location","/schedules_for?materia=" + urlEncode(materia) + "&msg=ocupado");
    server.send(303,"text/plain","Slot ocupado");
    return;
  }
  addScheduleSlot(materia, day, start, end);
  server.sendHeader("Location","/schedules_for?materia=" + urlEncode(materia));
  server.send(303,"text/plain","Agregado");
}

void handleSchedulesForMateriaDelPOST() {
  if (!server.hasArg("materia") || !server.hasArg("day") || !server.hasArg("start")) { server.send(400,"text/plain","faltan"); return; }
  String mat = server.arg("materia"); String day = server.arg("day"); String start = server.arg("start");
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; lines.push_back("\"materia\",\"day\",\"start\",\"end\"");
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=4 && c[0]==mat && c[1]==day && c[2]==start) continue;
    lines.push_back(l);
  }
  f.close();
  writeAllLines(SCHEDULES_FILE, lines);
  server.sendHeader("Location","/schedules_for?materia=" + urlEncode(mat)); server.send(303,"text/plain","Borrado");
}

// ---------- Notifications ----------
void handleNotificationsPage() {
  String html = htmlHeader("Notificaciones");
  html += "<div class='card'><h2>Notificaciones</h2>";
  html += "<div><form method='POST' action='/notifications_clear' onsubmit='return confirm(\"Borrar todas las notificaciones? Esta acción es irreversible.\");' style='display:inline'><input type='submit' value='Borrar Notificaciones'></form> <a href='/'>Volver</a></div>";
  auto nots = readNotifications(200);
  if (nots.size()==0) html += "<p>No hay notificaciones.</p>";
  else {
    html += "<table border='1' cellpadding='6'><tr><th>Timestamp</th><th>UID</th><th>Nombre</th><th>Cuenta</th><th>Nota</th></tr>";
    for (auto &ln : nots) {
      auto c = parseQuotedCSVLine(ln);
      String ts = (c.size()>0?c[0]:"");
      String uid = (c.size()>1?c[1]:"");
      String name = (c.size()>2?c[2]:"");
      String acc = (c.size()>3?c[3]:"");
      String note = (c.size()>4?c[4]:"");
      html += "<tr><td>" + escapeHtml(ts) + "</td><td>" + escapeHtml(uid) + "</td><td>" + escapeHtml(name) + "</td><td>" + escapeHtml(acc) + "</td><td>" + escapeHtml(note) + "</td></tr>";
    }
    html += "</table>";
  }
  html += "</div>" + htmlFooter();
  server.send(200,"text/html",html);
}
void handleNotificationsClearPOST() {
  clearNotifications();
  server.sendHeader("Location","/notifications"); server.send(303,"text/plain","Notificaciones borradas");
}

// ---------- Edit user ----------
void handleEditGet() {
  if (!server.hasArg("uid")) { server.send(400,"text/plain","uid required"); return; }
  String uid = server.arg("uid");
  String line = findAnyUserByUID(uid);
  if (line.length()==0) { server.send(404,"text/plain","not found"); return; }
  auto c = parseQuotedCSVLine(line);
  String name = (c.size()>1?c[1]:"");
  String acc  = (c.size()>2?c[2]:"");
  String mat  = (c.size()>3?c[3]:"");
  String created = (c.size()>4?c[4]:"");
  String html = htmlHeader("Editar Usuario");
  html += "<div class='card'><h2>Editar Usuario</h2>";
  html += "<form method='POST' action='/edit_post'><input type='hidden' name='orig_uid' value='" + escapeHtml(uid) + "'>";
  html += "UID (no editable):<br><input value='" + escapeHtml(uid) + "' readonly><br>Name:<br><input name='name' value='" + escapeHtml(name) + "' required><br>Cuenta:<br><input name='account' value='" + escapeHtml(acc) + "' required maxlength='7' minlength='7'><br>Materia:<br>";
  auto courses = loadCourses();
  html += "<select name='materia'>";
  for (auto &c2 : courses) {
    html += "<option value='" + escapeHtml(c2.materia) + "'";
    if (c2.materia == mat) html += " selected";
    html += ">" + escapeHtml(c2.materia) + " (" + escapeHtml(c2.profesor) + ")</option>";
  }
  html += "</select><br><br><input type='submit' value='Guardar'></form>";
  html += "<p class='small'>Registrado: " + escapeHtml(created) + "</p>";
  html += "<p><a href='/students_all'>Volver</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}
void handleEditPost() {
  if (!server.hasArg("orig_uid") || !server.hasArg("name") || !server.hasArg("account") || !server.hasArg("materia")) { server.send(400,"text/plain","faltan"); return; }
  String uid = server.arg("orig_uid");
  String name = server.arg("name");
  String acc = server.arg("account");
  String mat = server.arg("materia");
  bool ok=true; if (acc.length()!=7) ok=false; for (size_t i=0;i<acc.length();i++) if (!isDigit(acc[i])) ok=false;
  if (!ok) { server.send(400,"text/plain","Cuenta invalida"); return; }
  if (!courseExists(mat)) { server.send(400,"text/plain","Materia no registrada"); return; }

  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; lines.push_back("\"uid\",\"name\",\"account\",\"materia\",\"created_at\"");
  bool found=false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (!found && c.size()>0 && c[0]==uid) {
      String created = (c.size()>4?c[4]:nowISO());
      String newL = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + acc + "\"," + "\"" + mat + "\"," + "\"" + created + "\"";
      lines.push_back(newL); found=true;
    } else lines.push_back(l);
  }
  f.close();
  if (!found) { server.send(404,"text/plain","UID not found"); return; }
  writeAllLines(USERS_FILE, lines);
  server.sendHeader("Location","/"); server.send(303,"text/plain","OK");
}

// ---------- Users CSV ----------
void handleUsersCSV() {
  if (!SPIFFS.exists(USERS_FILE)) { server.send(404,"text/plain","No users"); return; }
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  server.streamFile(f, "text/csv");
  f.close();
}

// ---------- History (view + download) ----------
void handleHistoryPage() {
  String materiaFilter = server.hasArg("materia") ? server.arg("materia") : String();
  String profFilter = server.hasArg("profesor") ? server.arg("profesor") : String();
  String html = htmlHeader("Historial de Accesos");
  html += "<div class='card'><h2>Historial de Accesos</h2>";
  html += "<p><a href='/history.csv'>Descargar TODO</a></p>";
  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  if (!f) { html += "<p>No hay historial.</p>" + htmlFooter(); server.send(200,"text/html",html); return; }
  String header = f.readStringUntil('\n');
  html += "<table border='1' cellpadding='6'><tr><th>Timestamp</th><th>UID</th><th>Nombre</th><th>Cuenta</th><th>Materia</th><th>Modo</th></tr>";
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    String ts = (c.size()>0?c[0]:"");
    String uid = (c.size()>1?c[1]:"");
    String name = (c.size()>2?c[2]:"");
    String acc = (c.size()>3?c[3]:"");
    String mat = (c.size()>4?c[4]:"");
    String mode = (c.size()>5?c[5]:"");
    if (profFilter.length()) {
      bool okProf=false;
      auto courses = loadCourses();
      for (auto &co : courses) {
        if (co.materia == mat && co.profesor.indexOf(profFilter) != -1) { okProf=true; break; }
      }
      if (!okProf) continue;
    }
    if (materiaFilter.length() && mat != materiaFilter) continue;
    html += "<tr><td>" + escapeHtml(ts) + "</td><td>" + escapeHtml(uid) + "</td><td>" + escapeHtml(name) + "</td><td>" + escapeHtml(acc) + "</td><td>" + escapeHtml(mat) + "</td><td>" + escapeHtml(mode) + "</td></tr>";
  }
  f.close();
  html += "</table>" + htmlFooter();
  server.send(200,"text/html",html);
}

void handleHistoryCSV() {
  String materiaFilter = server.hasArg("materia") ? server.arg("materia") : String();
  String dateFilter = server.hasArg("date") ? server.arg("date") : String();
  if (!SPIFFS.exists(ATT_FILE)) { server.send(404,"text/plain","no history"); return; }
  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  String out = "\"timestamp\",\"uid\",\"name\",\"account\",\"materia\",\"mode\"\r\n";
  String header = f.readStringUntil('\n');
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    String ts = (c.size()>0?c[0]:"");
    String mat = (c.size()>4?c[4]:"");
    if (materiaFilter.length() && mat != materiaFilter) continue;
    if (dateFilter.length()) {
      if (ts.indexOf(dateFilter) != 0) continue;
    }
    out += l + "\r\n";
  }
  f.close();
  server.sendHeader("Content-Disposition","attachment; filename=history.csv");
  server.send(200,"text/csv",out);
}

void handleHistoryClearPOST() {
  writeAllLines(ATT_FILE, std::vector<String>{String("\"timestamp\",\"uid\",\"name\",\"account\",\"materia\",\"mode\"")});
  server.sendHeader("Location","/history"); server.send(303,"text/plain","Historial borrado");
}

// ---------- Materia history by days ----------
void handleMateriaHistoryGET() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  if (!courseExists(materia)) { server.send(404,"text/plain","Materia no encontrada"); return; }
  std::vector<String> dates;
  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  if (f) {
    String header = f.readStringUntil('\n');
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size()>4 && c[4]==materia) {
        String ts = c[0];
        String day = ts.substring(0,10);
        bool found=false;
        for (auto &d : dates) if (d==day) { found=true; break; }
        if (!found) dates.push_back(day);
      }
    }
    f.close();
  }
  String html = htmlHeader(("Historial por días - " + materia).c_str());
  html += "<div class='card'><h2>Historial por días - " + escapeHtml(materia) + "</h2>";
  if (dates.size()==0) html += "<p>No hay registros para esta materia.</p>";
  else {
    html += "<ul>";
    for (auto &d : dates) html += "<li>" + escapeHtml(d) + " <a href='/history.csv?materia=" + urlEncode(materia) + "&date=" + urlEncode(d) + "'>Descargar CSV</a></li>";
    html += "</ul>";
  }
  html += "<p><a href='/materias'>Volver</a> <a href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

// ---------- Status (simple) ----------
void handleStatus() {
  String j = "{\"capture\":" + String(captureMode ? "true" : "false") + ",\"ip\":\"" + (WiFi.localIP().toString()) + "\",\"notifs\":" + String(notifCount()) + "}";
  server.send(200,"application/json", j);
}
