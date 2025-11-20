// src/web/self_register.cpp
#include "self_register.h"
#include "files_utils.h"
#include "globals.h"
#include <SPIFFS.h>
#include "web_utils.h"   // para htmlEscape(), csvEscape(), etc.
#include <algorithm>
#include "display.h"     // para showWaitingMessage()
#include "web_common.h"  // no usamos header/footer aquí pero a veces helpers
#include <ctype.h>

// Nota: SelfRegSession y selfRegSessions deben estar declarados en globals.h

static String makeRandomToken() {
  uint32_t r = (uint32_t)esp_random();
  uint32_t m = (uint32_t)millis();
  char buf[32];
  snprintf(buf, sizeof(buf), "%08X%08X", r, m);
  return String(buf);
}

int findSelfRegSessionIndexByToken(const String &token) {
  for (int i = 0; i < (int)selfRegSessions.size(); ++i) {
    if (selfRegSessions[i].token == token) return i;
  }
  return -1;
}

void removeSelfRegSessionByIndex(int idx) {
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

// Helper: extrae la parte "materia" si owner viene como "Materia||Profesor"
static String baseMateriaFromOwner(const String &owner) {
  int idx = owner.indexOf("||");
  if (idx < 0) {
    String o = owner; o.trim(); return o;
  }
  String b = owner.substring(0, idx);
  b.trim();
  return b;
}

// POST /self_register_start (profesor crea sesión)
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
  int idx = findSelfRegSessionIndexByToken(token);
  if (idx < 0) {
    server.send(404, "text/plain", "session not found or expired");
    return;
  }

  SelfRegSession &s = selfRegSessions[idx];

  // Determinar materia actual por horario (si hay)
  String currOwner = currentScheduledMateria(); // puede venir "Materia" o "Materia||Profesor"
  String currMateriaBase = baseMateriaFromOwner(currOwner);

  // Construir HTML minimalista (responsive)
  String html;
  html  = "<!doctype html><html lang='es'><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta charset='utf-8'><title>Auto-registro</title>";
  html += "<style>"
          "body{font-family:Arial,Helvetica,sans-serif;background:#0b1220;color:#fff;margin:0;padding:12px;}"
          ".card{background:#0f1724;padding:14px;border-radius:8px;box-shadow:0 2px 6px rgba(0,0,0,0.4);}"
          "h1{font-size:18px;margin:0 0 8px 0;color:#fff}"
          ".small{font-size:13px;color:#cbd5e1;margin-bottom:10px}"
          "label{display:block;font-size:13px;margin-top:8px;margin-bottom:3px}"
          "input[type=text],input[type=tel],select{width:100%;padding:8px;border-radius:6px;border:1px solid #334155;background:#071026;color:#fff;box-sizing:border-box}"
          ".btn{display:inline-block;padding:10px 14px;border-radius:6px;border:none;font-weight:600;margin-top:12px;cursor:pointer;text-decoration:none}"
          ".btn-green{background:#10b981;color:#04201b}"
          ".msg{margin-top:8px;padding:8px;border-radius:6px;background:#05203b;color:#dbeafe}"
          "</style></head><body>";
  html += "<div class='card'>";
  html += "<h1>Registro rápido</h1>";
  html += "<div class='small'>Complete Nombre y Cuenta (7 dígitos). UID prellenado. Esta página expira en breve.</div>";

  // Form
  html += "<form method='POST' action='/self_register_submit' id='srForm'>";
  html += "<input type='hidden' name='token' value='" + htmlEscape(s.token) + "'>";
  html += "<label>UID</label>";
  html += "<input name='uid' readonly value='" + htmlEscape(s.uid) + "'>";
  html += "<label>Nombre</label>";
  html += "<input name='name' required placeholder='Nombre completo'>";
  html += "<label>Cuenta (7 dígitos)</label>";
  html += "<input name='account' inputmode='numeric' pattern='[0-9]{7}' maxlength='7' minlength='7' placeholder='Ej: 2123456'>";

  if (currMateriaBase.length() > 0) {
    html += "<label>Materia (en sesión ahora)</label>";
    html += "<input name='materia' readonly value='" + htmlEscape(currMateriaBase) + "'>";
  } else {
    auto courses = loadCourses();
    if (courses.size() > 0) {
      html += "<label>Materia (opcional)</label>";
      html += "<select name='materia'><option value=''>-- Ninguna --</option>";
      for (auto &c : courses) {
        html += "<option value='" + htmlEscape(c.materia) + "'>" + htmlEscape(c.materia) + (c.profesor.length() ? String(" (") + htmlEscape(c.profesor) + ")" : "") + "</option>";
      }
      html += "</select>";
    } else {
      html += "<div class='small'>No hay materias registradas.</div>";
      html += "<input type='hidden' name='materia' value=''>";
    }
  }

  html += "<div style='display:flex;gap:8px;flex-wrap:wrap;align-items:center;'>";
  html += "<button class='btn btn-green' type='submit'>Registrar</button>";
  // NO mostramos botón 'Cerrar' ni 'Cancelar' para evitar interacción que quite QR; si quieres cancelar, usar endpoint específico.
  html += "</div>";

  html += "<div id='msg' class='msg' style='display:none;'></div>";
  html += "</form>";

  // JS: validar cuenta es numérico de 7 y mostrar errores en línea
  html += "<script>"
          "const f=document.getElementById('srForm');"
          "f.addEventListener('submit', function(ev){"
          "  var acc = f.account.value.trim();"
          "  var name = f.name.value.trim();"
          "  if(!name){ ev.preventDefault(); showMsg('Por favor escriba su nombre.'); return false; }"
          "  if(!/^[0-9]{7}$/.test(acc)){ ev.preventDefault(); showMsg('Cuenta inválida: debe tener 7 dígitos.'); return false; }"
          "  var btn = f.querySelector('button[type=submit]'); if(btn) btn.disabled = true;"
          "  return true;"
          "});"
          "function showMsg(t){ var d=document.getElementById('msg'); d.style.display='block'; d.textContent = t; }"
          "</script>";

  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

// POST /self_register_cancel
// Cancela la sesión (token) y, si coincide con el QR activo, limpia flags y actualiza display.
void handleSelfRegisterCancelPOST() {
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"token required\"}");
    return;
  }
  String token = server.arg("token"); token.trim();
  int idx = findSelfRegSessionIndexByToken(token);
  if (idx >= 0) removeSelfRegSessionByIndex(idx);

  // Si coincide con currentSelfRegToken limpiar flags y volver a pantalla de espera
  if (token == currentSelfRegToken) {
    awaitingSelfRegister = false;
    currentSelfRegToken = String();
    currentSelfRegUID = String();
    awaitingSinceMs = 0;
    showWaitingMessage();
  }

  server.send(200, "application/json", "{\"ok\":true}");
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

  int idx = findSelfRegSessionIndexByToken(token);
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
    removeSelfRegSessionByIndex(idx);
    server.send(409, "text/plain", "UID already registered");
    return;
  }

  // Si hay materia en curso, forzarla (no permitir cambiar)
  String currOwner = currentScheduledMateria();
  String currMateriaBase = baseMateriaFromOwner(currOwner);
  if (currMateriaBase.length() > 0) {
    if (materia.length() > 0 && materia != currMateriaBase) {
      server.send(400, "text/plain", "No puede cambiar la materia: hay una sesión en curso");
      return;
    }
    materia = currMateriaBase;
  } else {
    if (materia.length() && !courseExists(materia)) {
      server.send(400, "text/plain", "Materia no registrada");
      return;
    }
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

  // Notificación para admin
  String note = "Auto-registro completado. Usuario: " + name + " (" + account + ")";
  addNotification(uid, name, account, note);

  // Eliminar la sesión (si exista)
  removeSelfRegSessionByIndex(idx);

  // Si por algún motivo el token coincidiera con currentSelfRegToken (caso anterior), limpiar flags.
  if (token == currentSelfRegToken) {
    awaitingSelfRegister = false;
    currentSelfRegToken = String();
    currentSelfRegUID = String();
    awaitingSinceMs = 0;
    showWaitingMessage();
  }

  // Página de confirmación MINIMALISTA (sin enlaces al sistema)
  String html;
  html  = "<!doctype html><html lang='es'><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta charset='utf-8'><title>Registro completado</title>";
  html += "<style>body{font-family:Arial,Helvetica,sans-serif;background:#071026;color:#fff;margin:0;padding:12px;} .card{background:#07203a;padding:14px;border-radius:8px;} .small{font-size:13px;color:#cfe9ff;margin-top:6px;}</style></head><body>";
  html += "<div class='card'><h2>✅ Registro completado</h2>";
  html += "<div class='small'>Gracias — su tarjeta ha sido registrada correctamente.<br>Puede cerrar esta página. (Se cerrará automáticamente en 5 s)</div>";
  // bloqueo "back": añadimos pushState y onpopstate para impedir volver al form
  html += "<script>"
          "try{ history.pushState(null,'',location.href); window.onpopstate = function(){ history.pushState(null,'',location.href); }; }catch(e){}"
          "setTimeout(function(){ try{ window.close(); }catch(e){ /* si no se puede cerrar, nada */ } },5000);"
          "</script>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}
