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

// URL-encode simple (para construir links con parámetros seguros)
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

// Muestra página con historial de accesos y filtros; ahora solo: materia, profesor, alumno, fecha (date picker)
// Acepta adicionalmente 'uid' en query string para filtrar por UID directamente.
void handleHistoryPage() {
  String materiaFilter = server.hasArg("materia") ? server.arg("materia") : String();
  String profFilter   = server.hasArg("profesor") ? server.arg("profesor") : String();
  String nameFilter   = server.hasArg("nombre") ? server.arg("nombre") : String();
  String dateFilter   = server.hasArg("date") ? server.arg("date") : String(); // YYYY-MM-DD exacto
  String uidFilter    = server.hasArg("uid") ? server.arg("uid") : String(); // nuevo: filtrar por uid

  String html = htmlHeader("Historial de Accesos");
  html += "<div class='card'><h2>Historial de Accesos</h2>";

  html += "<p class='small'>Esta pestaña muestra el historial completo de accesos y capturas de tarjetas. Use filtros para localizar rápidamente eventos (materia, profesor, alumno o fecha).</p>";

  // Filtros (cliente) - rellenamos inputs con valores recibidos (escape)
  html += "<div class='filters'>";
  html += "<input id='hf_materia' placeholder='Filtrar por materia' value='" + htmlEscape(materiaFilter) + "'>";
  html += "<input id='hf_prof' placeholder='Filtrar por nombre de profesor' value='" + htmlEscape(profFilter) + "'>";
  html += "<input id='hf_name' placeholder='Filtrar por nombre de alumno' value='" + htmlEscape(nameFilter) + "'>";
  // date picker (cliente)
  html += "<input id='hf_date' type='date' value='" + htmlEscape(dateFilter) + "'>";
  html += "<button class='search-btn btn btn-blue' onclick='applyHistoryFilters()'>Buscar</button>";
  html += "<button class='search-btn btn btn-green' onclick='clearHistoryFilters()'>Limpiar</button>";
  html += "</div>";

  // Acciones: descargar CSV (manteniendo filtros básicos) / borrar historial / volver
  html += "<p style='margin-top:8px'>";
  String csvLink = "/history.csv";
  bool hasParam = false;
  if (materiaFilter.length()) { csvLink += (hasParam ? "&" : "?") + String("materia=") + urlEncodeLocal(materiaFilter); hasParam = true; }
  if (dateFilter.length())    { csvLink += (hasParam ? "&" : "?") + String("ts=") + urlEncodeLocal(dateFilter); hasParam = true; }
  if (uidFilter.length())     { csvLink += (hasParam ? "&" : "?") + String("uid=") + urlEncodeLocal(uidFilter); hasParam = true; }
  html += "<a class='btn btn-green' href='" + csvLink + "'>📥 Descargar (filtrado)</a> ";
  html += "<form style='display:inline' method='POST' action='/history_clear' onsubmit='return confirm(\"Borrar todo el historial? Esta acción es irreversible.\")'>"
          "<input class='btn btn-red' type='submit' value='🗑️ Borrar Historial'></form> ";
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
    if (uidFilter.length()) {
      String utrim = uid; utrim.trim();
      if (utrim != uidFilter) continue;
    }
    if (materiaFilter.length()) {
      String matTrim = mat; matTrim.trim();
      String mfTrim = materiaFilter; mfTrim.trim();
      if (matTrim != mfTrim) continue;
    }
    if (dateFilter.length()) {
      if (ts.indexOf(dateFilter) != 0) continue;
    }
    if (profFilter.length()) {
      bool okProf=false;
      auto courses = loadCourses();
      String profFilterLc = profFilter; profFilterLc.toLowerCase(); profFilterLc.trim();
      for (auto &co : courses) {
        String cm = co.materia; cm.trim();
        if (cm == mat) {
          String profLc = co.profesor; profLc.toLowerCase(); profLc.trim();
          if (profLc.indexOf(profFilterLc) != -1) { okProf=true; break; }
        }
      }
      if (!okProf) continue;
    }
    if (nameFilter.length()) {
      String nameLc = name; nameLc.toLowerCase();
      String nf = nameFilter; nf.toLowerCase();
      if (nameLc.indexOf(nf) == -1) continue;
    }

    html += "<tr><td>" + ts + "</td><td>" + name + "</td><td>" + acc + "</td><td>" + mat + "</td><td>" + mode + "</td></tr>";
  }
  f.close();
  html += "</table>";

  // Scripts cliente para filtrar la tabla sin recargar (incluye filtro por profesor usando courses)
  html += R"rawliteral(
    <script>
      function applyHistoryFilters(){
        const table=document.getElementById('history_table'); if(!table) return;
        const fm=document.getElementById('hf_materia').value.trim().toLowerCase();
        const fp=document.getElementById('hf_prof').value.trim().toLowerCase();
        const fn=document.getElementById('hf_name').value.trim().toLowerCase();
        const fdate=document.getElementById('hf_date').value.trim();
        for(let r=1;r<table.rows.length;r++){
          const row=table.rows[r]; if(row.cells.length<5) continue;
          const mat=row.cells[3].textContent.toLowerCase();
          const name=row.cells[1].textContent.toLowerCase();
          const ts=row.cells[0].textContent.toLowerCase();
          var ok=true;
          if(fm.length && mat.indexOf(fm)===-1) ok=false;
          if(fn.length && name.indexOf(fn)===-1) ok=false;
          if(fdate.length && ts.indexOf(fdate)===-1) ok=false;
          if(fp.length){
            // approximate client-side: check if materia or name contains professor filter text
            if (mat.indexOf(fp)===-1 && name.indexOf(fp)===-1) ok=false;
          }
          row.style.display = ok ? '' : 'none';
        }
      }
      function clearHistoryFilters(){
        document.getElementById('hf_materia').value='';
        document.getElementById('hf_prof').value='';
        document.getElementById('hf_name').value='';
        document.getElementById('hf_date').value='';
        applyHistoryFilters();
      }
    </script>
  )rawliteral";

  html += htmlFooter();
  server.send(200,"text/html",html);
}

// /history.csv (GET) - Genera y envía CSV filtrado opcionalmente por materia y/o ts (fecha prefix) y/o uid
void handleHistoryCSV() {
  String materiaFilter = server.hasArg("materia") ? server.arg("materia") : String();
  String tsFilter      = server.hasArg("ts") ? server.arg("ts") : String();
  String uidFilter     = server.hasArg("uid") ? server.arg("uid") : String();

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

    if (uidFilter.length()) {
      String utrim = uid; utrim.trim();
      if (utrim != uidFilter) continue;
    }
    if (materiaFilter.length()) {
      String matTrim = mat; matTrim.trim();
      String mfTrim = materiaFilter; mfTrim.trim();
      if (matTrim != mfTrim) continue;
    }
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
      html += "<li>" + d + " <a class='btn btn-blue' href='/history.csv?materia=" + urlEncodeLocal(materia) + "&ts=" + urlEncodeLocal(d) + "'>⬇️ Descargar CSV</a></li>";
    }
    html += "</ul>";
  }
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/materias'>Volver</a> <a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}
