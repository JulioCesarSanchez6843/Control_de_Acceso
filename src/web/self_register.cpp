// src/web/self_register.cpp
#include "self_register.h"
#include "files_utils.h"
#include "globals.h"
#include <SPIFFS.h>
#include "web_utils.h"
#include <algorithm>
#include "display.h"
#include "web_common.h"
#include <ctype.h>

static String makeRandomToken() {
  uint32_t r = (uint32_t)esp_random();
  uint32_t m = (uint32_t)millis();
  char buf[32];
  snprintf(buf, sizeof(buf), "%08X%08X", r, m);
  return String(buf);
}

static String jsEscape(const String &s) {
  String r;
  r.reserve(s.length() * 2);
  for (size_t i = 0; i < (size_t)s.length(); ++i) {
    char c = s[i];
    if (c == '\\') r += "\\\\";
    else if (c == '\'') r += "\\\'";
    else if (c == '"') r += "\\\"";
    else if (c == '\n') r += "\\n";
    else if (c == '\r') r += "\\r";
    else r += c;
  }
  return r;
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

// ---------------- POST /self_register_start ----------------
void handleSelfRegisterStartPOST() {
  cleanupExpiredSessions();

  if (!server.hasArg("uid")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"uid required\"}");
    return;
  }
  String uid = server.arg("uid"); uid.trim();

  if (uid.length() == 0) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"uid empty\"}");
    return;
  }

  // NEW: Reject if uid belongs to a teacher (defensive)
  String trow = findTeacherByUID(uid);
  if (trow.length() > 0) {
    // don't create session or URL for teachers
    server.send(409, "application/json", "{\"ok\":false,\"err\":\"uid is teacher\"}");
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
  s.ttlMs = 5UL * 60UL * 1000UL;
  s.materia = ""; // Ya no manejamos materia aquí

  selfRegSessions.push_back(s);

  String url = String("/self_register?token=") + s.token;
  String resp = String("{\"ok\":true,\"url\":\"") + url + String("\",\"token\":\"") + s.token + String("\"}");
  server.send(200, "application/json", resp);
}

// ---------------- GET /self_register?token=... (page) ----------------
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

  String html;
  html.reserve(1200);
  html  = "<!doctype html><html lang='es'><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta charset='utf-8'><title>Auto-registro</title>";
  html += "<style>body{font-family:Arial,Helvetica,sans-serif;background:#071026;color:#fff;margin:0;padding:12px} .card{background:#0f1724;padding:12px;border-radius:8px} label{display:block;margin-top:8px} input{width:100%;padding:8px;border-radius:6px;border:1px solid #334155;background:#071026;color:#fff;box-sizing:border-box} .btn{padding:8px 12px;border-radius:6px;border:none;margin-top:12px} .small{font-size:13px;color:#cbd5e1;margin-top:6px}</style>";
  html += "</head><body>";
  html += "<div class='card'><h2>Registro rápido</h2>";
  html += "<div class='small'>Complete Nombre y Cuenta (7 dígitos). UID prellenado. Esta página expira pronto.</div>";

  html += "<form method='POST' action='/self_register_submit' id='srForm'>";
  html += "<input type='hidden' name='token' value='" + htmlEscape(s.token) + "'>";
  html += "<label>UID</label>";
  html += "<input name='uid' readonly value='" + htmlEscape(s.uid) + "'>";
  html += "<label>Nombre</label>";
  html += "<input name='name' required placeholder='Nombre completo'>";
  html += "<label>Cuenta (7 dígitos)</label>";
  html += "<input name='account' inputmode='numeric' pattern='[0-9]{7}' maxlength='7' minlength='7' placeholder='Ej: 2123456'>";

  // Campo materia oculto - será asignado después por el administrador
  html += "<input type='hidden' name='materia' value=''>";

  html += "<div style='display:flex;gap:8px;'><button class='btn' type='submit' style='background:#10b981;color:#04201b'>Registrar</button></div>";
  html += "<div id='msg' class='small' style='display:none;margin-top:8px;color:#ffdede;'></div>";
  html += "</form></div>";

  html += "<script>";
  html += "document.addEventListener('DOMContentLoaded',function(){";
  html += "  var f=document.getElementById('srForm');";
  html += "  f.addEventListener('submit', function(ev){";
  html += "    var acc=f.account.value.trim();";
  html += "    var name=f.name.value.trim();";
  html += "    if(!name){ ev.preventDefault(); showMsg('Por favor escriba su nombre.'); return false;}";
  html += "    if(!/^[0-9]{7}$/.test(acc)){ ev.preventDefault(); showMsg('Cuenta inválida: debe tener 7 dígitos.'); return false;}";
  html += "    var btn=f.querySelector('button[type=submit]'); if(btn) btn.disabled=true;";
  html += "    return true;";
  html += "  });";
  html += "});";
  html += "function showMsg(t){ var d=document.getElementById('msg'); d.style.display='block'; d.textContent = t; }";
  html += "</script>";

  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ---------------- POST /self_register_submit ----------------
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

  int idx = findSelfRegSessionIndexByToken(token);
  if (idx < 0) {
    server.send(404, "text/plain", "session invalid or expired");
    return;
  }

  if (uid.length() == 0 || name.length() == 0 || account.length() != 7) {
    server.send(400, "text/plain", "datos invalidos");
    return;
  }
  for (size_t i = 0; i < account.length(); ++i) {
    if (!isdigit(account[i])) { server.send(400, "text/plain", "cuenta invalida"); return; }
  }

  if (findAnyUserByUID(uid).length() > 0) {
    removeSelfRegSessionByIndex(idx);
    server.send(409, "text/plain", "UID already registered");
    return;
  }

  // Guardar usuario SIN materia - será asignada después por el administrador
  String created = nowISO();
  String line = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"\"," + "\"" + created + "\"";
  if (!appendLineToFile(USERS_FILE, line)) {
    server.send(500, "text/plain", "Error guardando usuario");
    return;
  }

  // Guardar attendance SIN materia por ahora
  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"\"," + "\"captura_self\"";
  if (!appendLineToFile(ATT_FILE, rec)) {
    server.send(500, "text/plain", "Error guardando attendance");
    return;
  }

  String note = "Auto-registro completado. Usuario: " + name + " (" + account + ") - Materia pendiente de asignar";
  addNotification(uid, name, account, note);

  removeSelfRegSessionByIndex(idx);

  if (token == currentSelfRegToken) {
    awaitingSelfRegister = false;
    currentSelfRegToken = String();
    currentSelfRegUID = String();
    awaitingSinceMs = 0;
    showWaitingMessage();
  }

  // Página de confirmación MEJORADA con diseño atractivo
  String html;
  html.reserve(800);
  html  = "<!doctype html><html lang='es'><head><meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<meta charset='utf-8'><title>Registro Completado</title>";
  html += "<style>";
  html += "body{font-family:Arial,Helvetica,sans-serif;background:#071026;color:#fff;margin:0;padding:20px;display:flex;justify-content:center;align-items:center;min-height:100vh;}";
  html += ".success-card{background:#0f1b2e;padding:30px;border-radius:12px;text-align:center;box-shadow:0 8px 25px rgba(0,0,0,0.3);border:1px solid #1e3a5f;max-width:400px;width:100%;}";
  html += ".success-icon{font-size:64px;margin-bottom:20px;color:#10b981;text-shadow:0 0 20px rgba(16,185,129,0.5);}";
  html += ".success-title{font-size:24px;font-weight:bold;margin-bottom:15px;color:#10b981;}";
  html += ".success-message{font-size:16px;color:#cbd5e1;line-height:1.5;margin-bottom:25px;}";
  html += ".success-note{font-size:14px;color:#94a3b8;font-style:italic;margin-top:20px;padding-top:15px;border-top:1px solid #1e3a5f;}";
  html += "</style>";
  html += "</head><body>";
  html += "<div class='success-card'>";
  html += "<div class='success-icon'>✓</div>";
  html += "<div class='success-title'>Registro Completado</div>";
  html += "<div class='success-message'>";
  html += "Su registro ha sido exitoso.<br>Gracias por completar el proceso.";
  html += "</div>";
  html += "<div class='success-note'>Puede cerrar esta página</div>";
  html += "</div>";
  html += "<script>";
  html += "setTimeout(function(){ try{ window.close(); }catch(e){} }, 3000);";
  html += "</script>";
  html += "</body></html>";

  server.send(200, "text/html", html);
}

// ---------------- POST /self_register_cancel ----------------
void handleSelfRegisterCancelPOST() {
  if (!server.hasArg("token")) {
    server.send(400, "application/json", "{\"ok\":false,\"err\":\"token required\"}");
    return;
  }
  String token = server.arg("token"); token.trim();
  int idx = findSelfRegSessionIndexByToken(token);
  if (idx >= 0) removeSelfRegSessionByIndex(idx);

  if (token == currentSelfRegToken) {
    awaitingSelfRegister = false;
    currentSelfRegToken = String();
    currentSelfRegUID = String();
    awaitingSinceMs = 0;
    showWaitingMessage();
  }

  server.send(200, "application/json", "{\"ok\":true}");
}

// Las otras funciones las mantenemos por si se usan en otro lugar
void handleSelfRegisterPollGET() {
  server.send(200, "application/json", "{\"ok\":true,\"status\":\"active\"}");
}

void handleSelfRegisterSetMateriaPOST() {
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Materia assignment handled in batch\"}");
}

void handleSelfRegisterSetMateriaBatchPOST() {
  server.send(200, "application/json", "{\"ok\":true,\"message\":\"Materia assignment handled in batch\"}");
}
