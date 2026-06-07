// src/web/notifications.cpp
#include "notifications.h"
#include "web_common.h"
#include "config.h"
#include "globals.h"
#include "db_sync.h"

#include <ArduinoJson.h>
#include <algorithm>
#include <vector>

// =========================================================
// Notificaciones 100% desde base de datos
// - Mostrar: listNotificaciones()
// - Leído/no leído: columna leida
// - Limpiar: borrar cada notif en el servidor
// =========================================================

struct OracleNotifRec {
  int id = -1;
  String ts;
  String uid;
  String name;
  String account;
  String note;
  int leida = 0;
};

static String htmlEscape(const String &s) {
  String r;
  r.reserve(s.length());
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (c == '&') r += "&amp;";
    else if (c == '<') r += "&lt;";
    else if (c == '>') r += "&gt;";
    else if (c == '"') r += "&quot;";
    else if (c == '\'') r += "&#39;";
    else r += c;
  }
  return r;
}

static String base64Encode(const String &in) {
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  const unsigned char *bytes = (const unsigned char *)in.c_str();
  int val = 0, valb = -6;

  for (size_t i = 0; i < in.length(); ++i) {
    val = (val << 8) + bytes[i];
    valb += 8;
    while (valb >= 0) {
      out += b64[(val >> valb) & 0x3F];
      valb -= 6;
    }
  }

  if (valb > -6) out += b64[((val << 8) >> (valb + 8)) & 0x3F];
  while (out.length() % 4) out += '=';
  return out;
}

static String detectNotificationType(const String &note) {
  String n = note;
  n.toLowerCase();

  if (n.indexOf("tarjeta") >= 0 && (n.indexOf("no registrada") >= 0 || n.indexOf("no registrado") >= 0 || n.indexOf("desconocida") >= 0)) {
    return String("Tarjeta desconocida");
  }
  if (n.indexOf("intento fuera de materia") >= 0 || n.indexOf("fuera de materia") >= 0 || n.indexOf("no pertenece") >= 0) {
    return String("Alerta (Denegado)");
  }
  if (n.indexOf("entrada fuera de horario") >= 0 || n.indexOf("fuera de horario") >= 0 || n.indexOf("no hay clase") >= 0) {
    if (n.indexOf("maestro") >= 0 || n.indexOf("teacher") >= 0) return String("Informativa (Maestro)");
    return String("Informativa (Alumno)");
  }
  if (n.indexOf("maestro") >= 0 || n.indexOf("teacher") >= 0) return String("Informativa (Maestro)");
  return String("Otro");
}

static String extractFieldFromNote(const String &note, const String &keysCSV) {
  String tmp = keysCSV;
  std::vector<String> keys;

  while (tmp.length()) {
    int c = tmp.indexOf(',');
    if (c < 0) {
      String k = tmp;
      k.trim();
      if (k.length()) keys.push_back(k);
      break;
    }
    String k = tmp.substring(0, c);
    k.trim();
    if (k.length()) keys.push_back(k);
    tmp = tmp.substring(c + 1);
  }

  String low = note;
  low.toLowerCase();

  for (auto &k : keys) {
    String lk = k;
    lk.toLowerCase();
    int pos = low.indexOf(lk);
    if (pos >= 0) {
      int start = pos + lk.length();
      while (start < (int)note.length() && (note[start] == ':' || note[start] == '-' || note[start] == ' ')) start++;
      int end = start;
      while (end < (int)note.length()) {
        char ch = note[end];
        if (ch == '\n' || ch == '\r' || ch == ',' || ch == ';' || ch == '.' || ch == '*' || ch == '-') break;
        end++;
      }
      String val = note.substring(start, end);
      val.trim();
      return val;
    }
  }

  return String();
}

static bool parseNotificationsJson(const String &body, std::vector<OracleNotifRec> &out) {
  out.clear();
  if (!body.length()) return false;

  size_t cap = body.length() * 2 + 1024;
  if (cap < 4096) cap = 4096;

  DynamicJsonDocument doc(cap);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    Serial.print("WARN: no se pudo parsear listNotificaciones(): ");
    Serial.println(err.c_str());
    return false;
  }

  auto loadObj = [&](JsonObjectConst obj) {
    OracleNotifRec r;
    r.id = obj["id"] | -1;
    r.ts = obj["fecha_hora"] | obj["ts"] | obj["created_at"] | "";
    r.uid = obj["rfid_uid"] | obj["uid"] | "";
    r.name = obj["name"] | obj["nombre"] | "";
    r.account = obj["account"] | obj["cuenta"] | "";
    r.note = obj["note"] | obj["nota"] | "";
    r.leida = obj["leida"] | 0;
    out.push_back(r);
  };

  if (doc.is<JsonArray>()) {
    for (JsonVariantConst v : doc.as<JsonArrayConst>()) {
      if (v.is<JsonObjectConst>()) loadObj(v.as<JsonObjectConst>());
    }
    return true;
  }

  if (doc.is<JsonObject>()) {
    JsonObjectConst root = doc.as<JsonObjectConst>();

    const char* wrappers[] = {"data", "rows", "items", "result", "notificaciones"};
    for (const char* key : wrappers) {
      if (root.containsKey(key) && root[key].is<JsonArray>()) {
        for (JsonVariantConst v : root[key].as<JsonArrayConst>()) {
          if (v.is<JsonObjectConst>()) loadObj(v.as<JsonObjectConst>());
        }
        return true;
      }
    }

    loadObj(root);
    return true;
  }

  return false;
}

static bool loadOracleNotifications(std::vector<OracleNotifRec> &out) {
  // La petición debe venir desde la BD, no desde SPIFFS
  String body = listNotificaciones(false);
  if (!body.length()) return false;
  return parseNotificationsJson(body, out);
}

static bool findNotificationsByTuple(const String &ts, const String &uid, const String &note, std::vector<OracleNotifRec> &matches) {
  matches.clear();
  std::vector<OracleNotifRec> all;
  if (!loadOracleNotifications(all)) return false;

  for (auto &r : all) {
    if (r.ts == ts && r.uid == uid && r.note == note) {
      matches.push_back(r);
    }
  }
  return true;
}

static bool deleteNotifOnServerById(int id) {
  if (id <= 0) return false;
  bool ok = deleteNotificacionById(id);
  if (ok) {
    Serial.print("DB_SYNC: notificación eliminada id=");
    Serial.println(id);
  } else {
    Serial.print("WARN: no se pudo eliminar notificación id=");
    Serial.println(id);
  }
  return ok;
}

static bool updateNotifReadStateOnServer(const OracleNotifRec &r, bool readState) {
  if (r.id <= 0) return false;
  bool ok = updateNotificacionById(r.id, r.ts, r.uid, r.name, r.account, r.note, readState ? 1 : 0);
  if (ok) {
    Serial.print("DB_SYNC: notificación actualizada id=");
    Serial.println(r.id);
  } else {
    Serial.print("WARN: no se pudo actualizar notificación id=");
    Serial.println(r.id);
  }
  return ok;
}

static bool clearNotificationsOnServer() {
  std::vector<OracleNotifRec> rows;
  if (!loadOracleNotifications(rows)) {
    Serial.println("WARN: no se pudieron cargar notificaciones desde la BD para borrar todas");
    return false;
  }

  bool ok = true;
  for (auto &r : rows) {
    if (r.id > 0) {
      if (!deleteNotifOnServerById(r.id)) ok = false;
    }
  }
  return ok;
}

static String inferProfileLink(const OracleNotifRec &r) {
  String low = r.note;
  low.toLowerCase();

  if (low.indexOf("maestro") >= 0 || low.indexOf("teacher") >= 0) {
    return String("/teachers_all?search_uid=") + r.uid;
  }
  return String("/students_all?search_uid=") + r.uid;
}

static String generateNotificationsHTML(bool showUnreadOnly) {
  std::vector<OracleNotifRec> notifs;
  bool loaded = loadOracleNotifications(notifs);

  if (!loaded) {
    return String("<p>No se pudieron cargar las notificaciones desde la base de datos.</p>");
  }

  if (notifs.size() > 1) std::reverse(notifs.begin(), notifs.end());

  size_t unreadCount = 0;
  size_t readCount = 0;

  for (auto &r : notifs) {
    if (r.leida) readCount++;
    else unreadCount++;
  }

  String html;

  if (showUnreadOnly) {
    html += "<h2>Notificaciones No Leídas <span id='unread_badge' style='background:#ef4444;color:#fff;padding:6px 10px;border-radius:999px;font-weight:800;font-size:0.95em;'>" + String(unreadCount) + "</span></h2>";
  } else {
    html += "<h2>Notificaciones Leídas <span id='read_badge' style='background:#1d4ed8;color:#fff;padding:6px 10px;border-radius:999px;font-weight:800;font-size:0.95em;'>" + String(readCount) + "</span></h2>";
  }

  html += "<p class='small'>Esta vista toma el estado de leído directamente desde la columna <b>leida</b> de la base de datos.</p>";

  html += "<div style='margin-bottom:12px;display:flex;gap:8px;align-items:center;flex-wrap:wrap;'>";
  html += "<form method='POST' action='/notifications_clear' onsubmit='return confirm(\"¿Borrar todas las notificaciones? Esta acción es irreversible.\");' style='display:inline'>";
  html += "<input class='btn btn-red' type='submit' value='🗑️ Borrar Todas'></form>";
  html += "<a class='btn btn-blue' href='/'>Inicio</a>";
  html += "<div style='margin-left:auto;display:flex;align-items:center;gap:8px;'>";
  html += "<span id='status_message' style='display:none;background:#10b981;color:white;padding:6px 12px;border-radius:6px;font-weight:600;font-size:0.9em;'></span>";
  if (showUnreadOnly) html += "<a class='btn btn-blue' href='/notifications_read'>Ver Leídas (" + String(readCount) + ")</a>";
  else html += "<a class='btn btn-blue' href='/notifications'>Ver No Leídas (" + String(unreadCount) + ")</a>";
  html += "</div></div>";

  html += "<div class='filters' style='margin-bottom:10px;display:flex;gap:8px;flex-wrap:wrap;align-items:center;'>";
  html += "<input id='nf_materia' placeholder='Filtrar por materia' style='min-width:160px'>";
  html += "<input id='nf_prof' placeholder='Filtrar por profesor' style='min-width:160px'>";
  html += "<input id='nf_name' placeholder='Filtrar por nombre' style='min-width:160px'>";
  html += "<input id='nf_date' type='date' placeholder='Filtrar por fecha' style='min-width:140px'>";
  html += "<button class='search-btn btn btn-blue' onclick='applyNotifFilters()'>Aplicar</button>";
  html += "<button class='search-btn btn btn-green' onclick='clearNotifFilters()'>Limpiar</button>";
  html += "</div>";

  html += R"rawliteral(
<style>
  .notif-list { display:flex; flex-direction:column; gap:12px; }
  .notif-item { padding:12px; border-radius:10px; box-shadow:0 6px 18px rgba(2,6,23,0.06); transition:transform .12s ease; position:relative; overflow:hidden; display:flex; flex-direction:column; cursor:pointer; border:1px solid #e2e8f0; }
  .notif-item:hover { transform:translateY(-4px); box-shadow:0 8px 25px rgba(2,6,23,0.1); }
  .notif-header { display:flex; justify-content:space-between; align-items:flex-start; gap:8px; }
  .notif-type { font-weight:800; padding:6px 10px; border-radius:8px; font-size:0.95em; display:inline-block; }
  .notif-meta { font-size:0.86em; color:#374151; margin-top:8px; margin-bottom:8px; }
  .notif-note { font-size:1em; color:#0f172a; white-space:pre-wrap; margin-top:6px; }
  .compact-actions { display:flex; gap:8px; align-items:center; }
  .badge-new { background:#ef4444; color:white; padding:6px 10px; border-radius:8px; font-weight:800; font-size:0.95em; display:inline-block; }
  .btn-mark-inline { font-size:0.9em; padding:6px 10px; }
  .modal-backdrop { position:fixed; left:0; right:0; top:0; bottom:0; background:rgba(2,6,23,0.35); display:none; align-items:center; justify-content:center; z-index:1200; }
  .modal-card { background:#fff; border-radius:12px; width:min(600px,92%); max-width:600px; padding:20px; box-shadow:0 20px 60px rgba(2,6,23,0.35); max-height:85vh; overflow-y:auto; }
  .modal-header { display:flex; justify-content:space-between; align-items:flex-start; gap:12px; margin-bottom:15px; }
  .modal-title { font-weight:900; font-size:1.2em; color:#0f172a; }
  .modal-body { margin-top:10px; color:#0f172a; font-size:1em; max-height:320px; overflow:auto; white-space:pre-wrap; line-height:1.5; }
  .modal-meta-big { margin-top:15px; font-size:0.95em; color:#475569; background:#f8fafc; padding:12px; border-radius:8px; }
  .modal-actions { display:flex; gap:8px; justify-content:flex-end; margin-top:20px; flex-wrap:wrap; }
  @media (max-width:900px) {
    .compact-actions { flex-direction:row; }
    .modal-actions { flex-direction:column; }
    .modal-card { padding:15px; }
  }
</style>
)rawliteral";

  html += "<div id='notif_list' class='notif-list'>";

  bool anyShown = false;

  for (size_t i = 0; i < notifs.size(); ++i) {
    const auto &r = notifs[i];
    bool isRead = (r.leida != 0);

    if (showUnreadOnly && isRead) continue;
    if (!showUnreadOnly && !isRead) continue;

    anyShown = true;

    String tipo = detectNotificationType(r.note);
    String materiaFromNote = extractFieldFromNote(r.note, "Materia,Materia:,materia");
    String profFromNote = extractFieldFromNote(r.note, "Profesor,Profesor:,Maestro,Maestro:,Teacher,Teacher:");

    String bg = "#fff";
    String badgeBg = "#E2E8F0";
    String badgeColor = "#0f172a";

    if (isRead) {
      bg = "#FAFAFA";
      if (tipo.indexOf("Alerta") == 0 || tipo == "Tarjeta desconocida") bg = "#fff7f7";
      else if (tipo.indexOf("Informativa (Alumno)") == 0) bg = "#fffeef";
      else if (tipo.indexOf("Informativa (Maestro)") == 0) bg = "#f6fff6";
    } else {
      if (tipo.indexOf("Alerta") == 0 || tipo == "Tarjeta desconocida") { bg = "#fff4f4"; badgeBg = "#ffcccc"; badgeColor = "#7a1f1f"; }
      else if (tipo.indexOf("Informativa (Alumno)") == 0) { bg = "#fffef0"; badgeBg = "#fff3bf"; badgeColor = "#664d03"; }
      else if (tipo.indexOf("Informativa (Maestro)") == 0) { bg = "#f0fff0"; badgeBg = "#c7f9d6"; badgeColor = "#065f46"; }
    }

    String noteEsc = htmlEscape(r.note);
    String nameEsc = htmlEscape(r.name);
    String accEsc = htmlEscape(r.account);
    String tsEsc = htmlEscape(r.ts);
    String uidEsc = htmlEscape(r.uid);
    String tipoEsc = htmlEscape(tipo);
    String profileLink = inferProfileLink(r);
    String noteB64 = base64Encode(r.note);

    html += "<div class='notif-item' style='background:" + bg + ";' data-idx='" + String((int)i) + "'";
    html += " data-id='" + String(r.id) + "'";
    html += " data-ts='" + tsEsc + "'";
    html += " data-uid='" + uidEsc + "'";
    html += " data-name='" + nameEsc + "'";
    html += " data-account='" + accEsc + "'";
    html += " data-note-html='" + noteEsc + "'";
    html += " data-type='" + tipoEsc + "'";
    html += " data-isread='" + String(isRead ? 1 : 0) + "'";
    if (materiaFromNote.length()) html += " data-materia='" + htmlEscape(materiaFromNote) + "'";
    if (profFromNote.length()) html += " data-prof='" + htmlEscape(profFromNote) + "'";
    html += ">";

    html += "<div class='notif-header'>";
    html += "<div style='display:flex;flex-direction:column;gap:6px;'><span class='notif-type' style='background:" + badgeBg + ";color:" + badgeColor + ";'>" + tipo + "</span></div>";
    html += "<div style='display:flex;align-items:center;gap:8px;'>";
    if (!isRead) html += "<div class='badge-new'>Nuevo</div>";
    html += "<div class='compact-actions'>";
    if (isRead) {
      html += "<button type='button' class='btn btn-orange btn-mark-inline' onclick='markInline(" + String((int)i) + ", false, event)'>Marcar no leído</button>";
    } else {
      html += "<button type='button' class='btn btn-green btn-mark-inline' onclick='markInline(" + String((int)i) + ", true, event)'>Marcar leído</button>";
    }
    html += "</div></div></div>";

    String meta = tsEsc;
    if (nameEsc.length()) meta += " • " + nameEsc;
    if (accEsc.length()) meta += " • " + accEsc;
    if (materiaFromNote.length()) meta += " • " + htmlEscape(materiaFromNote);

    html += "<div class='notif-meta'>" + meta + "</div>";
    html += "<div class='notif-note'>" + (noteEsc.length() ? noteEsc : "<i>(sin detalles)</i>") + "</div>";

    html += "<div id='notif_id_" + String((int)i) + "' style='display:none'>" + String(r.id) + "</div>";
    html += "<div id='notif_uid_" + String((int)i) + "' style='display:none'>" + uidEsc + "</div>";
    html += "<div id='notif_ts_" + String((int)i) + "' style='display:none'>" + tsEsc + "</div>";
    html += "<div id='notif_name_" + String((int)i) + "' style='display:none'>" + nameEsc + "</div>";
    html += "<div id='notif_acc_" + String((int)i) + "' style='display:none'>" + accEsc + "</div>";
    html += "<div id='notif_note_enc_" + String((int)i) + "' style='display:none'>" + noteB64 + "</div>";
    html += "<div id='notif_is_read_" + String((int)i) + "' style='display:none'>" + String(isRead ? 1 : 0) + "</div>";
    html += "<div id='notif_profile_" + String((int)i) + "' style='display:none'>" + htmlEscape(profileLink) + "</div>";

    html += "</div>";
  }

  if (!anyShown) {
    html += showUnreadOnly ? "<p>No hay notificaciones no leídas.</p>" : "<p>No hay notificaciones leídas.</p>";
  }

  html += "</div>";

  html += R"rawliteral(
<div id="modal_back" class="modal-backdrop" role="dialog" aria-modal="true">
  <div class="modal-card" role="document" aria-labelledby="modal_type" aria-describedby="modal_body">
    <div class="modal-header">
      <div>
        <div class="modal-title" id="modal_type">Notificación</div>
        <div style="font-size:0.9em;color:#475569" id="modal_meta"></div>
      </div>
      <div><button type="button" class="btn btn-red" onclick="closeModal()" id="modal_close_btn" aria-label="Cerrar">✕</button></div>
    </div>
    <div class="modal-body" id="modal_body" tabindex="0"></div>
    <div class="modal-meta-big" id="modal_biginfo"></div>
    <div class="modal-actions" id="modal_actions">
      <a id="modal_profile_link" class="btn btn-blue" target="_blank" rel="noopener">Ver perfil</a>
      <a id="modal_history_link" class="btn btn-purple" target="_blank" rel="noopener">Ver historial</a>
      <button id="modal_mark_btn" type="button" class="btn btn-green" onclick="markCurrentNotification()">Marcar leído</button>
      <button id="modal_unmark_btn" type="button" class="btn btn-orange" style="display:none;" onclick="unmarkCurrentNotification()">Marcar no leído</button>
      <button id="modal_delete_btn" type="button" class="btn btn-red" onclick="deleteCurrentNotification()">Eliminar</button>
    </div>
  </div>
</div>
)rawliteral";

  html += R"rawliteral(
<script>
  var currentIdx = -1;
  var currentNotificationData = null;

  document.addEventListener('DOMContentLoaded', function() {
    var list = document.getElementById('notif_list');
    if (list) {
      list.addEventListener('click', function(e) {
        var btn = e.target.closest('button');
        if (btn && btn.classList.contains('btn-mark-inline')) {
          var item = e.target.closest('.notif-item');
          if (!item) return;
          var idx = item.getAttribute('data-idx');
          if (!idx) return;
          var markAsRead = btn.classList.contains('btn-green');
          markInline(parseInt(idx, 10), markAsRead, e);
          return;
        }

        var item = e.target.closest('.notif-item');
        if (!item) return;
        var idx = item.getAttribute('data-idx');
        if (!idx) return;
        openNotif(parseInt(idx, 10), e);
      });
    }

    var mb = document.getElementById('modal_back');
    if (mb) {
      mb.addEventListener('click', function(e) {
        if (e.target.id === 'modal_back') closeModal();
      });
    }
  });

  function applyNotifFilters(){
    var fm = document.getElementById('nf_materia').value.trim().toLowerCase();
    var fp = document.getElementById('nf_prof').value.trim().toLowerCase();
    var fn = document.getElementById('nf_name').value.trim().toLowerCase();
    var fdate = document.getElementById('nf_date').value.trim();

    var items = document.querySelectorAll('#notif_list .notif-item');
    for (var i = 0; i < items.length; i++) {
      var item = items[i];
      var ts = (item.getAttribute('data-ts') || '').toLowerCase();
      var name = (item.getAttribute('data-name') || '').toLowerCase();
      var note = (item.getAttribute('data-note-html') || '').toLowerCase();
      var mat = (item.getAttribute('data-materia') || '').toLowerCase();
      var prof = (item.getAttribute('data-prof') || '').toLowerCase();

      var ok = true;
      if (fm.length && mat.indexOf(fm) === -1 && note.indexOf(fm) === -1 && name.indexOf(fm) === -1) ok = false;
      if (fp.length && prof.indexOf(fp) === -1 && note.indexOf(fp) === -1 && name.indexOf(fp) === -1) ok = false;
      if (fn.length && name.indexOf(fn) === -1 && note.indexOf(fn) === -1) ok = false;
      if (fdate.length && ts.indexOf(fdate) === -1) ok = false;

      item.style.display = ok ? '' : 'none';
    }
    updateCounts();
  }

  function clearNotifFilters(){
    document.getElementById('nf_materia').value = '';
    document.getElementById('nf_prof').value = '';
    document.getElementById('nf_name').value = '';
    document.getElementById('nf_date').value = '';
    applyNotifFilters();
  }

  function showStatusMessage(message, type) {
    var msgEl = document.getElementById('status_message');
    if (!msgEl) return;
    msgEl.textContent = message;
    msgEl.style.display = 'inline-block';
    if (type === 'success') {
      msgEl.style.backgroundColor = '#10b981';
      msgEl.style.color = 'white';
    } else if (type === 'info') {
      msgEl.style.backgroundColor = '#06b6d4';
      msgEl.style.color = 'white';
    } else if (type === 'warning') {
      msgEl.style.backgroundColor = '#f59e0b';
      msgEl.style.color = 'white';
    }
    setTimeout(function() { msgEl.style.display = 'none'; }, 3000);
  }

  function getField(idx, prefix) {
    var el = document.getElementById(prefix + idx);
    return el ? (el.textContent || el.innerText || '') : '';
  }

  function markInline(idx, markAsRead, event){
    if (event && event.stopPropagation) event.stopPropagation();

    var idEl = document.getElementById('notif_id_' + idx);
    var uidEl = document.getElementById('notif_uid_' + idx);
    var tsEl = document.getElementById('notif_ts_' + idx);
    var nameEl = document.getElementById('notif_name_' + idx);
    var accEl = document.getElementById('notif_acc_' + idx);
    var encEl = document.getElementById('notif_note_enc_' + idx);
    if (!idEl || !tsEl || !encEl) return;

    var id = idEl.textContent || idEl.innerText || '';
    var uid = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
    var ts = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
    var name = nameEl ? (nameEl.textContent || nameEl.innerText || '') : '';
    var acc = accEl ? (accEl.textContent || accEl.innerText || '') : '';
    var note = '';
    try { note = atob(encEl.textContent || encEl.innerText || ''); } catch(e){ note = ''; }

    var action = markAsRead ? 'mark' : 'unmark';
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/notifications_mark', true);
    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.onload = function() {
      if (xhr.status === 200) {
        var item = document.querySelector('.notif-item[data-idx="' + idx + '"]');
        if (item && item.parentNode) item.parentNode.removeChild(item);
        updateCounts();

        if (markAsRead) showStatusMessage('Notificación marcada como leída', 'success');
        else showStatusMessage('Notificación movida a no leídas', 'info');
      } else {
        alert('Error al cambiar estado');
      }
    };

    xhr.send(
      'id=' + encodeURIComponent(id) +
      '&action=' + encodeURIComponent(action) +
      '&ts=' + encodeURIComponent(ts) +
      '&uid=' + encodeURIComponent(uid) +
      '&name=' + encodeURIComponent(name) +
      '&account=' + encodeURIComponent(acc) +
      '&note=' + encodeURIComponent(note)
    );
  }

  function openNotif(idx, e){
    if (e && e.stopPropagation) e.stopPropagation();

    currentIdx = idx;

    var idEl = document.getElementById('notif_id_' + idx);
    var uidEl = document.getElementById('notif_uid_' + idx);
    var tsEl = document.getElementById('notif_ts_' + idx);
    var encEl = document.getElementById('notif_note_enc_' + idx);
    var nameEl = document.getElementById('notif_name_' + idx);
    var accEl = document.getElementById('notif_acc_' + idx);
    var isReadEl = document.getElementById('notif_is_read_' + idx);
    var profileEl = document.getElementById('notif_profile_' + idx);

    if (!idEl || !tsEl || !encEl) return;

    var id = idEl.textContent || idEl.innerText || '';
    var uid = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
    var ts = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
    var note = '';
    try { note = atob(encEl.textContent || encEl.innerText || ''); } catch(e){ note = ''; }
    var name = nameEl ? (nameEl.textContent || nameEl.innerText || '') : '';
    var acc = accEl ? (accEl.textContent || accEl.innerText || '') : '';
    var isRead = isReadEl ? (isReadEl.textContent === '1') : false;
    var profileLink = profileEl ? (profileEl.textContent || profileEl.innerText || '') : '';

    currentNotificationData = { idx: idx, id: id, ts: ts, uid: uid, note: note, name: name, acc: acc, isRead: isRead, profileLink: profileLink };
    showModal();
  }

  function showModal() {
    if (!currentNotificationData) return;

    var noteDisplay = (currentNotificationData.note || '').replace(/teacher/gi, 'maestro');
    var tipo = 'Notificación';
    var ln = (currentNotificationData.note || '').toLowerCase();

    if (ln.indexOf('tarjeta') !== -1 && ln.indexOf('no registrada') !== -1) tipo = 'Tarjeta desconocida';
    else if (ln.indexOf('fuera de materia') !== -1 || ln.indexOf('no pertenece') !== -1) tipo = 'Alerta (Denegado)';
    else if (ln.indexOf('fuera de horario') !== -1 || ln.indexOf('no hay clase') !== -1) {
      if (ln.indexOf('maestro') !== -1 || ln.indexOf('teacher') !== -1) tipo = 'Informativa (Maestro)';
      else tipo = 'Informativa (Alumno)';
    }

    document.getElementById('modal_type').textContent = tipo;

    var meta = currentNotificationData.ts;
    if (currentNotificationData.name) meta += ' • ' + currentNotificationData.name;
    if (currentNotificationData.acc) meta += ' • ' + currentNotificationData.acc;
    document.getElementById('modal_meta').textContent = meta;

    document.getElementById('modal_body').textContent = noteDisplay;

    var big = '';
    big += '<b>ID:</b> ' + (currentNotificationData.id || '-') + '<br/>';
    big += '<b>UID:</b> ' + (currentNotificationData.uid || '-') + '<br/>';
    big += '<b>Hora:</b> ' + (currentNotificationData.ts || '-') + '<br/>';
    big += '<b>Tipo:</b> ' + tipo + '<br/>';
    if (currentNotificationData.name) big += '<b>Nombre:</b> ' + currentNotificationData.name + '<br/>';
    if (currentNotificationData.acc) big += '<b>Cuenta:</b> ' + currentNotificationData.acc + '<br/>';
    document.getElementById('modal_biginfo').innerHTML = big;

    var profileEl = document.getElementById('modal_profile_link');
    var historyEl = document.getElementById('modal_history_link');
    var markBtn = document.getElementById('modal_mark_btn');
    var unmarkBtn = document.getElementById('modal_unmark_btn');

    if (currentNotificationData.profileLink) {
      profileEl.style.display = 'inline-block';
      profileEl.href = currentNotificationData.profileLink;
      historyEl.style.display = 'inline-block';
      var hist = '/history?uid=' + encodeURIComponent(currentNotificationData.uid);
      if (currentNotificationData.ts && currentNotificationData.ts.length >= 10) {
        hist += '&date=' + encodeURIComponent(currentNotificationData.ts.substring(0, 10));
      }
      historyEl.href = hist;
    } else {
      profileEl.style.display = 'none';
      historyEl.style.display = 'none';
    }

    if (currentNotificationData.isRead) {
      markBtn.style.display = 'none';
      unmarkBtn.style.display = 'inline-block';
    } else {
      markBtn.style.display = 'inline-block';
      unmarkBtn.style.display = 'none';
    }

    document.getElementById('modal_back').style.display = 'flex';
    document.addEventListener('keydown', handleEscKey);
  }

  function handleEscKey(e) {
    if (e.key === 'Escape') closeModal();
  }

  function closeModal() {
    document.getElementById('modal_back').style.display = 'none';
    currentIdx = -1;
    currentNotificationData = null;
    document.removeEventListener('keydown', handleEscKey);
  }

  function markCurrentNotification() {
    if (!currentNotificationData) return;
    sendMarkRequest(true);
  }

  function unmarkCurrentNotification() {
    if (!currentNotificationData) return;
    sendMarkRequest(false);
  }

  function sendMarkRequest(markAsRead) {
    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/notifications_mark', true);
    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.onload = function() {
      if (xhr.status === 200) {
        var item = document.querySelector('.notif-item[data-idx="' + currentNotificationData.idx + '"]');
        if (item && item.parentNode) item.parentNode.removeChild(item);
        updateCounts();
        document.getElementById('modal_back').style.display = 'none';

        if (markAsRead) {
          if (window.location.pathname === '/notifications') showStatusMessage('Notificación marcada como leída', 'success');
          else showStatusMessage('Notificación actualizada', 'info');
        } else {
          if (window.location.pathname === '/notifications_read') showStatusMessage('Notificación movida a no leídas', 'info');
          else showStatusMessage('Notificación actualizada', 'info');
        }

        currentIdx = -1;
        currentNotificationData = null;
        document.removeEventListener('keydown', handleEscKey);
      } else {
        alert('Error al cambiar estado');
      }
    };

    xhr.send(
      'id=' + encodeURIComponent(currentNotificationData.id || '') +
      '&action=' + encodeURIComponent(markAsRead ? 'mark' : 'unmark') +
      '&ts=' + encodeURIComponent(currentNotificationData.ts || '') +
      '&uid=' + encodeURIComponent(currentNotificationData.uid || '') +
      '&name=' + encodeURIComponent(currentNotificationData.name || '') +
      '&account=' + encodeURIComponent(currentNotificationData.acc || '') +
      '&note=' + encodeURIComponent(currentNotificationData.note || '')
    );
  }

  function deleteCurrentNotification() {
    if (!currentNotificationData) return;
    if (!confirm('¿Eliminar esta notificación?')) return;

    var xhr = new XMLHttpRequest();
    xhr.open('POST', '/notifications_delete', true);
    xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
    xhr.onload = function() {
      if (xhr.status === 200) {
        var el = document.querySelector('.notif-item[data-idx="' + currentNotificationData.idx + '"]');
        if (el && el.parentNode) el.parentNode.removeChild(el);
        updateCounts();
        document.getElementById('modal_back').style.display = 'none';
        showStatusMessage('Notificación eliminada', 'info');
        currentIdx = -1;
        currentNotificationData = null;
        document.removeEventListener('keydown', handleEscKey);
      } else {
        alert('Error al eliminar notificación');
      }
    };

    xhr.send(
      'id=' + encodeURIComponent(currentNotificationData.id || '') +
      '&ts=' + encodeURIComponent(currentNotificationData.ts || '') +
      '&uid=' + encodeURIComponent(currentNotificationData.uid || '') +
      '&note=' + encodeURIComponent(currentNotificationData.note || '')
    );
  }

  function updateCounts() {
    var unreadVisible = 0;
    var readVisible = 0;
    var items = document.querySelectorAll('#notif_list .notif-item');

    for (var i = 0; i < items.length; i++) {
      var it = items[i];
      if (it.style.display === 'none') continue;
      var isRead = it.getAttribute('data-isread') === '1';
      if (isRead) readVisible++;
      else unreadVisible++;
    }

    var hUnread = document.querySelector('#unread_badge');
    if (hUnread) hUnread.textContent = String(unreadVisible);

    var hRead = document.querySelector('#read_badge');
    if (hRead) hRead.textContent = String(readVisible);

    var switchLink = document.querySelector('a.btn-blue[href="/notifications_read"]');
    if (switchLink && window.location.pathname === '/notifications') {
      switchLink.textContent = 'Ver Leídas (' + readVisible + ')';
    }

    var switchLink2 = document.querySelector('a.btn-blue[href="/notifications"]');
    if (switchLink2 && window.location.pathname === '/notifications_read') {
      switchLink2.textContent = 'Ver No Leídas (' + unreadVisible + ')';
    }
  }
</script>
)rawliteral";

  return html;
}

void handleNotificationsPage() {
  String html = htmlHeader("Notificaciones No Leídas");
  html += "<div class='card'>";
  html += generateNotificationsHTML(true);
  html += "</div>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleNotificationsReadPage() {
  String html = htmlHeader("Notificaciones Leídas");
  html += "<div class='card'>";
  html += generateNotificationsHTML(false);
  html += "</div>";
  html += htmlFooter();
  server.send(200, "text/html", html);
}

void handleNotificationsClearPOST() {
  if (!clearNotificationsOnServer()) {
    Serial.println("WARN: no se pudo borrar toda la lista de notificaciones en la BD");
  }
  server.sendHeader("Location", "/notifications");
  server.send(303, "text/plain", "Notificaciones borradas");
}

void handleNotificationsDeletePOST() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id <= 0) {
      server.send(400, "text/plain", "id inválido");
      return;
    }
    if (!deleteNotifOnServerById(id)) {
      server.send(500, "text/plain", "No se pudo borrar");
      return;
    }
    server.send(200, "text/plain", "deleted");
    return;
  }

  // Compatibilidad: borrar por tupla si no viene ID
  if (!server.hasArg("ts") || !server.hasArg("uid") || !server.hasArg("note")) {
    server.send(400, "text/plain", "faltan parametros");
    return;
  }

  String ts = server.arg("ts");
  String uid = server.arg("uid");
  String note = server.arg("note");

  std::vector<OracleNotifRec> matches;
  if (!findNotificationsByTuple(ts, uid, note, matches)) {
    server.send(500, "text/plain", "error leyendo notificaciones");
    return;
  }

  if (matches.empty()) {
    server.send(404, "text/plain", "notificación no encontrada");
    return;
  }

  bool ok = true;
  for (auto &r : matches) {
    if (!deleteNotifOnServerById(r.id)) ok = false;
  }

  if (!ok) {
    server.send(500, "text/plain", "error eliminando");
    return;
  }

  server.send(200, "text/plain", "deleted");
}

void handleNotificationsMarkPOST() {
  if (!server.hasArg("action")) {
    server.send(400, "application/json", "{\"error\":\"missing action\"}");
    return;
  }

  String action = server.arg("action");
  bool readState;

  if (action == "mark") readState = true;
  else if (action == "unmark") readState = false;
  else {
    server.send(400, "application/json", "{\"error\":\"unknown action\"}");
    return;
  }

  std::vector<OracleNotifRec> targets;

  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    std::vector<OracleNotifRec> all;
    if (!loadOracleNotifications(all)) {
      server.send(500, "application/json", "{\"error\":\"load failed\"}");
      return;
    }

    for (auto &r : all) {
      if (r.id == id) {
        targets.push_back(r);
        break;
      }
    }
  } else if (server.hasArg("ts") && server.hasArg("uid") && server.hasArg("note")) {
    if (!findNotificationsByTuple(server.arg("ts"), server.arg("uid"), server.arg("note"), targets)) {
      server.send(500, "application/json", "{\"error\":\"load failed\"}");
      return;
    }
  } else {
    server.send(400, "application/json", "{\"error\":\"missing params\"}");
    return;
  }

  if (targets.empty()) {
    server.send(404, "application/json", "{\"error\":\"not found\"}");
    return;
  }

  bool ok = true;
  for (auto &r : targets) {
    if (!updateNotifReadStateOnServer(r, readState)) ok = false;
  }

  if (!ok) {
    server.send(500, "application/json", "{\"error\":\"update failed\"}");
    return;
  }

  String j = "{\"status\":\"ok\",\"read\":";
  j += (readState ? "true" : "false");
  j += "}";
  server.send(200, "application/json", j);
}