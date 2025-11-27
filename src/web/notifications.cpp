// src/web/notifications.cpp
#include "notifications.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>
#include <algorithm>

// Ruta interna para marcar leídos (archivo simple con claves)
static const char *NOTIF_READ_FILE_LOCAL = "/.notif_read";

// Construye clave única para una notificación (ts + uid + principio de nota)
static String notifKey(const String &ts, const String &uid, const String &note) {
  String k = ts + "|" + uid;
  // añadir fragmento de nota para evitar colisiones
  String frag = note;
  if (frag.length() > 80) frag = frag.substring(0, 80);
  // eliminar saltos
  frag.replace("\n", " ");
  frag.replace("\r", " ");
  k += "|" + frag;
  return k;
}

// Guarda marca de leído (append si no existe)
static void markNotifReadLocal(const String &key) {
  if (!SPIFFS.exists(NOTIF_READ_FILE_LOCAL)) {
    File f = SPIFFS.open(NOTIF_READ_FILE_LOCAL, FILE_WRITE);
    if (!f) return;
    f.close();
  }
  // comprobar si ya existe
  File f = SPIFFS.open(NOTIF_READ_FILE_LOCAL, FILE_READ);
  if (f) {
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (l == key) { f.close(); return; }
    }
    f.close();
  }
  appendLineToFile(NOTIF_READ_FILE_LOCAL, key);
}

// Quitar marca
static void unmarkNotifReadLocal(const String &key) {
  if (!SPIFFS.exists(NOTIF_READ_FILE_LOCAL)) return;
  File f = SPIFFS.open(NOTIF_READ_FILE_LOCAL, FILE_READ);
  if (!f) return;
  std::vector<String> lines;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length() && l != key) lines.push_back(l);
  }
  f.close();
  writeAllLines(NOTIF_READ_FILE_LOCAL, lines);
}

// Comprueba si está marcado leído
static bool isNotifReadLocal(const String &key) {
  if (!SPIFFS.exists(NOTIF_READ_FILE_LOCAL)) return false;
  File f = SPIFFS.open(NOTIF_READ_FILE_LOCAL, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l == key) { f.close(); return true; }
  }
  f.close();
  return false;
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

// Detecta el tipo de notificación según el texto (más heurística)
static String detectNotificationType(const String &note) {
  String n = note;
  n.toLowerCase();

  if (n.indexOf("tarjeta") >= 0 && (n.indexOf("no registrada") >= 0 || n.indexOf("no registrado") >= 0 || n.indexOf("desconocida") >= 0)) {
    return String("Tarjeta desconocida");
  }

  if (n.indexOf("intento fuera de materia") >= 0 ||
      n.indexOf("fuera de materia") >= 0 ||
      n.indexOf("no pertenece") >= 0) {
    return String("Alerta (Denegado)");
  }

  if (n.indexOf("entrada fuera de horario") >= 0 ||
      n.indexOf("fuera de horario") >= 0 ||
      n.indexOf("no hay clase") >= 0 ||
      n.indexOf("entrada fuera de horario (maestro)") >= 0 ||
      n.indexOf("entrada fuera de horario (maestro)") >= 0 ||
      n.indexOf("entrada fuera de horario (maestro)") >= 0 ) {
    if (n.indexOf("maestro") >= 0 || n.indexOf("teacher") >= 0) return String("Informativa (Maestro)");
    return String("Informativa (Alumno)");
  }

  if (n.indexOf("maestro") >= 0 || n.indexOf("teacher") >= 0) {
    return String("Informativa (Maestro)");
  }

  return String("Otro");
}

// ---------- Página /notifications (lista + modal) ----------
void handleNotificationsPage() {
  String html = htmlHeader("Notificaciones");
  html += "<div class='card'><h2>Notificaciones</h2>";

  // botones principales
  html += "<div style='margin-bottom:12px;display:flex;gap:8px;align-items:center;'>";
  html += "<form method='POST' action='/notifications_clear' onsubmit='return confirm(\"Borrar todas las notificaciones? Esta acción es irreversible.\");' style='display:inline'>";
  html += "<input class='btn btn-red' type='submit' value='🗑️ Borrar Notificaciones'></form>";
  html += "<a class='btn btn-blue' href='/'>Inicio</a>";
  html += "</div>";

  auto nots = readNotifications(500); // leer hasta 500 entradas recientes

  if (nots.size() > 1) std::reverse(nots.begin(), nots.end()); // últimas primero

  if (nots.size() == 0) {
    html += "<p>No hay notificaciones.</p>";
    html += "</div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // estilos para tarjetas + modal
  html += R"rawliteral(
    <style>
      .notif-list { display:flex; flex-direction:column; gap:12px; }
      .notif-item { padding:14px; border-radius:10px; margin-bottom:6px; box-shadow:0 6px 18px rgba(2,6,23,0.06); transition:transform .12s ease; position:relative; }
      .notif-item:hover { transform:translateY(-4px); }
      .notif-header { display:flex; justify-content:space-between; align-items:center; gap:8px; }
      .notif-type { font-weight:800; padding:6px 10px; border-radius:8px; font-size:0.95em; }
      .notif-meta { font-size:0.86em; color:#334155; margin-top:8px; margin-bottom:8px; }
      .notif-note { font-size:1em; color:#0f172a; white-space:pre-wrap; }
      .badge-new { position:absolute; top:8px; right:10px; background:#ef4444; color:white; padding:4px 8px; border-radius:12px; font-weight:800; font-size:0.8em; }

      /* modal */
      .modal-backdrop { position:fixed; left:0; right:0; top:0; bottom:0; background:rgba(2,6,23,0.45); display:none; align-items:center; justify-content:center; z-index:1200; }
      .modal-card { background:#fff; border-radius:12px; max-width:920px; width:96%; padding:18px; box-shadow:0 20px 60px rgba(2,6,23,0.35); transform:translateY(-10px); transition:all .18s ease; }
      .modal-header { display:flex; justify-content:space-between; align-items:center; gap:12px; }
      .modal-title { font-weight:900; font-size:1.2em; color:#0f172a; }
      .modal-body { margin-top:12px; color:#0f172a; font-size:1.05em; }
      .modal-meta-big { margin-top:10px; font-size:1.05em; color:#475569; }
      .modal-actions { display:flex; gap:8px; justify-content:flex-end; margin-top:14px; }

      @media (max-width:680px) {
        .modal-card { padding:12px;}
      }
    </style>
  )rawliteral";

  html += "<div class='notif-list'>";

  for (size_t i = 0; i < nots.size(); ++i) {
    String ln = nots[i];
    auto c = parseQuotedCSVLine(ln);
    String ts =  (c.size() > 0 ? c[0] : "");
    String uid = (c.size() > 1 ? c[1] : "");
    String name = (c.size() > 2 ? c[2] : "");
    String acc = (c.size() > 3 ? c[3] : "");
    String note = (c.size() > 4 ? c[4] : "");

    String tipo = detectNotificationType(note);

    // colores por tipo
    String bg = "#FFFFFF";
    String badgeBg = "#E2E8F0";
    String badgeColor = "#0f172a";

    if (tipo.indexOf("Alerta") == 0) { bg = "#fff0f0"; badgeBg = "#ffb4b4"; badgeColor = "#7a1f1f"; }
    else if (tipo.indexOf("Informativa (Alumno)") == 0) { bg = "#fffef0"; badgeBg = "#fff3bf"; badgeColor = "#664d03"; }
    else if (tipo.indexOf("Informativa (Maestro)") == 0) { bg = "#f0fff0"; badgeBg = "#c7f9d6"; badgeColor = "#065f46"; }
    else if (tipo == "Tarjeta desconocida") { bg = "#fff0f0"; badgeBg = "#ffb4b4"; badgeColor = "#7a1f1f"; } // rojo para desconocidas
    else { bg = "#ffffff"; badgeBg = "#e2e8f0"; badgeColor = "#0f172a"; }

    String noteEsc = htmlEscapeLocal(note);
    String nameEsc = htmlEscapeLocal(name);
    String accEsc = htmlEscapeLocal(acc);
    String tsEsc = htmlEscapeLocal(ts);
    String uidEsc = htmlEscapeLocal(uid);

    String key = notifKey(ts, uid, note);
    bool isRead = isNotifReadLocal(key);

    // tarjeta visual (ocultamos UID en la vista, pero la ponemos como data-* para el modal)
    html += "<div class='notif-item' style='background:" + bg + ";' data-idx='" + String(i) + "'>";
    if (!isRead) {
      html += "<div class='badge-new'>Nuevo</div>";
    }
    html += "<div class='notif-header'>";
    html += "<div style='display:flex;flex-direction:column'>";
    html += "<span class='notif-type' style='background:" + badgeBg + ";color:" + badgeColor + ";'>" + tipo + "</span>";
    html += "</div>";
    html += "<div>";
    html += "<button class='btn btn-blue' onclick='openNotif(" + String(i) + ")'>Info</button>";
    html += "</div>";
    html += "</div>"; // header

    // meta (timestamp + nombre/cuenta)
    String meta = tsEsc;
    if (nameEsc.length()) meta += " • " + nameEsc;
    if (accEsc.length()) meta += " • " + accEsc;

    html += "<div class='notif-meta'>" + meta + "</div>";
    html += "<div class='notif-note'>" + (noteEsc.length() ? noteEsc : "<i>(sin detalles)</i>") + "</div>";

    // datos ocultos para el modal: nota limpia, uid, ts, name, acc, key
    html += "<div id='notif_note_raw_" + String(i) + "' style='display:none'>" + htmlEscapeLocal(note) + "</div>";
    html += "<div id='notif_uid_" + String(i) + "' style='display:none'>" + uidEsc + "</div>";
    html += "<div id='notif_ts_" + String(i) + "' style='display:none'>" + tsEsc + "</div>";
    html += "<div id='notif_name_" + String(i) + "' style='display:none'>" + nameEsc + "</div>";
    html += "<div id='notif_acc_" + String(i) + "' style='display:none'>" + accEsc + "</div>";
    html += "<div id='notif_key_" + String(i) + "' style='display:none'>" + htmlEscapeLocal(key) + "</div>";

    html += "</div>"; // notif-item
  }

  html += "</div>"; // notif-list

  // modal markup (hidden)
  html += R"rawliteral(
    <div id="modal_back" class="modal-backdrop" role="dialog" aria-modal="true">
      <div class="modal-card" role="document">
        <div class="modal-header">
          <div>
            <div class="modal-title" id="modal_type">Notificación</div>
            <div style="font-size:0.9em;color:#475569" id="modal_meta"></div>
          </div>
          <div><button class="btn btn-red" onclick="closeModal()" id="modal_close_btn">Cerrar</button></div>
        </div>
        <div class="modal-body" id="modal_body" tabindex="0"></div>
        <div class="modal-meta-big" id="modal_biginfo"></div>
        <div class="modal-actions">
          <a id="modal_profile_link" class="btn btn-blue" target="_blank" rel="noopener">Ver perfil</a>
          <a id="modal_history_link" class="btn btn-purple" target="_blank" rel="noopener">Ver historial</a>
          <button id="modal_mark_btn" class="btn btn-green" onclick="toggleMark()">Marcar leído</button>
          <button id="modal_delete_btn" class="btn btn-red" onclick="deleteNotif()">Eliminar</button>
        </div>
      </div>
    </div>
  )rawliteral";

  // scripts: abrir modal, poblar, copiar, cerrar, delete, mark
  html += R"rawliteral(
    <script>
      var currentIdx = -1;

      function openNotif(idx) {
        currentIdx = idx;
        var noteEl = document.getElementById('notif_note_raw_' + idx);
        var uidEl  = document.getElementById('notif_uid_' + idx);
        var tsEl   = document.getElementById('notif_ts_' + idx);
        var nameEl = document.getElementById('notif_name_' + idx);
        var accEl  = document.getElementById('notif_acc_' + idx);
        var keyEl  = document.getElementById('notif_key_' + idx);
        if (!noteEl) return;

        var note = noteEl.textContent || noteEl.innerText || '';
        var uid  = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
        var ts   = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
        var name = nameEl ? (nameEl.textContent || nameEl.innerText || '') : '';
        var acc  = accEl ? (accEl.textContent || accEl.innerText || '') : '';
        var key  = keyEl ? (keyEl.textContent || keyEl.innerText || '') : '';

        // tipo heurístico
        var tipo = 'Notificación';
        var ln = note.toLowerCase();
        if (ln.indexOf('tarjeta')!==-1 && ln.indexOf('no registrada')!==-1) tipo = 'Tarjeta desconocida';
        else if (ln.indexOf('fuera de materia')!==-1 || ln.indexOf('no pertenece')!==-1) tipo = 'Alerta (Denegado)';
        else if (ln.indexOf('fuera de horario')!==-1 || ln.indexOf('no hay clase')!==-1) {
          if (ln.indexOf('maestro')!==-1) tipo = 'Informativa (Maestro)';
          else tipo = 'Informativa (Alumno)';
        }

        document.getElementById('modal_type').textContent = tipo;
        var meta = ts;
        if (name) meta += ' • ' + name;
        if (acc) meta += ' • ' + acc;
        document.getElementById('modal_meta').textContent = meta;

        // mostrar nota grande
        document.getElementById('modal_body').textContent = note;

        // mostrar información adicional grande: UID, hora y tipo
        var big = '';
        big += '<b>UID:</b> ' + (uid ? uid : '-') + '<br/>';
        big += '<b>Hora:</b> ' + (ts ? ts : '-') + '<br/>';
        big += '<b>Tipo:</b> ' + tipo + '<br/>';
        if (name) big += '<b>Nombre:</b> ' + name + '<br/>';
        if (acc) big += '<b>Cuenta:</b> ' + acc + '<br/>';
        document.getElementById('modal_biginfo').innerHTML = big;

        // preparar link a perfil (usamos capture_edit si existe)
        var profile = '/capture_edit?uid=' + encodeURIComponent(uid);
        document.getElementById('modal_profile_link').href = profile;

        // preparar link a historial con uid y ts
        var hist = '/history';
        var params = [];
        if (uid) params.push('uid=' + encodeURIComponent(uid));
        if (ts) params.push('ts=' + encodeURIComponent(ts));
        if (params.length) hist += '?' + params.join('&');
        document.getElementById('modal_history_link').href = hist;

        // comprobar si está marcado (servidor no lo sabe aquí), y setear texto del botón
        // pedimos estado al servidor (opcional): pero para simplicidad actualizamos el DOM local
        document.getElementById('modal_mark_btn').textContent = 'Marcar leído';

        // mostrar modal
        var mb = document.getElementById('modal_back');
        if (!mb) return;
        mb.style.display = 'flex';

        // al abrir, solicitar marcar leído automáticamente (UX opcional) -> comentar si no se quiere
        fetch('/notifications_mark', {
          method: 'POST',
          headers: {'Content-Type':'application/x-www-form-urlencoded'},
          body: 'action=mark&ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note)
        }).then(function(r){ if(r.ok) { try { var el=document.querySelectorAll('.notif-item')[idx]; if(el){ var bn=el.querySelector('.badge-new'); if(bn) bn.style.display='none'; } } catch(e){} }});
      }

      function closeModal() {
        var mb = document.getElementById('modal_back');
        if (!mb) return;
        mb.style.display = 'none';
      }

      // toggle mark read/unread
      function toggleMark() {
        if (currentIdx < 0) return;
        var ts = document.getElementById('notif_ts_' + currentIdx).textContent;
        var uid = document.getElementById('notif_uid_' + currentIdx).textContent;
        var note = document.getElementById('notif_note_raw_' + currentIdx).textContent;
        // conmutar: preguntar al servidor para marcar/unmarcar -> aquí pedimos toggle
        fetch('/notifications_mark', {
          method: 'POST',
          headers: {'Content-Type':'application/x-www-form-urlencoded'},
          body: 'action=toggle&ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note)
        }).then(function(resp){ return resp.json(); }).then(function(j){
          if (j && j.status) {
            // actualizar badge y texto del boton
            var el = document.querySelectorAll('.notif-item')[currentIdx];
            if (el) {
              var bn = el.querySelector('.badge-new');
              if (j.read) {
                if (bn) bn.style.display = 'none';
                document.getElementById('modal_mark_btn').textContent = 'Marcar no leído';
              } else {
                if (bn) bn.style.display = 'block';
                document.getElementById('modal_mark_btn').textContent = 'Marcar leído';
              }
            }
          }
        }).catch(function(){});
      }

      // eliminar notificación individual
      function deleteNotif() {
        if (currentIdx < 0) return;
        if (!confirm('Eliminar esta notificación?')) return;
        var ts = document.getElementById('notif_ts_' + currentIdx).textContent;
        var uid = document.getElementById('notif_uid_' + currentIdx).textContent;
        var note = document.getElementById('notif_note_raw_' + currentIdx).textContent;
        fetch('/notifications_delete', {
          method: 'POST',
          headers: {'Content-Type':'application/x-www-form-urlencoded'},
          body: 'ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note)
        }).then(function(r){ if (r.ok) {
          // remover tarjeta del DOM
          var el = document.querySelectorAll('.notif-item')[currentIdx];
          if (el) el.parentNode.removeChild(el);
          closeModal();
        }});
      }

      // cerrar modal con ESC
      document.addEventListener('keydown', function(e){ if (e.key === 'Escape') closeModal(); });
    </script>
  )rawliteral";

  html += "</div>" + htmlFooter();
  server.send(200, "text/html", html);
}

// POST /notifications_clear -> borrar archivo
void handleNotificationsClearPOST() {
  clearNotifications();
  // limpiar marcas también
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

  // leer todo el archivo NOTIF_FILE (usamos NOTIF_FILE macro)
  if (!SPIFFS.exists(NOTIF_FILE)) {
    server.send(404, "text/plain", "no notifs");
    return;
  }

  File f = SPIFFS.open(NOTIF_FILE, FILE_READ);
  std::vector<String> lines;
  if (!f) { server.send(500, "text/plain", "no file"); return; }
  // conservar cabecera si existe (asumimos primera línea header si contiene "timestamp" o similar)
  bool firstLineHandled = false;
  String headerLine;
  while (f.available()) {
    String l = f.readStringUntil('\n');
    if (!firstLineHandled) {
      headerLine = l;
      firstLineHandled = true;
      lines.push_back(l);
      continue;
    }
    l.trim();
    if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    String lts = (c.size()>0?c[0]:"");
    String luid = (c.size()>1?c[1]:"");
    String lnote = (c.size()>4?c[4]:"");
    // comparar ts+uid+note (nota exacta)
    if (lts == ts && luid == uid && lnote == note) {
      // skip (delete)
      continue;
    }
    lines.push_back(l);
  }
  f.close();

  // reescribir
  writeAllLines(NOTIF_FILE, lines);

  // también quitar marca read si existía
  String key = notifKey(ts, uid, note);
  unmarkNotifReadLocal(key);

  server.send(200, "text/plain", "deleted");
}

// POST /notifications_mark -> action=mark|unmark|toggle
// devuelve JSON { status:'ok', read:true/false }
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
  } else if (action == "toggle") {
    if (isNotifReadLocal(key)) { unmarkNotifReadLocal(key); nowRead = false; }
    else { markNotifReadLocal(key); nowRead = true; }
  } else {
    server.send(400, "application/json", "{\"error\":\"unknown action\"}");
    return;
  }

  String j = "{\"status\":\"ok\",\"read\":";
  j += (nowRead ? "true" : "false");
  j += "}";
  server.send(200, "application/json", j);
}
