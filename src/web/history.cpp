#include "history.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>

// Muestra página con historial de accesos y filtros; permite descarga y borrado.
void handleHistoryPage() {
  String materiaFilter = server.hasArg("materia") ? server.arg("materia") : String();
  String profFilter = server.hasArg("profesor") ? server.arg("profesor") : String();
  
  String html = htmlHeader("Historial de Accesos");
  html += "<div class='card'><h2>Historial de Accesos</h2>";

  // Texto explicativo
  html += "<p class='small'>Esta pestaña muestra el historial completo de accesos y capturas de tarjetas de los estudiantes. "
          "Aquí puede ver cuándo y quién ha sido registrado, junto con la materia correspondiente. "
          "Importante: Al borrar todo el historial, también se reiniciarán las listas de asistencia de las materias.</p>";

  // Filtros (cliente)
  html += "<div class='filters'>";
  html += "<input id='hf_materia' placeholder='Filtrar por materia' value='" + materiaFilter + "'>";
  html += "<input id='hf_prof' placeholder='Filtrar por profesor' value='" + profFilter + "'>";
  html += "<input id='hf_name' placeholder='Filtrar por nombre'>";
  html += "<button class='search-btn btn btn-blue' onclick='applyHistoryFilters()'>Buscar</button>";
  html += "<button class='search-btn btn btn-green' onclick='clearHistoryFilters()'>Limpiar</button>";
  html += "</div>";

  // Acciones: descargar CSV completo / borrar historial / volver
  html += "<p style='margin-top:8px'>";
  html += "<a class='btn btn-green' href='/history.csv'>📥 Descargar todo el historial del laboratorio</a> ";
  html += "<form style='display:inline' method='POST' action='/history_clear' onsubmit='return confirm(\"Borrar todo el historial? Esta acción es irreversible.\")'>";
  html += "<input class='btn btn-red' type='submit' value='🗑️ Borrar Historial'></form> ";
  html += "<a class='btn btn-blue' href='/'>Inicio</a></p>";

  // Abrir archivo de attendance
  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  if (!f) { 
    // Si no existe o no se puede abrir, mostrar mensaje
    html += "<p>No hay historial.</p>"; 
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
    String ts = (c.size()>0?c[0]:"");
    String name = (c.size()>2?c[2]:"");
    String acc = (c.size()>3?c[3]:"");
    String mat = (c.size()>4?c[4]:"");
    String mode = (c.size()>5?c[5]:"");

    // Filtrado por profesor 
    if (profFilter.length()) {
      bool okProf=false;
      auto courses = loadCourses();
      for (auto &co : courses) {
        if (co.materia == mat && co.profesor.indexOf(profFilter) != -1) { okProf=true; break; }
      }
      if (!okProf) continue;
    }
    // Filtrado por materia 
    if (materiaFilter.length() && mat != materiaFilter) continue;

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
          "const fn=document.getElementById('hf_name').value.trim().toLowerCase(); "
          "for(let r=1;r<table.rows.length;r++){ "
          "const row=table.rows[r]; if(row.cells.length<5) continue; "
          "const mat=row.cells[3].textContent.toLowerCase(); "
          "const name=row.cells[1].textContent.toLowerCase(); "
          "const ok=(mat.indexOf(fm)!==-1)&&(name.indexOf(fn)!==-1); "
          "row.style.display = ok ? '' : 'none'; "
          "} "
          "}"
          "function clearHistoryFilters(){ "
          "document.getElementById('hf_materia').value=''; "
          "document.getElementById('hf_prof').value=''; "
          "document.getElementById('hf_name').value=''; "
          "applyHistoryFilters(); "
          "}"
          "</script>";

  html += htmlFooter();
  server.send(200,"text/html",html);
}

// /history.csv (GET) - Genera y envía CSV filtrado opcionalmente por materia y/o fecha.
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
      if (ts.indexOf(dateFilter) != 0) continue; // compara prefijo YYYY-MM-DD
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
        String day = ts.substring(0,10); // extraer YYYY-MM-DD
        bool found=false;
        for (auto &d : dates) if (d==day) { found=true; break; }
        if (!found) dates.push_back(day);
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
      // Enlace descarga filtrada por materia+fecha
      html += "<li>" + d + " <a class='btn btn-blue' href='/history.csv?materia=" + materia + "&date=" + d + "'>⬇️ Descargar CSV</a></li>";
    }
    html += "</ul>";
  }
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a> <a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}
