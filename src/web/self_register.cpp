// src/web/self_register.cpp
#include "self_register.h"
#include "web_common.h"
#include "files_utils.h"
#include "globals.h"
#include <SPIFFS.h>
#include "web_utils.h"   // para htmlEscape(), csvEscape(), etc.
#include <algorithm>
#include "display.h"     // <<-- para showWaitingMessage()

// Nota: SelfRegSession y selfRegSessions deben estar declarados en globals.h

static String makeRandomToken() {
  uint32_t r = (uint32_t)esp_random();
  uint32_t m = (uint32_t)millis();
  char buf[32];
  snprintf(buf, sizeof(buf), "%08X%08X", r, m);
  return String(buf);
}

static int findSessionIndexByToken(const String &token) {
  for (int i = 0; i < (int)selfRegSessions.size(); ++i) {
    if (selfRegSessions[i].token == token) return i;
  }
  return -1;
}

static void removeSessionIndex(int idx) {
  if (idx < 0 || idx >= (int)selfRegSessions.size()) return;
  selfRegSessions.erase(selfRegSessions.begin() + idx);
}

static void cleanupExpiredSessions() {
  unsigned long now = millis();
  for (int i = (int)selfRegSessions.size() - 1; i >= 0; --i) {
    if ((long)(now - selfRegSessions[i].createdAtMs) > (long)selfRegSessions[i].ttlMs) {
      selfRegSessions.erase(selfRegSessions.begin() + i);
    }
  }
}

// POST /self_register_start
void handleSelfRegisterStartPOST() {
  cleanupExpiredSessions();

  if (!server.hasArg("uid")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"uid required\"}");
    return;
  }
  String uid = server.arg("uid"); uid.trim();
  String materia = server.hasArg("materia") ? server.arg("materia") : String();

  if (uid.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"uid empty\"}");
    return;
  }

  if (findAnyUserByUID(uid).length() > 0) {
    server.send(409, "application/json", "{\"ok\":false,\"err\":\"uid already registered\"}");
    return;
  }

  // Crear sesión y push_back
  SelfRegSession s;
  s.token = makeRandomToken();
  s.uid = uid;
  s.createdAtMs = millis();
  s.ttlMs = 5UL * 60UL * 1000UL; // 5 minutos
  s.materia = materia;

  selfRegSessions.push_back(s);

  String url = String("/self_register?token=") + s.token;
  String resp = String("{\"ok\":true,\"url\":\"") + url + String("\",\"token\":\"") + s.token + String("\"}");
  server.send(200, "application/json", resp);
}

// GET /self_register?token=...
void handleSelfRegisterGET() {
  cleanupExpiredSessions();

  if (!server.hasArg("token")) {
    server.send(400, "text/plain", "token required");
    return;
  }
  String token = server.arg("token");
  int idx = findSessionIndexByToken(token);
  if (idx < 0) {
    server.send(404, "text/plain", "session not found or expired");
    return;
  }

  SelfRegSession &s = selfRegSessions[idx];

  // Formulario HTML sencillo
  String html = htmlHeader("Auto-registro de Tarjeta");
  html += "<div class='card'><h2>Auto-registro</h2>";
  html += "<p class='small'>Complete su Nombre y Cuenta (7 dígitos). UID prellenado. Esta URL expira en 5 min.</p>";
  html += "<form method='POST' action='/self_register_submit'>";
  html += "<input type='hidden' name='token' value='" + htmlEscape(s.token) + "'>";
  html += "UID:<br><input name='uid' value='" + htmlEscape(s.uid) + "' readonly style='background:#eee'><br>";
  html += "Nombre:<br><input name='name' required><br>";
  html += "Cuenta (7 dígitos):<br><input name='account' required maxlength='7' minlength='7'><br>";

  if (s.materia.length()) {
    html += "Materia (preseleccionada):<br><input name='materia' value='" + htmlEscape(s.materia) + "' readonly style='background:#eee'><br>";
  } else {
    auto courses = loadCourses();
    if (courses.size() > 0) {
      html += "Materia (opcional):<br><select name='materia'><option value=''>-- Ninguna --</option>";
      for (auto &c : courses) {
        html += "<option value='" + htmlEscape(c.materia) + "'>" + htmlEscape(c.materia) + " (" + htmlEscape(c.profesor) + ")</option>";
      }
      html += "</select><br>";
    }
  }

  html += "<div style='margin-top:10px'><button class='btn btn-green' type='submit'>Registrar</button></div>";
  html += "</form></div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// POST /self_register_submit
void handleSelfRegisterPost() {
  cleanupExpiredSessions();

  if (!server.hasArg("token") || !server.hasArg("uid") || !server.hasArg("name") || !server.hasArg("account")) {
    server.send(400, "text/plain", "faltan parametros");
    return;
  }
  String token = server.arg("token"); token.trim();
  String uid = server.arg("uid"); uid.trim();
  String name = server.arg("name"); name.trim();
  String account = server.arg("account"); account.trim();
  String materia = server.hasArg("materia") ? server.arg("materia") : String();
  materia.trim();

  int idx = findSessionIndexByToken(token);
  if (idx < 0) {
    server.send(404, "text/plain", "session invalid or expired");
    return;
  }

  // Validaciones básicas
  if (uid.length() == 0 || name.length() == 0 || account.length() != 7) {
    server.send(400, "text/plain", "datos invalidos");
    return;
  }
  for (size_t i = 0; i < account.length(); ++i) {
    if (!isDigit(account[i])) { server.send(400, "text/plain", "cuenta invalida"); return; }
  }

  // Revisar UID no registrado ya
  if (findAnyUserByUID(uid).length() > 0) {
    removeSessionIndex(idx);
    server.send(409, "text/plain", "UID already registered");
    return;
  }

  // Si materia provista, asegurar que exista
  if (materia.length() && !courseExists(materia)) {
    server.send(400, "text/plain", "Materia no registrada");
    return;
  }

  // Guardar usuario
  String created = nowISO();
  String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"" + created + "\"";
  if (!appendLineToFile(USERS_FILE, line)) {
    server.send(500, "text/plain", "Error guardando usuario");
    return;
  }

  // Guardar attendance (captura_self)
  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + materia + "\"," + "\"captura_self\"";
  if (!appendLineToFile(ATT_FILE, rec)) {
    server.send(500, "text/plain", "Error guardando attendance");
    return;
  }

  // Notificación opcional para admin
  String note = "Auto-registro completado. Usuario: " + name + " (" + account + ")";
  addNotification(uid, name, account, note);

  // Si el token coincide con la sesión que está mostrando QR en display -> limpiar flags
  if (token == currentSelfRegToken) {
    // eliminar session correspondiente
    for (int i = (int)selfRegSessions.size()-1; i >= 0; --i) {
      if (selfRegSessions[i].token == token) selfRegSessions.erase(selfRegSessions.begin() + i);
    }
    awaitingSelfRegister = false;
    currentSelfRegToken = String();
    currentSelfRegUID = String();
    awaitingSinceMs = 0;
    // actualizar display
    showWaitingMessage();
  } else {
    // eliminar sesion por idx (si todavía existe)
    removeSessionIndex(idx);
  }

  // Página de confirmación
  String html = htmlHeader("Registro completado");
  html += "<div class='card'><h2>✅ Registro completado</h2>";
  html += "<p class='small'>Gracias — su tarjeta ha sido registrada correctamente.</p>";
  html += "<p style='margin-top:10px'><a class='btn btn-blue' href='/'>Inicio</a></p></div>" + htmlFooter();
  server.send(200, "text/html", html);
}
