// src/web/notifications.cpp
#include "notifications.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>
#include <algorithm>
#include <vector>
#include <stdint.h>

// Ruta interna para marcar leídos (archivo simple con claves)
static const char *NOTIF_READ_FILE_LOCAL = "/.notif_read";
static const char *NOTIF_READ_TMP_FILE = "/.notif_read.tmp";

// ---------------------------
// Utilities: hashing (stable)
// ---------------------------
static uint32_t fnv1a(const String &s) {
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < s.length(); i++) {
    hash ^= (uint8_t)s[i];
    hash *= 16777619UL;
  }
  return hash;
}

static String notifKey(const String &ts, const String &uid, const String &note) {
  String raw = ts + "|" + uid + "|" + note;
  uint32_t h = fnv1a(raw);
  String out = String(h, HEX);
  out.toLowerCase();
  return out;
}

// -------------------------------------------------
// Robust read/write helpers for NOTIF_READ_FILE_LOCAL
// -------------------------------------------------
static std::vector<String> readAllReadKeys() {
  std::vector<String> v;
  if (!SPIFFS.exists(NOTIF_READ_FILE_LOCAL)) return v;
  File f = SPIFFS.open(NOTIF_READ_FILE_LOCAL, FILE_READ);
  if (!f) return v;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length()) v.push_back(l);
  }
  f.close();
  return v;
}

static bool atomicReplaceFileWithLines(const char *destPath, const std::vector<String> &lines) {
  File tf = SPIFFS.open(NOTIF_READ_TMP_FILE, FILE_WRITE);
  if (!tf) return false;
  for (auto &ln : lines) tf.println(ln);
  tf.close();
  if (SPIFFS.exists(destPath)) SPIFFS.remove(destPath);
  bool ok = SPIFFS.rename(String(NOTIF_READ_TMP_FILE), String(destPath));
  if (!ok) {
    File f = SPIFFS.open(destPath, FILE_WRITE);
    if (!f) {
      if (SPIFFS.exists(NOTIF_READ_TMP_FILE)) SPIFFS.remove(NOTIF_READ_TMP_FILE);
      return false;
    }
    for (auto &ln : lines) f.println(ln);
    f.close();
    if (SPIFFS.exists(NOTIF_READ_TMP_FILE)) SPIFFS.remove(NOTIF_READ_TMP_FILE);
    return true;
  }
  return true;
}

static bool saveReadKeys(const std::vector<String> &v) { return atomicReplaceFileWithLines(NOTIF_READ_FILE_LOCAL, v); }

static void markNotifReadLocal(const String &key) {
  auto v = readAllReadKeys();
  for (auto &k : v) if (k == key) return;
  v.push_back(key);
  saveReadKeys(v);
}

static void unmarkNotifReadLocal(const String &key) {
  auto v = readAllReadKeys();
  std::vector<String> out; out.reserve(v.size());
  for (auto &k : v) if (k != key) out.push_back(k);
  saveReadKeys(out);
}

// Cleanup: eliminar claves huérfanas
static void cleanupReadKeys() {
  std::vector<String> valid;
  auto nots = readNotifications(500);
  for (auto &l : nots) {
    auto c = parseQuotedCSVLine(l);
    if (c.size() < 5) continue;
    String ts = c[0];
    String uid = c[1];
    String note = c[4];
    valid.push_back(notifKey(ts, uid, note));
  }
  auto old = readAllReadKeys();
  std::vector<String> out; out.reserve(old.size());
  for (auto &k : old) {
    for (auto &v : valid) {
      if (k == v) { out.push_back(k); break; }
    }
  }
  saveReadKeys(out);
}

// Escapa texto para HTML
static String htmlEscapeLocal(const String &s) {
  String r; r.reserve(s.length());
  for (size_t i = 0; i < (size_t)s.length(); ++i) {
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

// Base64 encode (para almacenar nota "segura" en el HTML y recuperarla con atob() en JS)
static String base64Encode(const String &in) {
  static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  String out;
  size_t len = in.length();
  const unsigned char *bytes = (const unsigned char*)in.c_str();
  int val = 0, valb = -6;
  for (size_t i = 0; i < len; ++i) {
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

// Detecta el tipo de notificación según el texto (más heurística)
static String detectNotificationType(const String &note) {
  String n = note; n.toLowerCase();
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

// Heurística ligera para extraer materia/profesor desde la nota (si existe)
static String extractFieldFromNote(const String &note, const String &keysCSV) {
  String tmp = keysCSV;
  std::vector<String> keys;
  while (tmp.length()) {
    int c = tmp.indexOf(',');
    if (c < 0) { String k = tmp; k.trim(); if (k.length()) keys.push_back(k); break; }
    String k = tmp.substring(0, c); k.trim(); if (k.length()) keys.push_back(k);
    tmp = tmp.substring(c+1);
  }
  String low = note; low.toLowerCase();
  for (auto &k : keys) {
    String lk = k; lk.toLowerCase();
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

// Función común para generar el HTML de notificaciones
// Mejoras de performance: carga las claves leídas UNA sola vez; almacena la nota en base64 para no romper la clave al enviarla.
static String generateNotificationsHTML(bool showUnreadOnly, const String &pageTitle) {
  String html = htmlHeader(pageTitle.c_str());

  // Leer notificaciones (una sola vez)
  auto nots = readNotifications(500);
  if (nots.size() > 1) std::reverse(nots.begin(), nots.end());

  // Leer las claves leídas UNA vez (evita múltiples accesos a SPIFFS)
  auto readKeysVec = readAllReadKeys();

  size_t unreadCount = 0;
  size_t readCount = 0;

  // Lista filtrada que vamos a mostrar
  std::vector<String> filteredNotifications;
  std::vector<bool> isReadStatus;
  std::vector<String> keys;
  std::vector<String> rawNotes; // para base64

  for (size_t i = 0; i < nots.size(); ++i) {
    String ln = nots[i];
    auto c = parseQuotedCSVLine(ln);
    String ts = (c.size() > 0 ? c[0] : "");
    String uid = (c.size() > 1 ? c[1] : "");
    String note = (c.size() > 4 ? c[4] : "");
    String key = notifKey(ts, uid, note);

    // buscar en readKeysVec (vector en memoria, mucho más rápido que abrir archivo cada vez)
    bool isRead = (std::find(readKeysVec.begin(), readKeysVec.end(), key) != readKeysVec.end());

    if (isRead) readCount++; else unreadCount++;

    if ((showUnreadOnly && !isRead) || (!showUnreadOnly && isRead)) {
      filteredNotifications.push_back(ln);
      isReadStatus.push_back(isRead);
      keys.push_back(key);
      rawNotes.push_back(note);
    }
  }

  // Header + controles
  if (showUnreadOnly) {
    html += "<div class='card'><h2 style='display:flex;align-items:center;gap:12px;'>Notificaciones No Leídas <span id='unread_badge' style='background:#ef4444;color:#fff;padding:6px 10px;border-radius:999px;font-weight:800;font-size:0.95em;'>" + String(unreadCount) + "</span></h2>";
  } else {
    html += "<div class='card'><h2 style='display:flex;align-items:center;gap:12px;'>Notificaciones Leídas</h2>";
  }

  html += "<div style='margin-bottom:12px;display:flex;gap:8px;align-items:center;flex-wrap:wrap;'>";
  html += "<form method='POST' action='/notifications_clear' onsubmit='return confirm(\"Borrar todas las notificaciones? Esta acción es irreversible.\");' style='display:inline'>";
  html += "<input class='btn btn-red' type='submit' value='🗑️ Borrar Todas'></form>";
  html += "<a class='btn btn-blue' href='/'>Inicio</a>";
  if (showUnreadOnly) html += "<a class='btn btn-switch' href='/notifications_read'>Ver Leídas (" + String(readCount) + ")</a>";
  else html += "<a class='btn btn-switch active' href='/notifications'>Ver No Leídas (" + String(unreadCount) + ")</a>";
  html += "</div>";

  // filtros + estilos (igual que antes)
  html += "<div class='filters' style='margin-bottom:10px;display:flex;gap:8px;flex-wrap:wrap;align-items:center;'>";
  html += "<input id='nf_materia' placeholder='Filtrar por materia' style='min-width:160px'>";
  html += "<input id='nf_prof' placeholder='Filtrar por profesor' style='min-width:160px'>";
  html += "<input id='nf_name' placeholder='Filtrar por nombre (alumno/maestro)' style='min-width:160px'>";
  html += "<input id='nf_date' type='date' placeholder='Filtrar por fecha' style='min-width:140px'>";
  html += "<button class='search-btn btn btn-blue' onclick='applyNotifFilters()'>Aplicar</button>";
  html += "<button class='search-btn btn btn-green' onclick='clearNotifFilters()'>Limpiar</button>";
  html += "</div>";

  html += R"rawliteral(
    <style>
      .notif-list { display:flex; flex-direction:column; gap:12px; }
      .notif-item { padding:12px; border-radius:10px; box-shadow:0 6px 18px rgba(2,6,23,0.06); transition:transform .12s ease; position:relative; overflow:hidden; display:flex; flex-direction:column; cursor:pointer; border: 1px solid #e2e8f0; }
      .notif-item:hover { transform:translateY(-4px); box-shadow: 0 8px 25px rgba(2,6,23,0.1); }
      .notif-header { display:flex; justify-content:space-between; align-items:flex-start; gap:8px; }
      .notif-type { font-weight:800; padding:6px 10px; border-radius:8px; font-size:0.95em; display:inline-block; }
      .notif-meta { font-size:0.86em; color:#374151; margin-top:8px; margin-bottom:8px; }
      .notif-note { font-size:1em; color:#0f172a; white-space:pre-wrap; margin-top:6px;}
      .compact-actions { display:flex; gap:8px; align-items:center; }
      .badge-new { background:#ef4444; color:white; padding:6px 10px; border-radius:8px; font-weight:800; font-size:0.95em; display:inline-block; }
      .btn-mark-inline { font-size:0.9em; padding:6px 10px; }
      .modal-backdrop { position:fixed; left:0; right:0; top:0; bottom:0; background:rgba(2,6,23,0.35); display:none; align-items:center; justify-content:center; z-index:1200; }
      .modal-card { background:#fff; border-radius:12px; width:min(600px,92%); max-width:600px; padding:20px; box-shadow:0 20px 60px rgba(2,6,23,0.35); transform:translateY(0); transition:all .12s ease; max-height: 85vh; overflow-y: auto; }
      .modal-header { display:flex; justify-content:space-between; align-items:center; gap:12px; margin-bottom: 15px; }
      .modal-title { font-weight:900; font-size:1.2em; color:#0f172a; }
      .modal-body { margin-top:10px; color:#0f172a; font-size:1em; max-height:320px; overflow:auto; white-space: pre-wrap; line-height: 1.5; }
      .modal-meta-big { margin-top:15px; font-size:0.95em; color:#475569; background: #f8fafc; padding: 12px; border-radius: 8px; }
      .modal-actions { display:flex; gap:8px; justify-content:flex-end; margin-top:20px; flex-wrap: wrap; }
      .btn-switch { background:transparent; border:1px solid #e5e7eb; color:#0f172a; padding:8px 12px; border-radius:8px; cursor:pointer; font-weight:600; transition: all 0.2s ease; text-decoration: none; display: inline-block; }
      .btn-switch:hover { background:#f3f4f6; }
      .btn-switch.active { background:#1d4ed8; color:white; border-color:transparent; box-shadow:0 6px 18px rgba(29,78,216,0.12); }
      .btn { border:none; border-radius:8px; padding:8px 12px; cursor:pointer; font-weight:700; transition: all 0.2s ease; text-decoration: none; display: inline-block; text-align: center; }
      .btn:hover { opacity: 0.9; transform: translateY(-1px); }
      .btn-green { background:#10b981; color:#fff; }
      .btn-orange { background:#f59e0b; color:#fff; }
      .btn-red { background:#ef4444; color:#fff; }
      .btn-blue { background:#06b6d4; color:#05345b; }
      .btn-purple { background:#8b5cf6; color:#fff; }
      @media (max-width:900px) { .compact-actions { flex-direction:row; } .modal-actions { flex-direction: column; } .modal-card { padding: 15px; } }
    </style>
  )rawliteral";

  // Lista de notificaciones
  html += "<div id='notif_list' class='notif-list'>";
  if (filteredNotifications.empty()) {
    html += (showUnreadOnly ? "<p>No hay notificaciones no leídas.</p>" : "<p>No hay notificaciones leídas.</p>");
  } else {
    for (size_t i = 0; i < filteredNotifications.size(); ++i) {
      String ln = filteredNotifications[i];
      bool isRead = isReadStatus[i];
      String key = keys[i];
      String rawNote = rawNotes[i];
      auto c = parseQuotedCSVLine(ln);
      String ts = (c.size()>0?c[0]:"");
      String uid = (c.size()>1?c[1]:"");
      String name = (c.size()>2?c[2]:"");
      String acc = (c.size()>3?c[3]:"");
      String note = rawNote;
      String tipo = detectNotificationType(note);
      String materiaFromNote = extractFieldFromNote(note, "Materia,Materia:,materia");
      String profFromNote = extractFieldFromNote(note, "Profesor,Profesor:,Maestro,Maestro:,Teacher,Teacher:");

      String bg="#fff", badgeBg="#E2E8F0", badgeColor="#0f172a";
      if (isRead) {
        bg = "#FAFAFA"; badgeBg = "#E2E8F0"; badgeColor = "#0f172a";
        if (tipo.indexOf("Alerta")==0 || tipo=="Tarjeta desconocida") bg="#fff7f7";
        else if (tipo.indexOf("Informativa (Alumno)") == 0) bg="#fffeef";
        else if (tipo.indexOf("Informativa (Maestro)") == 0) bg="#f6fff6";
      } else {
        if (tipo.indexOf("Alerta")==0 || tipo=="Tarjeta desconocida") { bg="#fff4f4"; badgeBg="#ffcccc"; badgeColor="#7a1f1f"; }
        else if (tipo.indexOf("Informativa (Alumno)") == 0) { bg="#fffef0"; badgeBg="#fff3bf"; badgeColor="#664d03"; }
        else if (tipo.indexOf("Informativa (Maestro)") == 0) { bg="#f0fff0"; badgeBg="#c7f9d6"; badgeColor="#065f46"; }
      }

      String noteEsc = htmlEscapeLocal(note);
      String nameEsc = htmlEscapeLocal(name);
      String accEsc = htmlEscapeLocal(acc);
      String tsEsc = htmlEscapeLocal(ts);
      String uidEsc = htmlEscapeLocal(uid);
      String tipoEsc = htmlEscapeLocal(tipo);
      String keyEsc = htmlEscapeLocal(key);
      String noteB64 = base64Encode(note);

      // data-idx corresponde al índice en la lista filtrada
      html += "<div class='notif-item' style='background:" + bg + ";' data-idx='" + String(i) + "' data-ts='" + tsEsc + "' data-uid='" + uidEsc + "' data-name='" + nameEsc + "' data-note-html='" + noteEsc + "' data-type='" + tipoEsc + "' data-key='" + keyEsc + "' data-isread='" + (isRead ? "1" : "0") + "'>";
      html += "<div class='notif-header'>";
      html += "<div style='display:flex;flex-direction:column;gap:6px;'><span class='notif-type' style='background:" + badgeBg + ";color:" + badgeColor + ";'>" + tipo + "</span></div>";
      html += "<div style='display:flex;align-items:center;gap:8px;'>";
      if (!isRead) html += "<div class='action-left'><div class='badge-new'>Nuevo</div></div>";
      html += "<div class='compact-actions'>";
      if (isRead) html += "<button type='button' class='btn btn-orange btn-mark-inline' onclick='markInline(" + String(i) + ", false, event)'>Marcar no leído</button>";
      else html += "<button type='button' class='btn btn-green btn-mark-inline' onclick='markInline(" + String(i) + ", true, event)'>Marcar leído</button>";
      html += "</div></div></div>";

      String meta = tsEsc;
      if (nameEsc.length()) meta += " • " + nameEsc;
      if (accEsc.length()) meta += " • " + accEsc;
      if (materiaFromNote.length()) meta += " • " + htmlEscapeLocal(materiaFromNote);
      html += "<div class='notif-meta'>" + meta + "</div>";
      html += "<div class='notif-note'>" + (noteEsc.length() ? noteEsc : "<i>(sin detalles)</i>") + "</div>";

      // Datos ocultos: uid, ts, name, acc, key, isread, y nota codificada base64 (para enviarla intacta)
      html += "<div id='notif_uid_" + String(i) + "' style='display:none'>" + uidEsc + "</div>";
      html += "<div id='notif_ts_" + String(i) + "' style='display:none'>" + tsEsc + "</div>";
      html += "<div id='notif_name_" + String(i) + "' style='display:none'>" + nameEsc + "</div>";
      html += "<div id='notif_acc_" + String(i) + "' style='display:none'>" + accEsc + "</div>";
      html += "<div id='notif_key_" + String(i) + "' style='display:none'>" + keyEsc + "</div>";
      html += "<div id='notif_is_read_" + String(i) + "' style='display:none'>" + (isRead ? "1" : "0") + "</div>";
      html += "<div id='notif_note_enc_" + String(i) + "' style='display:none'>" + noteB64 + "</div>";

      html += "</div>";
    }
  }
  html += "</div>"; // end list

  // Modal
  html += R"rawliteral(
    <div id="modal_back" class="modal-backdrop" role="dialog" aria-modal="true" style="display:none">
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

  // JavaScript: delegación de eventos + atob() para nota original (base64)
  html += R"rawliteral(
    <script>
      var currentIdx = -1;
      var currentNotificationData = null;

      // Delegación de eventos: un listener para la lista (esto evita hacer un listener por item)
      document.addEventListener('DOMContentLoaded', function() {
        var list = document.getElementById('notif_list');
        if (list) {
          list.addEventListener('click', function(e) {
            var btn = e.target.closest('button');
            if (btn) {
              // botón dentro de una notificación: determinar su acción (marcar o desmarcar)
              var item = e.target.closest('.notif-item');
              if (!item) return;
              var idx = item.getAttribute('data-idx');
              if (!idx) return;
              if (btn.classList.contains('btn-mark-inline')) {
                var markAsRead = btn.classList.contains('btn-green');
                markInline(parseInt(idx), markAsRead, e);
              }
              return;
            }
            var item = e.target.closest('.notif-item');
            if (!item) return;
            var idx = item.getAttribute('data-idx');
            if (!idx) return;
            openNotif(parseInt(idx), e);
          });
        }
      });

      function applyNotifFilters(){
        var fm=document.getElementById('nf_materia').value.trim().toLowerCase();
        var fp=document.getElementById('nf_prof').value.trim().toLowerCase();
        var fn=document.getElementById('nf_name').value.trim().toLowerCase();
        var fdate=document.getElementById('nf_date').value.trim();
        var items = document.querySelectorAll('#notif_list .notif-item');
        for (var i=0;i<items.length;i++){
          var item = items[i];
          var ts = (item.getAttribute('data-ts')||'').toLowerCase();
          var name = (item.getAttribute('data-name')||'').toLowerCase();
          var note = (item.getAttribute('data-note-html')||'').toLowerCase();
          var mat = (item.getAttribute('data-materia')||'').toLowerCase();
          var prof = (item.getAttribute('data-prof')||'').toLowerCase();
          var ok = true;
          if (fm.length && mat.indexOf(fm) === -1 && name.indexOf(fm) === -1 && note.indexOf(fm) === -1) ok = false;
          if (fp.length && prof.indexOf(fp) === -1 && name.indexOf(fp) === -1 && note.indexOf(fp) === -1) ok = false;
          if (fn.length && name.indexOf(fn) === -1 && note.indexOf(fn) === -1) ok = false;
          if (fdate.length && ts.indexOf(fdate) === -1) ok = false;
          item.style.display = ok ? '' : 'none';
        }
        updateCounts();
      }

      function clearNotifFilters(){
        document.getElementById('nf_materia').value='';
        document.getElementById('nf_prof').value='';
        document.getElementById('nf_name').value='';
        document.getElementById('nf_date').value='';
        applyNotifFilters();
      }

      // Nota: la nota original se recupera con atob(document.getElementById('notif_note_enc_' + idx).textContent)
      function markInline(idx, markAsRead, event){
        if (event && event.stopPropagation) event.stopPropagation();
        var uidEl  = document.getElementById('notif_uid_' + idx);
        var tsEl   = document.getElementById('notif_ts_' + idx);
        var encEl  = document.getElementById('notif_note_enc_' + idx);
        if (!tsEl || !encEl) return;
        var uid  = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
        var ts   = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
        var note = '';
        try { note = atob(encEl.textContent || encEl.innerText || ''); } catch(e){ note = ''; }
        var action = markAsRead ? 'mark' : 'unmark';
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/notifications_mark', true);
        xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        xhr.onload = function() {
          if (xhr.status === 200) {
            // Si estamos en /notifications (no leídas) y marcamos como leído, quitar del DOM y redirigir a /notifications_read
            var item = document.querySelector('.notif-item[data-idx="' + idx + '"]');
            if (item) {
              if (window.location.pathname === '/notifications' && markAsRead) {
                if (item.parentNode) item.parentNode.removeChild(item);
                window.location.href = '/notifications_read';
                return;
              } else if (window.location.pathname === '/notifications_read' && !markAsRead) {
                if (item.parentNode) item.parentNode.removeChild(item);
                window.location.href = '/notifications';
                return;
              } else {
                // actualizar UI localmente
                item.setAttribute('data-isread', markAsRead ? '1' : '0');
                var badge = item.querySelector('.badge-new');
                if (badge) badge.style.display = markAsRead ? 'none' : '';
                var btn = item.querySelector('.btn-mark-inline');
                if (btn) {
                  if (markAsRead) { btn.textContent = 'Marcar no leído'; btn.className = 'btn btn-orange btn-mark-inline'; }
                  else { btn.textContent = 'Marcar leído'; btn.className = 'btn btn-green btn-mark-inline'; }
                }
              }
            }
            updateCounts();
          } else {
            alert('Error al cambiar estado');
          }
        };
        xhr.send('action=' + action + '&ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note));
      }

      function openNotif(idx, e){
        if (e && e.stopPropagation) e.stopPropagation();
        currentIdx = idx;
        var uidEl  = document.getElementById('notif_uid_' + idx);
        var tsEl   = document.getElementById('notif_ts_' + idx);
        var encEl  = document.getElementById('notif_note_enc_' + idx);
        var nameEl = document.getElementById('notif_name_' + idx);
        var accEl  = document.getElementById('notif_acc_' + idx);
        var isReadEl = document.getElementById('notif_is_read_' + idx);
        if (!tsEl || !encEl) return;
        var uid  = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
        var ts   = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
        var note = '';
        try { note = atob(encEl.textContent || encEl.innerText || ''); } catch(e){ note = ''; }
        var name = nameEl ? (nameEl.textContent || nameEl.innerText || '') : '';
        var acc  = accEl ? (accEl.textContent || accEl.innerText || '') : '';
        var isRead = isReadEl ? (isReadEl.textContent === '1') : false;
        currentNotificationData = { idx: idx, ts: ts, uid: uid, note: note, name: name, acc: acc, isRead: isRead };

        // Si no está leída, marcar automáticamente (y si estamos en vista /notifications, redirigir a /notifications_read para mostrar el cambio)
        if (!isRead) {
          var xhr = new XMLHttpRequest();
          xhr.open('POST', '/notifications_mark', true);
          xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
          xhr.onload = function() {
            // Si OK, eliminar del DOM y redirigir para que la vista muestre la notificación entre leídas
            if (xhr.status === 200) {
              var item = document.querySelector('.notif-item[data-idx="' + idx + '"]');
              if (item) {
                if (item.parentNode) item.parentNode.removeChild(item);
                // redirigir para ver la lista de leídas inmediatamente
                window.location.href = '/notifications_read';
                return;
              }
            }
            // si falla o no hizo redirect, mostramos modal igualmente
            showModal();
          };
          xhr.send('action=mark&ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note));
        } else {
          showModal();
        }
      }

      function showModal() {
        if (!currentNotificationData) return;
        var noteDisplay = (currentNotificationData.note||'').replace(/teacher/gi,'maestro');
        var tipo = 'Notificación';
        var ln = (currentNotificationData.note||'').toLowerCase();
        if (ln.indexOf('tarjeta')!==-1 && ln.indexOf('no registrada')!==-1) tipo = 'Tarjeta desconocida';
        else if (ln.indexOf('fuera de materia')!==-1 || ln.indexOf('no pertenece')!==-1) tipo = 'Alerta (Denegado)';
        else if (ln.indexOf('fuera de horario')!==-1 || ln.indexOf('no hay clase')!==-1) {
          if (ln.indexOf('maestro')!==-1 || ln.indexOf('teacher')!==-1) tipo = 'Informativa (Maestro)'; else tipo = 'Informativa (Alumno)';
        }
        document.getElementById('modal_type').textContent = tipo;
        var itemEl = document.querySelector('.notif-item[data-idx="' + currentNotificationData.idx + '"]');
        var prof = (itemEl && itemEl.getAttribute('data-prof')) ? itemEl.getAttribute('data-prof') : '';
        var meta = currentNotificationData.ts;
        if (prof) meta += ' • ' + prof; else if (currentNotificationData.name) meta += ' • ' + currentNotificationData.name;
        if (currentNotificationData.acc) meta += ' • ' + currentNotificationData.acc;
        document.getElementById('modal_meta').textContent = meta;
        document.getElementById('modal_body').textContent = noteDisplay;
        var big = '';
        big += '<b>UID:</b> ' + (currentNotificationData.uid?currentNotificationData.uid:'-') + '<br/>';
        big += '<b>Hora:</b> ' + (currentNotificationData.ts?currentNotificationData.ts:'-') + '<br/>';
        big += '<b>Tipo:</b> ' + tipo + '<br/>';
        if (prof) big += '<b>Maestro:</b> ' + prof + '<br/>';
        else if (currentNotificationData.name) big += '<b>Nombre:</b> ' + currentNotificationData.name + '<br/>';
        if (currentNotificationData.acc) big += '<b>Cuenta:</b> ' + currentNotificationData.acc + '<br/>';
        document.getElementById('modal_biginfo').innerHTML = big;

        var profileEl = document.getElementById('modal_profile_link');
        var historyEl = document.getElementById('modal_history_link');
        var markBtn = document.getElementById('modal_mark_btn');
        var unmarkBtn = document.getElementById('modal_unmark_btn');

        if (tipo === 'Tarjeta desconocida') {
          profileEl.style.display = 'none';
          historyEl.style.display = 'none';
        } else {
          profileEl.style.display = 'inline-block';
          historyEl.style.display = 'inline-block';
          var profile_base = '/capture_edit?uid=' + encodeURIComponent(currentNotificationData.uid);
          if (prof) profile_base = '/teachers_all?search_uid=' + encodeURIComponent(currentNotificationData.uid);
          else if ((currentNotificationData.note||'').toLowerCase().indexOf('maestro')!==-1 || (currentNotificationData.note||'').toLowerCase().indexOf('teacher')!==-1) profile_base = '/teachers_all?search_uid=' + encodeURIComponent(currentNotificationData.uid);
          else profile_base = '/students_all?search_uid=' + encodeURIComponent(currentNotificationData.uid);
          profileEl.href = profile_base;
          var hist = '/history';
          var params=[];
          if (currentNotificationData.uid) params.push('uid=' + encodeURIComponent(currentNotificationData.uid));
          if (currentNotificationData.ts && currentNotificationData.ts.length>=10) params.push('date=' + encodeURIComponent(currentNotificationData.ts.substring(0,10)));
          if (params.length) hist += '?' + params.join('&');
          historyEl.href = hist;
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

      function handleEscKey(e) { if (e.key === 'Escape') closeModal(); }

      function closeModal() {
        document.getElementById('modal_back').style.display='none';
        currentIdx = -1;
        currentNotificationData = null;
        document.removeEventListener('keydown', handleEscKey);
      }

      function markCurrentNotification(){
        if (!currentNotificationData) return;
        var idx = currentNotificationData.idx;
        var encEl = document.getElementById('notif_note_enc_' + idx);
        var note = '';
        try { note = atob(encEl.textContent || encEl.innerText || ''); } catch(e){ note = currentNotificationData.note; }
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/notifications_mark', true);
        xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        xhr.onload = function() {
          if (xhr.status === 200) {
            // si estábamos en /notifications (no leídas) redirigir para ver leídas
            if (window.location.pathname === '/notifications') {
              closeModal();
              window.location.href = '/notifications_read';
              return;
            }
            // si estamos en /notifications_read, simplemente actualizar UI
            var el = document.querySelector('.notif-item[data-idx="' + idx + '"]');
            if (el) { el.setAttribute('data-isread','1'); var b = el.querySelector('.badge-new'); if (b) b.style.display='none'; }
            updateCounts();
            closeModal();
          } else alert('Error al marcar como leído');
        };
        xhr.send('action=mark&ts=' + encodeURIComponent(currentNotificationData.ts) + '&uid=' + encodeURIComponent(currentNotificationData.uid) + '&note=' + encodeURIComponent(note));
      }

      function unmarkCurrentNotification(){
        if (!currentNotificationData) return;
        var idx = currentNotificationData.idx;
        var encEl = document.getElementById('notif_note_enc_' + idx);
        var note = '';
        try { note = atob(encEl.textContent || encEl.innerText || ''); } catch(e){ note = currentNotificationData.note; }
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/notifications_mark', true);
        xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        xhr.onload = function() {
          if (xhr.status === 200) {
            if (window.location.pathname === '/notifications_read') {
              closeModal();
              window.location.href = '/notifications';
              return;
            }
            var el = document.querySelector('.notif-item[data-idx="' + idx + '"]');
            if (el) { el.setAttribute('data-isread','0'); var b = el.querySelector('.badge-new'); if (b) b.style.display=''; }
            updateCounts();
            closeModal();
          } else alert('Error al marcar como no leído');
        };
        xhr.send('action=unmark&ts=' + encodeURIComponent(currentNotificationData.ts) + '&uid=' + encodeURIComponent(currentNotificationData.uid) + '&note=' + encodeURIComponent(note));
      }

      function deleteCurrentNotification(){
        if (!currentNotificationData) return;
        if (!confirm('¿Eliminar esta notificación?')) return;
        var idx = currentNotificationData.idx;
        var encEl = document.getElementById('notif_note_enc_' + idx);
        var note = '';
        try { note = atob(encEl.textContent || encEl.innerText || ''); } catch(e){ note = currentNotificationData.note; }
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/notifications_delete', true);
        xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        xhr.onload = function() {
          if (xhr.status === 200) {
            var el = document.querySelector('.notif-item[data-idx="' + idx + '"]');
            if (el && el.parentNode) el.parentNode.removeChild(el);
            updateCounts();
            closeModal();
          } else alert('Error al eliminar notificación');
        };
        xhr.send('ts=' + encodeURIComponent(currentNotificationData.ts) + '&uid=' + encodeURIComponent(currentNotificationData.uid) + '&note=' + encodeURIComponent(note));
      }

      // cerrar modal al hacer click fuera
      (function() {
        var mb = document.getElementById('modal_back');
        if (mb) mb.addEventListener('click', function(e) { if (e.target.id === 'modal_back') closeModal(); });
      })();

      function updateCounts(){
        var unreadVisible = 0;
        var readVisible = 0;
        var items = document.querySelectorAll('#notif_list .notif-item');
        for (var i=0;i<items.length;i++){
          var it = items[i];
          if (it.style.display === 'none') continue;
          var isRead = it.getAttribute('data-isread') === '1';
          if (isRead) readVisible++; else unreadVisible++;
        }
        var hUnread = document.querySelector('#unread_badge');
        if (hUnread) hUnread.textContent = String(unreadVisible);
      }

    </script>
  )rawliteral";

  html += "</div>" + htmlFooter();
  return html;
}

// Rutas: /notifications (no leídas) y /notifications_read (leídas)
void handleNotificationsPage() {
  String html = generateNotificationsHTML(true, "Notificaciones No Leídas");
  server.send(200, "text/html", html);
}

void handleNotificationsReadPage() {
  String html = generateNotificationsHTML(false, "Notificaciones Leídas");
  server.send(200, "text/html", html);
}

// POST /notifications_clear -> borrar archivo
void handleNotificationsClearPOST() {
  clearNotifications();
  if (SPIFFS.exists(NOTIF_READ_FILE_LOCAL)) SPIFFS.remove(NOTIF_READ_FILE_LOCAL);
  server.sendHeader("Location", "/notifications");
  server.send(303, "text/plain", "Notificaciones borradas");
}

// POST /notifications_delete -> borrar una notificación específica (by ts+uid+note)
void handleNotificationsDeletePOST() {
  if (!server.hasArg("ts") || !server.hasArg("uid") || !server.hasArg("note")) {
    server.send(400, "text/plain", "faltan parametros");
    return;
  }
  String ts = server.arg("ts");
  String uid = server.arg("uid");
  String note = server.arg("note");

  if (!SPIFFS.exists(NOTIF_FILE)) {
    server.send(404, "text/plain", "no notifs");
    return;
  }

  File f = SPIFFS.open(NOTIF_FILE, FILE_READ);
  std::vector<String> lines;
  if (!f) { server.send(500, "text/plain", "no file"); return; }
  bool firstLineHandled = false;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if (!firstLineHandled) { firstLineHandled = true; lines.push_back(l); continue; }
    l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    String lts = (c.size()>0?c[0]:"");
    String luid = (c.size()>1?c[1]:"");
    String lnote = (c.size()>4?c[4]:"");
    if (lts == ts && luid == uid && lnote == note) continue;
    lines.push_back(l);
  }
  f.close();

  writeAllLines(NOTIF_FILE, lines);
  cleanupReadKeys();
  server.send(200, "text/plain", "deleted");
}

// POST /notifications_mark -> action=mark|unmark
void handleNotificationsMarkPOST() {
  if (!server.hasArg("action") || !server.hasArg("ts") || !server.hasArg("uid") || !server.hasArg("note")) {
    server.send(400, "application/json", "{\"error\":\"missing\"}");
    return;
  }
  String action = server.arg("action");
  String ts = server.arg("ts");
  String uid = server.arg("uid");
  String note = server.arg("note");
  String key = notifKey(ts, uid, note);

  bool nowRead = false;
  if (action == "mark") { 
    markNotifReadLocal(key); 
    nowRead = true; 
  } else if (action == "unmark") { 
    unmarkNotifReadLocal(key); 
    nowRead = false; 
  } else {
    server.send(400, "application/json", "{\"error\":\"unknown action\"}");
    return;
  }

  String j = "{\"status\":\"ok\",\"read\":";
  j += (nowRead ? "true" : "false");
  j += "}";
  server.send(200, "application/json", j);
}
