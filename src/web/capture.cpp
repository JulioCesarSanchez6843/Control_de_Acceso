#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "capture.h"
#include "globals.h"
#include "web_common.h"

// Página de captura manual con confirmación
void handleCapturePage() {
  captureMode = true;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  String html = htmlHeader("Capturar Tarjeta (modo manual)");
  html += "<div class='card'><h2>Capturar Tarjeta</h2>";
  html += "<p class='small'>Acerca la tarjeta. Si ya existe en otra materia, se autocompletan los campos. "
          "Selecciona Materia (obligatorio) y revisa la información antes de confirmar.</p>";

  // Formulario principal
  html += "<form id='capForm' method='POST' action='/capture_confirm'>";
  html += "UID (autocompleta):<br><input id='uid' name='uid' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input id='name' name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input id='account' name='account' required maxlength='7' minlength='7'><br>";

  auto courses = loadCourses();
  html += "Materia (seleccionar):<br><select id='materia' name='materia'>";
  html += "<option value=''>-- Seleccionar materia --</option>";
  for (auto &c : courses)
    html += "<option value='" + c.materia + "'>" + c.materia + " (" + c.profesor + ")</option>";
  html += "</select><br>";

  html += "<div id='msg' style='color:red;font-weight:bold;margin-top:10px;'></div>";

  // Botones
  html += "<div style='display:flex;gap:10px;justify-content:center;margin-top:10px;'>";
  html += "<button type='submit' class='btn btn-green'>Confirmar</button>";
  html += "<a class='btn btn-red' href='/' onclick='fetch(\"/capture_stop\");return true;'>Cancelar</a>";
  html += "</div>";

  html += "</form>";

  // Script de autocompletado y validación
  html += R"rawliteral(
  <script>
  function isAccountValid(s){ return /^[0-9]{7}$/.test(s); }

  // Polling para detectar tarjeta
  function poll(){
    fetch('/capture_poll')
      .then(r=>r.json())
      .then(j=>{
        if(j.status=='waiting'){ setTimeout(poll,700); }
        else if(j.status=='found'){
          document.getElementById('uid').value = j.uid;
          if(j.name) document.getElementById('name').value = j.name;
          if(j.account) document.getElementById('account').value = j.account;
          setTimeout(poll,700);
        } else { setTimeout(poll,700); }
      })
      .catch(e=>setTimeout(poll,1200));
  }

  poll();

  // Detiene modo captura al salir
  window.addEventListener('beforeunload', function(){
    try { navigator.sendBeacon('/capture_stop'); } catch(e){}
  });
  </script>
  )rawliteral";

  html += "</div>" + htmlFooter();
  server.send(200,"text/html",html);
}

// Manejo de polling para UID detectado
void handleCapturePoll() {
  if (captureUID.length() == 0) {
    server.send(200,"application/json","{\"status\":\"waiting\"}");
    return;
  }
  String j = "{\"status\":\"found\",\"uid\":\"" + captureUID + "\"";
  if (captureName.length()) j += ",\"name\":\"" + captureName + "\"";
  if (captureAccount.length()) j += ",\"account\":\"" + captureAccount + "\"";
  j += "}";
  server.send(200,"application/json", j);
}

// Confirmación manual (POST)
void handleCaptureConfirm() {
  if (!server.hasArg("uid") || !server.hasArg("name") ||
      !server.hasArg("account") || !server.hasArg("materia")) {
    server.send(400,"text/plain","Faltan parámetros");
    return;
  }

  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.arg("materia"); materia.trim();

  if (uid.isEmpty()) { server.send(400,"text/plain","UID vacío"); return; }

  bool ok = true;
  if (account.length() != 7) ok = false;
  for (size_t i = 0; i < account.length(); i++) if (!isDigit(account[i])) ok = false;
  if (!ok) { server.send(400,"text/plain","Cuenta inválida"); return; }

  if (!courseExists(materia)) {
    server.send(400,"text/plain","La materia especificada no existe. Regístrela primero en Materias.");
    return;
  }

  // 🚫 Validar duplicado sin redirigir
  if (existsUserUidMateria(uid, materia) || existsUserAccountMateria(account, materia)) {
    captureMode = false;
    String html = htmlHeader("Duplicado detectado");
    html += "<div class='card'><h3 style='color:red;'>⚠️ El estudiante ya está registrado en esta materia.</h3>";
    html += "<a href='/capture' class='btn btn-blue'>Volver a Capturar</a> ";
    html += "<a href='/' class='btn btn-green'>Inicio</a></div>";
    html += htmlFooter();
    server.send(200,"text/html",html);
    return;
  }

  // ✅ Registrar nuevo usuario
  String created = nowISO();
  String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," +
                "\"" + materia + "\"," + "\"" + created + "\"";
  appendLineToFile(USERS_FILE, line);

  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," +
               "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura\"";
  appendLineToFile(ATT_FILE, rec);

  captureMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  // 🔁 Confirmación visual
  String html = htmlHeader("Registrado correctamente");
  html += "<div class='card'><h3>✅ Usuario registrado correctamente.</h3>";
  html += "<a href='/' class='btn btn-green'>Volver al inicio</a></div>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleCaptureStopGET() {
  captureMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;
  server.sendHeader("Location","/");
  server.send(303,"text/plain","stopped");
}
