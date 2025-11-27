// src/web/history.cpp
#include "history.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>

// Pequeña función de escape HTML para evitar inyección y mostrar valores en inputs
static String htmlEscape(const String &s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

// Muestra página con historial de accesos y filtros; permite descarga y borrado.
// Ahora acepta filtros GET opcionales: materia, profesor, uid, ts (timestamp prefix)
void handleHistoryPage() {
  String materiaFilter = server.hasArg("materia") ? server.arg("materia") : String();
  String profFilter   = server.hasArg("profesor") ? server.arg("profesor") : String();
  String uidFilter    = server.hasArg("uid") ? server.arg("uid") : String();
  String tsFilter     = server.hasArg("ts") ? server.arg("ts") : String(); // puede ser YYYY-MM-DD o prefijo parcial

  String html = htmlHeader("Historial de Accesos");
  html += "<div class='card'><h2>Historial de Accesos</h2>";

  html += "<p class='small'>Esta pestaña muestra el historial completo de accesos y capturas de tarjetas. Use filtros para localizar rápidamente eventos (materia, profesor, UID o timestamp).</p>";

  // Filtros (cliente) - rellenamos inputs con valores recibidos (escape)
  html += "<div class='filters'>";
  html += "<input id='hf_materia' placeholder='Filtrar por materia' value='" + htmlEscape(materiaFilter) + "'>";
  html += "<input id='hf_prof' placeholder='Filtrar por profesor' value='" + htmlEscape(profFilter) + "'>";
  html += "<input id='hf_uid' placeholder='Filtrar por UID' value='" + htmlEscape(uidFilter) + "'>";
  html += "<input id='hf_ts' placeholder='Filtrar por timestamp (prefijo)' value='" + htmlEscape(tsFilter) + "'>";
  html += "<input id='hf_name' placeholder='Filtrar por nombre'>";
  html += "<button class='search-btn btn btn-blue' onclick='applyHistoryFilters()'>Buscar</button>";
  html += "<button class='search-btn btn btn-green' onclick='clearHistoryFilters()'>Limpiar</button>";
  html += "</div>";

  // Acciones: descargar CSV (manteniendo filtros básicos) / borrar historial / volver
  html += "<p style='margin-top:8px'>";
  String csvLink = "/history.csv";
  bool hasParam = false;
  if (materiaFilter.length()) { csvLink += (hasParam ? "&" : "?") + String("materia=") + materiaFilter; hasParam = true; }
  if (uidFilter.length())     { csvLink += (hasParam ? "&" : "?") + String("uid=") + uidFilter; hasParam = true; }
  if (tsFilter.length())      { csvLink += (hasParam ? "&" : "?") + String("ts=") + tsFilter; hasParam = true; }
  html += "<a class='btn btn-green' href='" + csvLink + "'>📥 Descargar (filtrado)</a> ";
  html += "<form style='display:inline' method='POST' action='/history_clear' onsubmit='return confirm(\"Borrar todo el historial? Esta acción es irreversible.\")'>";
  html += "<input class='btn btn-red' type='submit' value='🗑️ Borrar Historial'></form> ";
  html += "<a class='btn btn-blue' href='/'>Inicio</a></p>";

  // Abrir archivo de attendance
  if (!SPIFFS.exists(ATT_FILE)) {
    html += "<p>No hay historial.</p>";
    html += htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  if (!f) {
    html += "<p>No se pudo abrir archivo de historial.</p>";
    html += htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  // Tabla con registros
  String header = f.readStringUntil('\n');
  html += "<table id='history_table'><tr><th>Timestamp</th><th>Nombre</th><th>Cuenta</th><th>Materia</th><th>Modo</th></tr>";

  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    String ts   = (c.size()>0?c[0]:"");
    String uid  = (c.size()>1?c[1]:"");
    String name = (c.size()>2?c[2]:"");
    String acc  = (c.size()>3?c[3]:"");
    String mat  = (c.size()>4?c[4]:"");
    String mode = (c.size()>5?c[5]:"");

    // Aplicar filtros del servidor (si vienen por query string)
    if (materiaFilter.length() && mat != materiaFilter) continue;
    if (uidFilter.length() && uid != uidFilter) continue;
    if (tsFilter.length()) {
      if (ts.indexOf(tsFilter) != 0) continue;
    }
    if (profFilter.length()) {
      bool okProf=false;
      auto courses = loadCourses();
      for (auto &co : courses) {
        if (co.materia == mat) {
          if (co.profesor.indexOf(profFilter) != -1) { okProf=true; break; }
        }
      }
      if (!okProf) continue;
    }

    html += "<tr><td>" + ts + "</td><td>" + name + "</td><td>" + acc + "</td><td>" + mat + "</td><td>" + mode + "</td></tr>";
  }
  f.close();
  html += "</table>";

  // Scripts cliente para filtrar la tabla sin recargar
  html += "<script>"
          "function applyHistoryFilters(){ "
          "const table=document.getElementById('history_table'); if(!table) return; "
          "const fm=document.getElementById('hf_materia').value.trim().toLowerCase(); "
          "const fp=document.getElementById('hf_prof').value.trim().toLowerCase(); "
          "const fu=document.getElementById('hf_uid').value.trim().toLowerCase(); "
          "const fts=document.getElementById('hf_ts').value.trim().toLowerCase(); "
          "const fn=document.getElementById('hf_name').value.trim().toLowerCase(); "
          "for(let r=1;r<table.rows.length;r++){ "
          "const row=table.rows[r]; if(row.cells.length<5) continue; "
          "const mat=row.cells[3].textContent.toLowerCase(); "
          "const name=row.cells[1].textContent.toLowerCase(); "
          "const uid=row.cells[2].textContent.toLowerCase(); "
          "const ts=row.cells[0].textContent.toLowerCase(); "
          "var ok=true; "
          "if(fm.length && mat.indexOf(fm)===-1) ok=false; "
          "if(fu.length && uid.indexOf(fu)===-1) ok=false; "
          "if(fts.length && ts.indexOf(fts)===-1) ok=false; "
          "if(fn.length && name.indexOf(fn)===-1) ok=false; "
          "row.style.display = ok ? '' : 'none'; "
          "} }"
          "function clearHistoryFilters(){ "
          "document.getElementById('hf_materia').value=''; "
          "document.getElementById('hf_prof').value=''; "
          "document.getElementById('hf_uid').value=''; "
          "document.getElementById('hf_ts').value=''; "
          "document.getElementById('hf_name').value=''; "
          "applyHistoryFilters(); }"
          "</script>";

  html += htmlFooter();
  server.send(200,"text/html",html);
}

// /history.csv (GET) - Genera y envía CSV filtrado opcionalmente por materia, uid y/o ts (prefix)
void handleHistoryCSV() {
  String materiaFilter = server.hasArg("materia") ? server.arg("materia") : String();
  String uidFilter     = server.hasArg("uid") ? server.arg("uid") : String();
  String tsFilter      = server.hasArg("ts") ? server.arg("ts") : String();

  if (!SPIFFS.exists(ATT_FILE)) { server.send(404,"text/plain","no history"); return; }
  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  String out = "\"timestamp\",\"uid\",\"name\",\"account\",\"materia\",\"mode\"\r\n";
  String header = f.readStringUntil('\n');
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    String ts = (c.size()>0?c[0]:"");
    String uid = (c.size()>1?c[1]:"");
    String mat = (c.size()>4?c[4]:"");

    if (materiaFilter.length() && mat != materiaFilter) continue;
    if (uidFilter.length() && uid != uidFilter) continue;
    if (tsFilter.length()) {
      if (ts.indexOf(tsFilter) != 0) continue;
    }
    out += l + "\r\n";
  }
  f.close();
  server.sendHeader("Content-Disposition","attachment; filename=history.csv");
  server.send(200,"text/csv",out);
}

// /history_clear (POST) - Borra (resetea) el archivo de attendance dejando solo la cabecera.
void handleHistoryClearPOST() {
  writeAllLines(ATT_FILE, std::vector<String>{String("\"timestamp\",\"uid\",\"name\",\"account\",\"materia\",\"mode\"")});
  server.sendHeader("Location","/history");
  server.send(303,"text/plain","Historial borrado");
}

// /materia_history (GET) - Lista fechas con registros para una materia y permite descargar por día.
void handleMateriaHistoryGET() {
  if (!server.hasArg("materia")) { server.send(400,"text/plain","materia required"); return; }
  String materia = server.arg("materia");
  materia.trim();
  if (!courseExists(materia)) { server.send(404,"text/plain","Materia no encontrada"); return; }
  std::vector<String> dates;
  if (!SPIFFS.exists(ATT_FILE)) { server.send(404,"text/plain","no history"); return; }
  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  if (f) {
    String header = f.readStringUntil('\n');
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size()>4 && c[4]==materia) {
        String ts = c[0];
        if (ts.length() >= 10) {
          String day = ts.substring(0,10); // YYYY-MM-DD
          bool found=false;
          for (auto &d : dates) if (d==day) { found=true; break; }
          if (!found) dates.push_back(day);
        }
      }
    }
    f.close();
  }
  String html = htmlHeader(("Historial por días - " + materia).c_str());
  html += "<div class='card'><h2>Historial por días - " + materia + "</h2>";
  html += "<p class='small'>Seleccione un día para descargar la lista de asistencia de esa materia.</p>";
  if (dates.size()==0) html += "<p>No hay registros para esta materia.</p>";
  else {
    html += "<ul>";
    for (auto &d : dates) {
      html += "<li>" + d + " <a class='btn btn-blue' href='/history.csv?materia=" + materia + "&ts=" + d + "'>⬇️ Descargar CSV</a></li>";
    }
    html += "</ul>";
  }
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a> <a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}
