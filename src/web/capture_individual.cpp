// capture_individual.cpp
#include "capture_individual.h"
#include "capture_common.h"
#include "web_common.h"
#include "files_utils.h"
#include <FS.h>
#include <SPIFFS.h>
#include <ctype.h>
#include "display.h"

// Globals (de globals.h)
extern volatile bool captureMode;
extern volatile bool captureBatchMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// Self-register externs (usadas en common helpers)
extern volatile bool awaitingSelfRegister;
extern String currentSelfRegUID;
extern String currentSelfRegToken;
extern unsigned long awaitingSinceMs;
extern std::vector<SelfRegSession> selfRegSessions;

// Clase ligera que encapsula la lógica de captura individual
class IndividualCapture {
public:
  void handlePage() {
    captureMode = true;
    captureBatchMode = false;
    captureUID = "";
    captureName = "";
    captureAccount = "";
    captureDetectedAt = 0;

    #ifdef USE_DISPLAY
    showCaptureMode(false, false);
    #endif

    String html = htmlHeader("Capturar - Individual");
    html += "<div class='card'><h2>Captura Individual</h2>";
    html += "<p class='small'>Acerca la tarjeta. UID autocompletará los campos si existe.</p>";

    html += "<form id='capForm' method='POST' action='/capture_confirm'>";
    html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
    html += "Nombre:<br><input id='name' name='name' required><br>";
    html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7'><br>";

    auto courses = loadCourses();
    html += "Materia (seleccionar):<br><select id='materia' name='materia'>";
    html += "<option value=''>-- Seleccionar materia --</option>";
    for (auto &c : courses) html += "<option value='" + c.materia + "'>" + c.materia + " (" + c.profesor + ")</option>";
    html += "</select><br>";

    html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
    html += "<button type='submit' class='btn btn-green'>Confirmar</button>";
    html += "<a class='btn btn-red' href='/' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
    html += "</div>";
    html += "</form></div>" + htmlFooter();

    html += R"rawliteral(
      <script>
      function pollUID(){
        fetch('/capture_poll')
          .then(r=>r.json())
          .then(j=>{
            if(j.status=='found'){
              document.getElementById('uid').value = j.uid || '';
              if(j.name) document.getElementById('name').value = j.name;
              if(j.account) document.getElementById('account').value = j.account;
            }
            setTimeout(pollUID,700);
          })
          .catch(e=>setTimeout(pollUID,1200));
      }
      pollUID();
      window.addEventListener('beforeunload', function(){ try { navigator.sendBeacon('/capture_stop'); } catch(e){} });
      </script>
    )rawliteral";

    server.send(200, "text/html", html);
  }

  void handlePoll() {
    if (captureUID.length() == 0) {
      server.send(200, "application/json", "{\"status\":\"waiting\"}");
      return;
    }
    String j = "{\"status\":\"found\",\"uid\":\"" + captureUID + "\"";
    if (captureName.length() > 0) j += ",\"name\":\"" + jsonEscape(captureName) + "\"";
    if (captureAccount.length() > 0) j += ",\"account\":\"" + jsonEscape(captureAccount) + "\"";
    j += "}";
    server.send(200, "application/json", j);
  }

  void handleConfirm() {
    if (!server.hasArg("uid") || !server.hasArg("name") ||
        !server.hasArg("account") || !server.hasArg("materia")) {
      server.send(400, "text/plain", "Faltan parámetros");
      return;
    }

    String uid = server.arg("uid"); uid.trim();
    String name = server.arg("name"); name.trim();
    String account = server.arg("account"); account.trim();
    String materia = server.arg("materia"); materia.trim();

    if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }

    bool ok = true;
    if (account.length() != 7) ok = false;
    for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { ok = false; break; }
    if (!ok) { server.send(400, "text/plain", "Cuenta inválida"); return; }

    if (!courseExists(materia)) {
      server.send(400, "text/plain", "La materia especificada no existe. Regístrela primero en Materias.");
      return;
    }

    if (existsUserUidMateria(uid, materia) || existsUserAccountMateria(account, materia)) {
      captureMode = false; captureBatchMode = false;
      String html = htmlHeader("Duplicado detectado");
      html += "<div class='card'><h3 style='color:red;'>⚠️ El estudiante ya está registrado en esta materia.</h3>";
      html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'><a href='/' class='btn btn-blue'>Inicio</a> <a href='/capture' class='btn btn-green'>Capturar otro</a></div></div>";
      html += htmlFooter();
      server.send(200,"text/html",html);
      return;
    }

    String created = nowISO();
    String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"" + created + "\"";
    if (!appendLineToFile(USERS_FILE, line)) { server.send(500, "text/plain", "Error guardando usuario"); return; }

    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura\"";
    if (!appendLineToFile(ATT_FILE, rec)) { server.send(500, "text/plain", "Error guardando attendance"); return; }

    captureMode = false; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

    String html = htmlHeader("Registrado correctamente");
    html += "<div class='card'><h3>✅ Usuario registrado correctamente.</h3>";
    html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'><a href='/' class='btn btn-blue'>Inicio</a> <a href='/capture' class='btn btn-green'>Capturar otro</a></div></div>";
    html += htmlFooter();
    server.send(200,"text/html",html);
  }

  // compatibilidad start/stop
  void handleStartPOST() {
    captureMode = true; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
    #ifdef USE_DISPLAY
    showCaptureMode(false,false);
    #endif
    server.sendHeader("Location", "/capture_individual");
    server.send(303, "text/plain", "capture started");
  }

  void handleStopGET() {
    captureMode = false; captureBatchMode = false;
    captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

    cancelCaptureAndReturnToNormal();

    server.sendHeader("Location", "/");
    server.send(303, "text/plain", "stopped");
  }

  void handleEditPage() {
    if (!server.hasArg("uid")) { server.send(400, "text/plain", "uid required"); return; }
    String uid = server.arg("uid");
    String return_to = server.hasArg("return_to") ? server.arg("return_to") : "/students_all";
    return_to = sanitizeReturnTo(return_to);

    File f = SPIFFS.open(USERS_FILE, FILE_READ);
    if (!f) { server.send(500, "text/plain", "No se pudo abrir archivo de usuarios"); return; }

    String foundName = "", foundAccount = "", foundMateria = "", foundCreated = "";
    bool found = false;
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (l.length() == 0) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 3 && c[0] == uid) {
        foundName = (c.size() > 1 ? c[1] : "");
        foundAccount = (c.size() > 2 ? c[2] : "");
        foundMateria = (c.size() > 3 ? c[3] : "");
        foundCreated = (c.size() > 4 ? c[4] : nowISO());
        found = true;
        break;
      }
    }
    f.close();
    if (!found) { server.send(404, "text/plain", "Alumno no encontrado"); return; }

    String html = htmlHeader("Editar Alumno");
    html += "<div class='card'><h2>Editar Usuario</h2>";
    html += "<form method='POST' action='/capture_edit_post'>";
    html += "<input type='hidden' name='uid' value='" + uid + "'>";
    html += "<input type='hidden' name='return_to' value='" + return_to + "'>";
    html += "<label>UID (no editable):</label><br>";
    html += "<input readonly style='background:#eee;' value='" + uid + "'><br><br>";
    html += "<label>Nombre:</label><br>";
    html += "<input name='name' required value='" + foundName + "'><br><br>";
    html += "<label>Cuenta:</label><br>";
    html += "<input name='account' required maxlength='7' minlength='7' value='" + foundAccount + "'><br><br>";
    auto courses2 = loadCourses();
    html += "<label>Materia:</label><br><select name='materia'>";
    html += "<option value=''>-- Ninguna --</option>";
    for (auto &c : courses2) {
      String sel = (c.materia == foundMateria) ? " selected" : "";
      html += "<option value='" + c.materia + "'" + sel + ">" + c.materia + " (" + c.profesor + ")</option>";
    }
    html += "</select><br><br>";
    html += "<label>Registrado:</label><br>";
    html += "<div style='padding:6px;background:#f5f7f5;border-radius:4px;'>" + foundCreated + "</div><br>";
    html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
    html += "<button type='submit' class='btn btn-green'>Guardar</button>";
    html += "<a class='btn btn-red' href='" + return_to + "'>Cancelar</a>";
    html += "</div></form></div>" + htmlFooter();
    server.send(200, "text/html", html);
  }

  void handleEditPost() {
    if (!server.hasArg("uid") || !server.hasArg("name") || !server.hasArg("account") || !server.hasArg("return_to")) {
      server.send(400, "text/plain", "Faltan parámetros");
      return;
    }
    String uid = server.arg("uid"); uid.trim();
    String name = server.arg("name"); name.trim();
    String account = server.arg("account"); account.trim();
    String materia = server.hasArg("materia") ? server.arg("materia") : "";
    materia.trim();
    String return_to = sanitizeReturnTo(server.arg("return_to"));

    if (uid.length() == 0) { server.send(400, "text/plain", "UID vacío"); return; }
    if (name.length() == 0) { server.send(400, "text/plain", "Nombre vacío"); return; }
    if (account.length() != 7) { server.send(400, "text/plain", "Cuenta inválida"); return; }
    for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) { server.send(400, "text/plain", "Cuenta inválida"); return; }

    if (materia.length() > 0 && !courseExists(materia)) { server.send(400, "text/plain", "La materia seleccionada no existe"); return; }

    File f = SPIFFS.open(USERS_FILE, FILE_READ);
    if (!f) { server.send(500, "text/plain", "No se pudo abrir archivo de usuarios"); return; }

    std::vector<String> lines;
    String header = f.readStringUntil('\n');
    lines.push_back(header);
    bool updated = false;

    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (l.length() == 0) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 1 && c[0] == uid) {
        String created = (c.size() > 4 ? c[4] : nowISO());
        String mat = (materia.length() ? materia : (c.size() > 3 ? c[3] : ""));
        String newline = "\"" + uid + "\",\"" + name + "\",\"" + account + "\",\"" + mat + "\",\"" + created + "\"";
        lines.push_back(newline);
        updated = true;
      } else lines.push_back(l);
    }
    f.close();

    if (!updated) { server.send(404, "text/plain", "Alumno no encontrado"); return; }

    if (!writeAllLines(USERS_FILE, lines)) { server.send(500, "text/plain", "Error guardando usuarios"); return; }

    server.sendHeader("Location", sanitizeReturnTo(server.arg("return_to")));
    server.send(303, "text/plain", "Updated");
  }
};

// Instancia única
static IndividualCapture g_individualCapture;

// Funciones públicas llamadas desde shim
void capture_individual_page() { g_individualCapture.handlePage(); }
void capture_individual_poll() { g_individualCapture.handlePoll(); }
void capture_individual_confirm() { g_individualCapture.handleConfirm(); }
void capture_individual_startPOST() { g_individualCapture.handleStartPOST(); }
void capture_individual_stopGET()  { g_individualCapture.handleStopGET(); }
void capture_individual_editPage() { g_individualCapture.handleEditPage(); }
void capture_individual_editPost() { g_individualCapture.handleEditPost(); }

// Expose C-linkage style names expected by shim (declare in capture_individual.h)
