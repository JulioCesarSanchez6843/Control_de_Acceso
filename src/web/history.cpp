// src/web/history.cpp
#include "history.h"
#include "web_common.h"
#include "globals.h"
#include "db_sync.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <vector>

static String htmlEscape(const String &s) {
  String out = s;
  out.replace("&", "&amp;");
  out.replace("<", "&lt;");
  out.replace(">", "&gt;");
  out.replace("\"", "&quot;");
  out.replace("'", "&#39;");
  return out;
}

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

static String csvField(const String &v) {
  String out = v;
  out.replace("\"", "\"\"");
  return "\"" + out + "\"";
}

struct HistoryRec {
  String ts;
  String uid;
  String name;
  String account;
  String materia;
  String mode;
  int id = -1;
};

static String makeHistoryKey(const HistoryRec &r) {
  return r.ts + "|" + r.uid + "|" + r.name + "|" + r.account + "|" + r.materia + "|" + r.mode;
}

// Filtra por profesor buscando la materia en el listado de materias de la BD
static bool recordMatchesProfessor(const String &materia, const String &profFilter) {
  if (!profFilter.length()) return true;
  if (WiFi.status() != WL_CONNECTED) return false;

  String profFilterLc = profFilter;
  profFilterLc.toLowerCase();
  profFilterLc.trim();

  String body = listMaterias();
  if (!body.length()) return false;

  DynamicJsonDocument doc(16384);
  if (deserializeJson(doc, body)) return false;

  auto checkObj = [&](JsonObjectConst obj) -> bool {
    String mat = obj["materia"] | "";
    String prof = obj["profesor"] | "";
    mat.trim();
    prof.toLowerCase();
    prof.trim();
    if (mat == materia && prof.indexOf(profFilterLc) != -1) return true;
    return false;
  };

  if (doc.is<JsonArray>()) {
    for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
      if (v.is<JsonObjectConst>() && checkObj(v.as<JsonObjectConst>())) return true;
    }
  } else if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "materias", "rows", "result", "items"};
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArrayConst>()) {
        for (JsonVariantConst v : root[key].as<JsonArrayConst>()) {
          if (v.is<JsonObjectConst>() && checkObj(v.as<JsonObjectConst>())) return true;
        }
        return false;
      }
    }
    return checkObj(root);
  }

  return false;
}

// Carga asistencias desde Oracle únicamente
static bool loadOracleAttendance(
    std::vector<HistoryRec> &out,
    const String &uidFilter,
    const String &materiaFilter
) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String body = listAsistencias(uidFilter, materiaFilter);
  if (!body.length()) return false;

  DynamicJsonDocument doc(65536);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("WARN: no se pudo parsear asistencias Oracle: ");
    Serial.println(err.c_str());
    return false;
  }

  auto handleObj = [&](JsonObjectConst obj) {
    HistoryRec r;
    r.id      = obj["id"]        | -1;
    r.ts      = obj["fecha_hora"] | "";
    if (!r.ts.length()) r.ts = obj["timestamp"] | "";
    r.uid     = obj["rfid_uid"]  | "";
    r.name    = obj["name"]      | "";
    if (!r.name.length())    r.name    = obj["nombre"]  | "";
    r.account = obj["account"]   | "";
    if (!r.account.length()) r.account = obj["cuenta"]  | "";
    r.materia = obj["materia"]   | "";
    r.mode    = obj["tipo"]      | "";
    if (!r.mode.length())    r.mode    = obj["mode"]    | "";
    out.push_back(r);
  };

  if (doc.is<JsonArray>()) {
    for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
      if (v.is<JsonObjectConst>()) handleObj(v.as<JsonObjectConst>());
    }
    return true;
  }

  if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "asistencias", "rows", "result", "items"};
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArrayConst>()) {
        for (JsonVariantConst v : root[key].as<JsonArrayConst>()) {
          if (v.is<JsonObjectConst>()) handleObj(v.as<JsonObjectConst>());
        }
        return true;
      }
    }
    handleObj(root);
    return true;
  }

  return false;
}

static void dedupPush(std::vector<HistoryRec> &out, std::vector<String> &keys, const HistoryRec &rec) {
  String key = makeHistoryKey(rec);
  for (auto &k : keys) {
    if (k == key) return;
  }
  keys.push_back(key);
  out.push_back(rec);
}

// Fuente única: Oracle. Sin SPIFFS ni archivos locales.
static std::vector<HistoryRec> collectAttendance(
    const String &uidFilter,
    const String &materiaFilter
) {
  std::vector<HistoryRec> merged;
  std::vector<String> keys;

  std::vector<HistoryRec> oracleRows;
  if (loadOracleAttendance(oracleRows, uidFilter, materiaFilter)) {
    for (auto &r : oracleRows) dedupPush(merged, keys, r);
  }

  std::sort(merged.begin(), merged.end(), [](const HistoryRec &a, const HistoryRec &b) {
    return a.ts > b.ts;
  });

  return merged;
}

static std::vector<HistoryRec> applyHistoryFilters(
    const std::vector<HistoryRec> &in,
    const String &dateFilter,
    const String &profFilter,
    const String &nameFilter
) {
  std::vector<HistoryRec> out;
  String nameFilterLc = nameFilter;
  nameFilterLc.toLowerCase();
  nameFilterLc.trim();

  for (auto &r : in) {
    if (dateFilter.length()) {
      if (r.ts.indexOf(dateFilter) != 0) continue;
    }
    if (nameFilter.length()) {
      String nameLc = r.name;
      nameLc.toLowerCase();
      if (nameLc.indexOf(nameFilterLc) == -1) continue;
    }
    if (profFilter.length()) {
      if (!recordMatchesProfessor(r.materia, profFilter)) continue;
    }
    out.push_back(r);
  }

  return out;
}

static String buildHistoryTableRows(const std::vector<HistoryRec> &rows) {
  String html;
  for (auto &r : rows) {
    html += "<tr>";
    html += "<td>" + htmlEscape(r.ts)      + "</td>";
    html += "<td>" + htmlEscape(r.name)    + "</td>";
    html += "<td>" + htmlEscape(r.account) + "</td>";
    html += "<td>" + htmlEscape(r.materia) + "</td>";
    html += "<td>" + htmlEscape(r.mode)    + "</td>";
    html += "</tr>";
  }
  return html;
}

static String buildHistoryCSV(const std::vector<HistoryRec> &rows) {
  String out = "\"timestamp\",\"uid\",\"name\",\"account\",\"materia\",\"mode\"\r\n";
  for (auto &r : rows) {
    out += csvField(r.ts)      + ",";
    out += csvField(r.uid)     + ",";
    out += csvField(r.name)    + ",";
    out += csvField(r.account) + ",";
    out += csvField(r.materia) + ",";
    out += csvField(r.mode)    + "\r\n";
  }
  return out;
}

// Borra todas las asistencias en Oracle una por una
static bool clearOracleAttendanceAll() {
  if (WiFi.status() != WL_CONNECTED) return false;

  String body = listAsistencias();
  if (!body.length()) return false;

  DynamicJsonDocument doc(65536);
  DeserializationError err = deserializeJson(doc, body);
  if (err) return false;

  bool allOk = true;

  auto handleObj = [&](JsonObjectConst obj) {
    int id = obj["id"] | -1;
    if (id > 0) {
      if (!deleteAsistenciaById(id)) allOk = false;
    }
  };

  if (doc.is<JsonArray>()) {
    for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
      if (v.is<JsonObjectConst>()) handleObj(v.as<JsonObjectConst>());
    }
  } else if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();
    const char* wrappers[] = {"data", "asistencias", "rows", "result", "items"};
    bool wrapped = false;
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArrayConst>()) {
        wrapped = true;
        for (JsonVariantConst v : root[key].as<JsonArrayConst>()) {
          if (v.is<JsonObjectConst>()) handleObj(v.as<JsonObjectConst>());
        }
        break;
      }
    }
    if (!wrapped) handleObj(root);
  }

  return allOk;
}

// GET /history
void handleHistoryPage() {
  String materiaFilter = server.hasArg("materia")  ? server.arg("materia")  : String();
  String profFilter    = server.hasArg("profesor")  ? server.arg("profesor") : String();
  String nameFilter    = server.hasArg("nombre")    ? server.arg("nombre")   : String();
  String dateFilter    = server.hasArg("date")      ? server.arg("date")     : String();
  String uidFilter     = server.hasArg("uid")       ? server.arg("uid")      : String();

  std::vector<HistoryRec> rows = collectAttendance(uidFilter, materiaFilter);
  rows = applyHistoryFilters(rows, dateFilter, profFilter, nameFilter);

  String html = htmlHeader("Historial de Accesos");
  html += "<div class='card'><h2>Historial de Accesos</h2>";
  html += "<p class='small'>Esta pestaña muestra el historial completo de accesos. Los datos se leen desde Oracle.</p>";

  html += "<div class='filters'>";
  html += "<input id='hf_materia' placeholder='Filtrar por materia' value='"   + htmlEscape(materiaFilter) + "'>";
  html += "<input id='hf_prof'    placeholder='Filtrar por nombre de profesor' value='" + htmlEscape(profFilter) + "'>";
  html += "<input id='hf_name'    placeholder='Filtrar por nombre de alumno'  value='" + htmlEscape(nameFilter) + "'>";
  html += "<input id='hf_date' type='date' value='" + htmlEscape(dateFilter) + "'>";
  html += "<button class='search-btn btn btn-blue'  onclick='applyHistoryFilters()'>Buscar</button>";
  html += "<button class='search-btn btn btn-green' onclick='clearHistoryFilters()'>Limpiar</button>";
  html += "</div>";

  html += "<p style='margin-top:8px'>";
  String csvLink = "/history.csv";
  bool hasParam = false;
  auto addParam = [&](const String &key, const String &val) {
    if (!val.length()) return;
    csvLink += (hasParam ? "&" : "?") + key + "=" + urlEncodeLocal(val);
    hasParam = true;
  };
  addParam("materia", materiaFilter);
  addParam("ts",      dateFilter);
  addParam("uid",     uidFilter);
  addParam("profesor",profFilter);
  addParam("nombre",  nameFilter);

  html += "<a class='btn btn-green' href='" + csvLink + "'>📥 Descargar (filtrado)</a> ";
  html += "<form style='display:inline' method='POST' action='/history_clear' "
          "onsubmit='return confirm(\"Borrar todo el historial? Esta acción es irreversible.\")'>"
          "<input class='btn btn-red' type='submit' value='🗑️ Borrar Historial'></form> ";
  html += "<a class='btn btn-blue' href='/'>Inicio</a></p>";

  if (rows.empty()) {
    html += "<p>No hay historial.</p>";
  } else {
    html += "<table id='history_table'>"
            "<tr><th>Timestamp</th><th>Nombre</th><th>Cuenta</th><th>Materia</th><th>Modo</th></tr>";
    html += buildHistoryTableRows(rows);
    html += "</table>";
  }

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
          if(fp.length && mat.indexOf(fp)===-1 && name.indexOf(fp)===-1) ok=false;
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
  server.send(200, "text/html", html);
}

// GET /history.csv
void handleHistoryCSV() {
  String materiaFilter = server.hasArg("materia")  ? server.arg("materia")  : String();
  String tsFilter      = server.hasArg("ts")        ? server.arg("ts")       : String();
  String uidFilter     = server.hasArg("uid")       ? server.arg("uid")      : String();
  String profFilter    = server.hasArg("profesor")  ? server.arg("profesor") : String();
  String nameFilter    = server.hasArg("nombre")    ? server.arg("nombre")   : String();

  std::vector<HistoryRec> rows = collectAttendance(uidFilter, materiaFilter);
  rows = applyHistoryFilters(rows, tsFilter, profFilter, nameFilter);

  String out = buildHistoryCSV(rows);
  server.sendHeader("Content-Disposition", "attachment; filename=history.csv");
  server.send(200, "text/csv", out);
}

// POST /history_clear
void handleHistoryClearPOST() {
  bool oracleOk = clearOracleAttendanceAll();
  if (!oracleOk) {
    Serial.println("WARN: no se pudo borrar todo en Oracle (o no había conexión).");
  }

  server.sendHeader("Location", "/history");
  server.send(303, "text/plain", "Historial borrado");
}

// GET /materia_history
void handleMateriaHistoryGET() {
  if (!server.hasArg("materia")) {
    server.send(400, "text/plain", "materia required");
    return;
  }

  String materia = server.arg("materia");
  materia.trim();

  // Verificar que la materia existe en Oracle
  if (WiFi.status() == WL_CONNECTED) {
    String body = getMateriaByName(materia);
    if (!body.length()) {
      server.send(404, "text/plain", "Materia no encontrada");
      return;
    }
  }

  std::vector<HistoryRec> rows = collectAttendance(String(), materia);
  std::vector<String> dates;

  for (auto &r : rows) {
    if (r.ts.length() >= 10) {
      String day = r.ts.substring(0, 10);
      bool found = false;
      for (auto &d : dates) {
        if (d == day) { found = true; break; }
      }
      if (!found) dates.push_back(day);
    }
  }

  String html = htmlHeader(("Historial por días - " + materia).c_str());
  html += "<div class='card'><h2>Historial por días - " + htmlEscape(materia) + "</h2>";
  html += "<p class='small'>Seleccione un día para descargar la lista de asistencia de esa materia. Los datos se obtienen desde Oracle.</p>";

  if (dates.empty()) {
    html += "<p>No hay registros para esta materia.</p>";
  } else {
    html += "<ul>";
    for (auto &d : dates) {
      html += "<li>" + htmlEscape(d) +
              " <a class='btn btn-blue' href='/history.csv?materia=" + urlEncodeLocal(materia) +
              "&ts=" + urlEncodeLocal(d) + "'>⬇️ Descargar CSV</a></li>";
    }
    html += "</ul>";
  }

  html += "<p style='margin-top:8px'>"
          "<a class='btn btn-blue' href='/materias'>Volver</a> "
          "<a class='btn btn-blue' href='/'>Inicio</a></p>";
  html += "</div>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}