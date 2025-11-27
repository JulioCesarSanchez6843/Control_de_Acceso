// src/web/notifications.cpp
#include "notifications.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>
#include <algorithm>
#include "display.h" // include por si hay referencias desde otras partes

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

// Detecta tipo (más específico)
static String detectNotificationType(const String &note) {
  String n = note;
  n.toLowerCase();

  if (n.indexOf("no registrada") >= 0 ||
      n.indexOf("no registrado") >= 0 ||
      n.indexOf("tarjeta desconocida") >= 0 ||
      n.indexOf("tarjeta no registrada") >= 0) {
    return String("Tarjeta desconocida");
  }

  if (n.indexOf("intento fuera de materia") >= 0 ||
      n.indexOf("no pertenece") >= 0 ||
      n.indexOf("fuera de materia") >= 0) {
    return String("Alerta (Denegado)");
  }

  if (n.indexOf("fuera de horario") >= 0 ||
      n.indexOf("entrada fuera de horario") >= 0 ||
      n.indexOf("no hay clase") >= 0 ||
      n.indexOf("sin materia asignada") >= 0 ||
      n.indexOf("no materia") >= 0 ||
      n.indexOf("sin materia") >= 0) {
    if (n.indexOf("maestro") >= 0 || n.indexOf("teacher") >= 0) return String("Informativa (Maestro)");
    return String("Informativa (Alumno)");
  }

  if (n.indexOf("teacher") >= 0 || n.indexOf("maestro") >= 0 || n.indexOf("maestra") >= 0) {
    if (n.indexOf("no materia") >= 0 || n.indexOf("no asignado") >= 0 || n.indexOf("no asignado a") >= 0 || n.indexOf("no materia teacher") >= 0) {
      return String("Alerta (Denegado - Maestro)");
    }
    return String("Informativa (Maestro)");
  }

  return String("Otro");
}

// Página /notifications (visual mejorada + modal)
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
      .notif-item { padding:14px; border-radius:10px; margin-bottom:6px; box-shadow:0 6px 18px rgba(2,6,23,0.06); transition:transform .12s ease; }
      .notif-item:hover { transform:translateY(-4px); }
      .notif-header { display:flex; justify-content:space-between; align-items:center; gap:8px; }
      .notif-type { font-weight:800; padding:6px 10px; border-radius:8px; font-size:0.95em; }
      .notif-meta { font-size:0.86em; color:#334155; margin-top:8px; margin-bottom:8px; }
      .notif-note { font-size:1em; color:#0f172a; white-space:pre-wrap; }

      /* modal */
      .modal-backdrop { position:fixed; left:0; right:0; top:0; bottom:0; background:rgba(2,6,23,0.45); display:none; align-items:center; justify-content:center; z-index:1200; }
      .modal-card { background:#fff; border-radius:12px; max-width:820px; width:92%; padding:18px; box-shadow:0 20px 60px rgba(2,6,23,0.35); transform:translateY(-10px); transition:all .18s ease; }
      .modal-header { display:flex; justify-content:space-between; align-items:center; gap:12px; }
      .modal-title { font-weight:900; font-size:1.1em; color:#0f172a; }
      .modal-body { margin-top:12px; color:#0f172a; }
      .modal-actions { display:flex; gap:8px; justify-content:flex-end; margin-top:12px; }

      @media (max-width:560px) {
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

    if (tipo == "Alerta (Denegado)" || tipo == "Alerta (Denegado - Maestro)") { bg = "#fff0f0"; badgeBg = "#ffb4b4"; badgeColor = "#7a1f1f"; }
    else if (tipo == "Informativa (Alumno)") { bg = "#fffef0"; badgeBg = "#fff3bf"; badgeColor = "#664d03"; }
    else if (tipo == "Informativa (Maestro)") { bg = "#f0fff0"; badgeBg = "#c7f9d6"; badgeColor = "#065f46"; }
    else if (tipo == "Tarjeta desconocida") { bg = "#e9f6ff"; badgeBg = "#d0eefc"; badgeColor = "#04566a"; }
    else { bg = "#ffffff"; badgeBg = "#e2e8f0"; badgeColor = "#0f172a"; }

    String noteEsc = htmlEscapeLocal(note);
    String nameEsc = htmlEscapeLocal(name);
    String accEsc = htmlEscapeLocal(acc);
    String tsEsc = htmlEscapeLocal(ts);
    String uidEsc = htmlEscapeLocal(uid);

    // tarjeta visual (ocultamos UID en la vista, pero la ponemos como data-* para el modal)
    html += "<div class='notif-item' style='background:" + bg + ";' data-idx='" + String(i) + "'>";
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

    // datos ocultos para el modal: nota limpia, uid, ts, name, acc
    // usamos ids únicas basadas en el índice
    html += "<div id='notif_note_raw_" + String(i) + "' style='display:none'>" + noteEsc + "</div>";
    html += "<div id='notif_uid_" + String(i) + "' style='display:none'>" + uidEsc + "</div>";
    html += "<div id='notif_ts_" + String(i) + "' style='display:none'>" + tsEsc + "</div>";
    html += "<div id='notif_name_" + String(i) + "' style='display:none'>" + nameEsc + "</div>";
    html += "<div id='notif_acc_" + String(i) + "' style='display:none'>" + accEsc + "</div>";

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
          <div><button class="btn btn-red" onclick="closeModal()">Cerrar</button></div>
        </div>
        <div class="modal-body" id="modal_body"></div>
        <div class="modal-actions">
          <a id="modal_history_link" class="btn btn-blue" target="_blank" rel="noopener">Ver en historial</a>
          <button class="btn btn-green" onclick="copyNote()">Copiar nota</button>
          <button class="btn btn-purple" onclick="closeModal()">Cerrar</button>
        </div>
      </div>
    </div>
  )rawliteral";

  // scripts: abrir modal, poblar, copiar, cerrar
  html += R"rawliteral(
    <script>
      function openNotif(idx) {
        var noteEl = document.getElementById('notif_note_raw_' + idx);
        var uidEl  = document.getElementById('notif_uid_' + idx);
        var tsEl   = document.getElementById('notif_ts_' + idx);
        var nameEl = document.getElementById('notif_name_' + idx);
        var accEl  = document.getElementById('notif_acc_' + idx);
        if (!noteEl) return;

        var note = noteEl.textContent || noteEl.innerText || '';
        var uid  = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
        var ts   = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
        var name = nameEl ? (nameEl.textContent || nameEl.innerText || '') : '';
        var acc  = accEl ? (accEl.textContent || accEl.innerText || '') : '';

        // tipo: intentar inferir con una heurística simple
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
        document.getElementById('modal_body').textContent = note;

        // preparar link a historial (si hay UID lo agregamos)
        var hist = '/history';
        var params = [];
        if (uid) params.push('uid=' + encodeURIComponent(uid));
        if (ts) params.push('ts=' + encodeURIComponent(ts));
        if (params.length) hist += '?' + params.join('&');
        document.getElementById('modal_history_link').href = hist;

        // mostrar modal
        var mb = document.getElementById('modal_back');
        if (!mb) return;
        mb.style.display = 'flex';
        // focus accessibility
        setTimeout(function(){document.getElementById('modal_body').focus && document.getElementById('modal_body').focus();},100);
      }

      function closeModal() {
        var mb = document.getElementById('modal_back');
        if (!mb) return;
        mb.style.display = 'none';
      }

      function copyNote() {
        var body = document.getElementById('modal_body');
        if (!body) return;
        var txt = body.textContent || body.innerText || '';
        if (!navigator.clipboard) {
          // fallback
          var ta = document.createElement('textarea');
          ta.value = txt; document.body.appendChild(ta); ta.select();
          try { document.execCommand('copy'); alert('Texto copiado'); } catch(e) { alert('Copia no soportada'); }
          document.body.removeChild(ta);
          return;
        }
        navigator.clipboard.writeText(txt).then(function(){ alert('Texto copiado al portapapeles'); }, function(){ alert('No se pudo copiar'); });
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
  server.sendHeader("Location", "/notifications");
  server.send(303, "text/plain", "Notificaciones borradas");
}
