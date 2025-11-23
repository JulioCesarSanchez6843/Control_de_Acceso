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

// urlEncode
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
      char buf[4];
      snprintf(buf, sizeof(buf), "%%%02X", (uint8_t)c);
      ret += buf;
    }
  }
  return ret;
}

static std::vector<String> teachersForMateriaLocal(const String &materia) {
  std::vector<String> out;
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (!f) return out;
  String header = f.readStringUntil('\n'); (void)header;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 4) {
      String mat = c[3];
      if (mat == materia) out.push_back(l);
    }
  }
  f.close();
  return out;
}

void handleTeachersForMateria() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  String html = htmlHeader(("Maestros - " + materia).c_str());
  html += "<div class='card'><h2>Maestros - " + materia + "</h2>";

  html += "<div class='filters'><input id='tf_name' placeholder='Filtrar Nombre'><input id='tf_acc' placeholder='Filtrar Cuenta'><button class='search-btn btn btn-blue' onclick='applyTeacherFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearTeacherFilters()'>Limpiar</button></div>";

  auto users = teachersForMateriaLocal(materia);
  if (users.size() == 0) {
    html += "<p>No hay maestros registrados para esta materia.</p>";
  } else {
    html += "<table id='teachers_mat_table'><tr><th>Nombre</th><th>Cuenta</th><th>Registro</th><th>Acciones</th></tr>";
    for (auto &ln : users) {
      auto c = parseQuotedCSVLine(ln);
      String uid = (c.size() > 0 ? c[0] : "");
      String name = (c.size() > 1 ? c[1] : "");
      String acc = (c.size() > 2 ? c[2] : "");
      String created = (c.size() > 4 ? c[4] : nowISO());

      html += "<tr><td>" + name + "</td><td>" + acc + "</td><td>" + created + "</td>";
      html += "<td>";
      html += "<form method='POST' action='/teacher_remove_course' style='display:inline' onsubmit='return confirm(\"Eliminar este maestro de la materia?\");'>";
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
  String html = htmlHeader("Maestros - Todos");
  html += "<div class='card'><h2>Todos los maestros</h2>";

  // Top area: filters + capture individual button (teachers)
  html += "<div style='display:flex;justify-content:space-between;align-items:center;gap:12px;'>";
  html += "<div class='filters'><input id='ta_name' placeholder='Filtrar Nombre'><input id='ta_acc' placeholder='Filtrar Cuenta'><input id='ta_mat' placeholder='Filtrar Materia'><button class='search-btn btn btn-blue' onclick='applyAllTeacherFilters()'>Buscar</button><button class='search-btn btn btn-green' onclick='clearAllTeacherFilters()'>Limpiar</button></div>";
  html += "<div style='display:flex;gap:8px;align-items:center;'>";
  html += "<a class='btn btn-blue' href='/capture_individual?target=teachers'>🎴 Capturar Maestro (Individual)</a>";
  html += "</div></div>";

  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (!f) { html += "<p>No hay archivo de maestros.</p>"; html += htmlFooter(); server.send(200,"text/html",html); return; }

  String header = f.readStringUntil('\n');
  struct TRec { String name; String acc; std::vector<String> mats; String created; };
  std::vector<String> uids;
  std::vector<TRec> recs;

  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 3) {
      String uid = c[0]; String name = c[1]; String acc = c[2]; String mat = (c.size() > 3 ? c[3] : ""); String created = (c.size() > 4 ? c[4] : nowISO());
      int idx=-1; for (int i=0;i<(int)uids.size();i++) if (uids[i]==uid) { idx=i; break; }
      if (idx==-1) { uids.push_back(uid); TRec r; r.name=name; r.acc=acc; r.created = created; if (mat.length()) r.mats.push_back(mat); recs.push_back(r); }
      else { if (mat.length()) recs[idx].mats.push_back(mat); }
    }
  }
  f.close();

  if (uids.size()==0) html += "<p>No hay maestros registrados.</p>";
  else {
    html += "<table id='teachers_all_table' style='margin-top:12px;'><tr><th>Nombre</th><th>Cuenta</th><th>Materias</th><th>Registro</th><th>Acciones</th></tr>";
    for (int i=0;i<(int)uids.size();i++) {
      TRec &r = recs[i];
      String mats="";
      for (int j=0;j<(int)r.mats.size();j++) { if (j) mats += "; "; mats += r.mats[j]; }
      if (mats.length()==0) mats = "-";
      html += "<tr><td>" + r.name + "</td><td>" + r.acc + "</td><td>" + mats + "</td><td>" + r.created + "</td><td>";
      html += "<a class='btn btn-green' href='/capture_edit?uid=" + uids[i] + "&return_to=" + urlEncodeLocal(String("/teachers_all")) + "'>✏️ Editar</a> ";
      html += "<form method='POST' action='/teacher_delete' style='display:inline' onsubmit='return confirm(\"Eliminar totalmente este maestro?\");'>";
      html += "<input type='hidden' name='uid' value='" + uids[i] + "'>";
      html += "<input class='btn btn-red' type='submit' value='Eliminar totalmente'>";
      html += "</form>";
      html += "</td></tr>";
    }
    html += "</table>";
    html += "<script>"
            "function applyAllTeacherFilters(){ const table=document.getElementById('teachers_all_table'); if(!table) return; const f1=document.getElementById('ta_name').value.trim().toLowerCase(); const f2=document.getElementById('ta_acc').value.trim().toLowerCase(); const f3=document.getElementById('ta_mat').value.trim().toLowerCase(); for(let r=1;r<table.rows.length;r++){ const row=table.rows[r]; if(row.cells.length<4) continue; const name=row.cells[0].textContent.toLowerCase(); const acc=row.cells[1].textContent.toLowerCase(); const mats=row.cells[2].textContent.toLowerCase(); const ok=(name.indexOf(f1)!==-1)&&(acc.indexOf(f2)!==-1)&&(mats.indexOf(f3)!==-1); row.style.display = ok ? '' : 'none'; } }"
            "function clearAllTeacherFilters(){ document.getElementById('ta_name').value=''; document.getElementById('ta_acc').value=''; document.getElementById('ta_mat').value=''; applyAllTeacherFilters(); }"
            "</script>";
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
    if (c.size()>=4 && c[0]==uid && c[3]==materia) continue;
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
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; String header = f.readStringUntil('\n'); lines.push_back(header);
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size()>=1 && c[0]==uid) continue;
    lines.push_back(l);
  }
  f.close();
  writeAllLines(TEACHERS_FILE, lines);
  server.sendHeader("Location","/teachers_all");
  server.send(303,"text/plain","Deleted");
}
