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
  String frag = note;
  if (frag.length() > 80) frag = frag.substring(0, 80);
  frag.replace("\n", " ");
  frag.replace("\r", " ");
  k += "|" + frag;
  return k;
}

// Marca / unmarca helpers
static void markNotifReadLocal(const String &key) {
  if (!SPIFFS.exists(NOTIF_READ_FILE_LOCAL)) {
    File f = SPIFFS.open(NOTIF_READ_FILE_LOCAL, FILE_WRITE);
    if (!f) return;
    f.close();
  }
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
      while (start < (int)note.length() && (note[start] == ':' || note[start] == '-' || isSpace(note[start]))) start++;
      int end = start;
      while (end < (int)note.length()) {
        char ch = note[end];
        if (ch == '\n' || ch == '\r' || ch == ',' || ch == ';' || ch == '.' || ch == '•') break;
        end++;
      }
      String val = note.substring(start, end);
      val.trim();
      return val;
    }
  }
  return String();
}

// ---------- Página /notifications (lista + mini-modal) ----------
// Nota: vista de UNA columna. Por defecto muestra No leídas; el botón "Leídas"
// cambia a la lista de leídas (comportamiento tipo switch). Botones muestran estado activo.
void handleNotificationsPage() {
  String html = htmlHeader("Notificaciones");
  // Título + badge (solo no leídas)
  html += "<div class='card'><h2 style='display:flex;align-items:center;gap:12px;'>Notificaciones <span id='unread_badge' style='background:#ef4444;color:#fff;padding:6px 10px;border-radius:999px;font-weight:800;font-size:0.95em;'>0</span></h2>";

  // botones principales + toggles
  html += "<div style='margin-bottom:12px;display:flex;gap:8px;align-items:center;flex-wrap:wrap;'>";
  html += "<form method='POST' action='/notifications_clear' onsubmit='return confirm(\"Borrar todas las notificaciones? Esta acción es irreversible.\");' style='display:inline'>";
  html += "<input class='btn btn-red' type='submit' value='🗑️ Borrar Notificaciones'></form>";
  html += "<a class='btn btn-blue' href='/'>Inicio</a>";
  // botón switch: No leídas / Leídas
  html += "<div style='margin-left:8px;display:flex;gap:6px;align-items:center;'>";
  html += "<button id='btn_show_unread' class='btn btn-switch' onclick='showUnread(event)'>No leídas</button>";
  html += "<button id='btn_show_read' class='btn btn-switch' onclick='showRead(event)'>Leídas</button>";
  html += "</div>";
  html += "</div>";

  // pequeño título de vista que indica claramente qué lista está activa
  html += "<div id='view_title' style='font-weight:700;margin-bottom:8px;color:#0f172a;'></div>";

  // filtros
  html += "<div class='filters' style='margin-bottom:10px;display:flex;gap:8px;flex-wrap:wrap;align-items:center;'>";
  html += "<input id='nf_materia' placeholder='Filtrar por materia' style='min-width:160px'>";
  html += "<input id='nf_prof' placeholder='Filtrar por profesor' style='min-width:160px'>";
  html += "<input id='nf_name' placeholder='Filtrar por nombre (alumno/maestro)' style='min-width:160px'>";
  html += "<input id='nf_date' type='date' placeholder='Filtrar por fecha' style='min-width:140px'>";
  html += "<button class='search-btn btn btn-blue' onclick='applyNotifFilters()'>Aplicar</button>";
  html += "<button class='search-btn btn btn-green' onclick='clearNotifFilters()'>Limpiar</button>";
  html += "</div>";

  auto nots = readNotifications(500);
  if (nots.size() > 1) std::reverse(nots.begin(), nots.end());
  if (nots.size() == 0) {
    html += "<p>No hay notificaciones.</p>";
    html += "</div>" + htmlFooter();
    server.send(200, "text/html", html);
    return;
  }

  // separar leídas/no leídas
  std::vector<size_t> unreadIdx; std::vector<size_t> readIdx;
  std::vector<String> parsedLines; parsedLines.reserve(nots.size());
  for (size_t i=0;i<nots.size();++i) {
    String ln = nots[i];
    parsedLines.push_back(ln);
    auto c = parseQuotedCSVLine(ln);
    String ts = (c.size()>0?c[0]:"");
    String uid = (c.size()>1?c[1]:"");
    String note = (c.size()>4?c[4]:"");
    String key = notifKey(ts, uid, note);
    if (isNotifReadLocal(key)) readIdx.push_back(i);
    else unreadIdx.push_back(i);
  }

  // styles + modal (ahora layout de una columna)
  html += R"rawliteral(
    <style>
      .single-col { display:flex; flex-direction:column; gap:12px; }
      .notif-list { display:flex; flex-direction:column; gap:12px; }
      .notif-item { padding:12px; border-radius:10px; box-shadow:0 6px 18px rgba(2,6,23,0.06); transition:transform .12s ease; position:relative; overflow:hidden; display:flex; flex-direction:column; cursor:pointer; }
      .notif-item:hover { transform:translateY(-4px); }
      .notif-header { display:flex; justify-content:space-between; align-items:flex-start; gap:8px; }
      .notif-type { font-weight:800; padding:6px 10px; border-radius:8px; font-size:0.95em; display:inline-block; }
      .notif-meta { font-size:0.86em; color:#374151; margin-top:8px; margin-bottom:8px; }
      .notif-note { font-size:1em; color:#0f172a; white-space:pre-wrap; margin-top:6px;}
      .compact-actions { display:flex; gap:8px; align-items:center; }
      .badge-new { background:#ef4444; color:white; padding:6px 10px; border-radius:8px; font-weight:800; font-size:0.95em; display:inline-block; }
      .btn-mark-inline { font-size:0.9em; padding:6px 10px; }
      .modal-backdrop { position:fixed; left:0; right:0; top:0; bottom:0; background:rgba(2,6,23,0.35); display:none; align-items:center; justify-content:center; z-index:1200; }
      .modal-card { background:#fff; border-radius:12px; width:min(600px,92%); max-width:600px; padding:14px; box-shadow:0 20px 60px rgba(2,6,23,0.35); transform:translateY(0); transition:all .12s ease; }
      .modal-header { display:flex; justify-content:space-between; align-items:center; gap:12px; }
      .modal-title { font-weight:900; font-size:1.05em; color:#0f172a; }
      .modal-body { margin-top:10px; color:#0f172a; font-size:0.98em; max-height:320px; overflow:auto; }
      .modal-meta-big { margin-top:10px; font-size:0.95em; color:#475569; }
      .modal-actions { display:flex; gap:8px; justify-content:flex-end; margin-top:12px; }
      /* Estilos para los botones switch: .btn-switch y estado activo .active */
      .btn-switch { background:transparent; border:1px solid #e5e7eb; color:#0f172a; padding:8px 12px; border-radius:8px; cursor:pointer; font-weight:600; }
      .btn-switch:hover { background:#f3f4f6; }
      .btn-switch.active { background:#1d4ed8; color:white; border-color:transparent; box-shadow:0 6px 18px rgba(29,78,216,0.12); }
      /* Mejores estilos para botones con color (usar las clases globales .btn-green/.btn-orange/.btn-red) */
      .btn { border:none; border-radius:8px; padding:8px 12px; cursor:pointer; font-weight:700; }
      .btn-green { background:#10b981; color:#fff; }
      .btn-orange { background:#f59e0b; color:#fff; }
      .btn-red { background:#ef4444; color:#fff; }
      .btn-blue { background:#06b6d4; color:#05345b; }
      .btn-purple { background:#8b5cf6; color:#fff; }
      @media (max-width:900px) { .compact-actions { flex-direction:row; } }
    </style>
  )rawliteral";

  // SINGLE COLUMN container: both lists exist but only one is visible at a time (JS switch)
  html += "<div class='single-col'>";
  // Unread list (visible by default)
  html += "<div id='list_unread_container'><h3 id='hdr_unread'>No leídas (" + String(unreadIdx.size()) + ")</h3><div id='list_unread' class='notif-list'>";
  for (size_t idxPos=0; idxPos<unreadIdx.size(); ++idxPos) {
    size_t i = unreadIdx[idxPos];
    String ln = parsedLines[i];
    auto c = parseQuotedCSVLine(ln);
    String ts = (c.size()>0?c[0]:"");
    String uid = (c.size()>1?c[1]:"");
    String name = (c.size()>2?c[2]:"");
    String acc = (c.size()>3?c[3]:"");
    String note = (c.size()>4?c[4]:"");
    String tipo = detectNotificationType(note);
    String materiaFromNote = extractFieldFromNote(note, "Materia,Materia:,materia");
    String profFromNote = extractFieldFromNote(note, "Profesor,Profesor:,Maestro,Maestro:,Teacher,Teacher:");
    String bg="#fff", badgeBg="#E2E8F0", badgeColor="#0f172a";
    if (tipo.indexOf("Alerta")==0 || tipo=="Tarjeta desconocida") { bg="#fff4f4"; badgeBg="#ffcccc"; badgeColor="#7a1f1f"; }
    else if (tipo.indexOf("Informativa (Alumno)") == 0) { bg="#fffef0"; badgeBg="#fff3bf"; badgeColor="#664d03"; }
    else if (tipo.indexOf("Informativa (Maestro)") == 0) { bg="#f0fff0"; badgeBg="#c7f9d6"; badgeColor="#065f46"; }

    String noteEsc = htmlEscapeLocal(note);
    String nameEsc = htmlEscapeLocal(name);
    String accEsc = htmlEscapeLocal(acc);
    String tsEsc = htmlEscapeLocal(ts);
    String uidEsc = htmlEscapeLocal(uid);
    String key = notifKey(ts, uid, note);

    String dataAttrs = " data-ts='" + tsEsc + "' data-uid='" + uidEsc + "' data-name='" + nameEsc + "' data-note='" + htmlEscapeLocal(note) + "'";
    if (materiaFromNote.length()) dataAttrs += " data-materia='" + htmlEscapeLocal(materiaFromNote) + "'";
    if (profFromNote.length()) dataAttrs += " data-prof='" + htmlEscapeLocal(profFromNote) + "'";
    // entire item clickable -> openNotif(i)
    html += "<div class='notif-item' style='background:" + bg + ";' data-idx='" + String(i) + "'" + dataAttrs + " onclick='openNotif(" + String(i) + ")'>";
    html += "<div class='notif-header'>";
    html += "<div style='display:flex;flex-direction:column;gap:6px;'>";
    html += "<span class='notif-type' style='background:" + badgeBg + ";color:" + badgeColor + ";'>" + tipo + "</span>";
    html += "</div>";
    // right actions: badge + inline mark
    html += "<div style='display:flex;align-items:center;gap:8px;'>";
    html += "<div class='action-left'><div class='badge-new' id='badge_new_" + String(i) + "'>Nuevo</div></div>";
    html += "<div class='compact-actions'>";
    // inline mark: ahora con color verde
    html += "<button class='btn btn-green' onclick='event.stopPropagation(); markInline(" + String(i) + ")'>Marcar leído</button>";
    html += "</div></div>";
    html += "</div>"; // end header

    String meta = tsEsc;
    if (nameEsc.length()) meta += " • " + nameEsc;
    if (accEsc.length()) meta += " • " + accEsc;
    if (materiaFromNote.length()) meta += " • " + htmlEscapeLocal(materiaFromNote);
    html += "<div class='notif-meta'>" + meta + "</div>";
    html += "<div class='notif-note'>" + (noteEsc.length() ? noteEsc : "<i>(sin detalles)</i>") + "</div>";

    // ocultos
    html += "<div id='notif_note_raw_" + String(i) + "' style='display:none'>" + htmlEscapeLocal(note) + "</div>";
    html += "<div id='notif_uid_" + String(i) + "' style='display:none'>" + uidEsc + "</div>";
    html += "<div id='notif_ts_" + String(i) + "' style='display:none'>" + tsEsc + "</div>";
    html += "<div id='notif_name_" + String(i) + "' style='display:none'>" + nameEsc + "</div>";
    html += "<div id='notif_acc_" + String(i) + "' style='display:none'>" + accEsc + "</div>";
    html += "<div id='notif_key_" + String(i) + "' style='display:none'>" + htmlEscapeLocal(key) + "</div>";

    html += "</div>";
  }
  html += "</div></div>"; // end unread container

  // Read list (hidden by default; same items but marked read)
  html += "<div id='list_read_container' style='display:none;'><h3 id='hdr_read'>Leídas (" + String(readIdx.size()) + ")</h3><div id='list_read' class='notif-list'>";
  for (size_t idxPos=0; idxPos<readIdx.size(); ++idxPos) {
    size_t i = readIdx[idxPos];
    String ln = parsedLines[i];
    auto c = parseQuotedCSVLine(ln);
    String ts = (c.size()>0?c[0]:"");
    String uid = (c.size()>1?c[1]:"");
    String name = (c.size()>2?c[2]:"");
    String acc = (c.size()>3?c[3]:"");
    String note = (c.size()>4?c[4]:"");
    String tipo = detectNotificationType(note);
    String materiaFromNote = extractFieldFromNote(note, "Materia,Materia:,materia");
    String profFromNote = extractFieldFromNote(note, "Profesor,Profesor:,Maestro,Maestro:,Teacher,Teacher:");
    String bg="#FAFAFA";
    if (tipo.indexOf("Alerta")==0 || tipo=="Tarjeta desconocida") bg="#fff7f7";
    else if (tipo.indexOf("Informativa (Alumno)") == 0) bg="#fffeef";
    else if (tipo.indexOf("Informativa (Maestro)") == 0) bg="#f6fff6";

    String noteEsc = htmlEscapeLocal(note);
    String nameEsc = htmlEscapeLocal(name);
    String accEsc = htmlEscapeLocal(acc);
    String tsEsc = htmlEscapeLocal(ts);
    String uidEsc = htmlEscapeLocal(uid);
    String key = notifKey(ts, uid, note);

    String dataAttrs = " data-ts='" + tsEsc + "' data-uid='" + uidEsc + "' data-name='" + nameEsc + "' data-note='" + htmlEscapeLocal(note) + "'";
    if (materiaFromNote.length()) dataAttrs += " data-materia='" + htmlEscapeLocal(materiaFromNote) + "'";
    if (profFromNote.length()) dataAttrs += " data-prof='" + htmlEscapeLocal(profFromNote) + "'";
    html += "<div class='notif-item' style='background:" + bg + ";' data-idx='" + String(i) + "'" + dataAttrs + " onclick='openNotif(" + String(i) + ")'>";
    html += "<div class='notif-header'>";
    html += "<div style='display:flex;flex-direction:column;gap:6px;'>";
    html += "<span class='notif-type' style='background:#E2E8F0;color:#0f172a;'>" + tipo + "</span>";
    html += "</div>";
    html += "<div style='display:flex;align-items:center;gap:8px;'>";
    html += "<div class='action-left'><div style='height:28px;'></div></div>";
    html += "<div class='compact-actions'>";
    // inline mark to return to unread -> ahora con color naranja para "revertir"
    html += "<button class='btn btn-orange' onclick='event.stopPropagation(); toggleInline(" + String(i) + ")'>Marcar no leído</button>";
    html += "</div></div>";
    html += "</div>";
    String meta = tsEsc; if (nameEsc.length()) meta += " • " + nameEsc; if (accEsc.length()) meta += " • " + accEsc;
    if (materiaFromNote.length()) meta += " • " + htmlEscapeLocal(materiaFromNote);
    html += "<div class='notif-meta'>" + meta + "</div>";
    html += "<div class='notif-note'>" + (noteEsc.length() ? noteEsc : "<i>(sin detalles)</i>") + "</div>";
    html += "<div id='notif_note_raw_" + String(i) + "' style='display:none'>" + htmlEscapeLocal(note) + "</div>";
    html += "<div id='notif_uid_" + String(i) + "' style='display:none'>" + uidEsc + "</div>";
    html += "<div id='notif_ts_" + String(i) + "' style='display:none'>" + tsEsc + "</div>";
    html += "<div id='notif_name_" + String(i) + "' style='display:none'>" + nameEsc + "</div>";
    html += "<div id='notif_acc_" + String(i) + "' style='display:none'>" + accEsc + "</div>";
    html += "<div id='notif_key_" + String(i) + "' style='display:none'>" + htmlEscapeLocal(key) + "</div>";
    html += "</div>";
  }
  html += "</div></div>"; // end read container

  html += "</div>"; // end single-col

  // MINI-MODAL (pequeña ventana flotante) - se muestra centrada y con X
  html += R"rawliteral(
    <div id="modal_back" class="modal-backdrop" role="dialog" aria-modal="true" style="display:none">
      <div class="modal-card" role="document" aria-labelledby="modal_type" aria-describedby="modal_body">
        <div class="modal-header">
          <div>
            <div class="modal-title" id="modal_type">Notificación</div>
            <div style="font-size:0.9em;color:#475569" id="modal_meta"></div>
          </div>
          <div><button class="btn btn-red" onclick="closeModal()" id="modal_close_btn">✕</button></div>
        </div>
        <div class="modal-body" id="modal_body" tabindex="0"></div>
        <div class="modal-meta-big" id="modal_biginfo"></div>
        <div class="modal-actions" id="modal_actions">
          <a id="modal_profile_link" class="btn btn-blue" target="_blank" rel="noopener">Ver perfil</a>
          <a id="modal_history_link" class="btn btn-purple" target="_blank" rel="noopener">Ver historial</a>
          <button id="modal_mark_btn" class="btn btn-green" onclick="toggleMark()">Marcar leído</button>
          <button id="modal_delete_btn" class="btn btn-red" onclick="deleteNotif()">Eliminar</button>
        </div>
      </div>
    </div>
  )rawliteral";

  // SCRIPTS
  // Notas:
  // - Cuando abrimos el modal revisamos si el elemento está en la lista 'read' o 'unread'
  //   para ajustar el texto y color del botón de marcar.
  html += R"rawliteral(
    <script>
      var currentIdx = -1;

      document.addEventListener('DOMContentLoaded', function(){
        // por defecto mostrar no leídas
        showUnread();
        updateCounts();
      });

      function setSwitchActive(isUnread){
        var bU = document.getElementById('btn_show_unread');
        var bR = document.getElementById('btn_show_read');
        if (isUnread) {
          bU.classList.add('active');
          bR.classList.remove('active');
          document.getElementById('view_title').textContent = 'Mostrando: No leídas';
        } else {
          bU.classList.remove('active');
          bR.classList.add('active');
          document.getElementById('view_title').textContent = 'Mostrando: Leídas';
        }
      }

      function showRead(e){ if (e) e.preventDefault(); document.getElementById('list_unread_container').style.display='none'; document.getElementById('list_read_container').style.display='block'; setSwitchActive(false); updateCounts(); }
      function showUnread(e){ if (e) e.preventDefault(); document.getElementById('list_unread_container').style.display='block'; document.getElementById('list_read_container').style.display='none'; setSwitchActive(true); updateCounts(); }

      function applyNotifFilters(){
        var fm=document.getElementById('nf_materia').value.trim().toLowerCase();
        var fp=document.getElementById('nf_prof').value.trim().toLowerCase();
        var fn=document.getElementById('nf_name').value.trim().toLowerCase();
        var fdate=document.getElementById('nf_date').value.trim();
        var lists = [document.querySelectorAll('#list_unread .notif-item'), document.querySelectorAll('#list_read .notif-item')];
        for (var li=0; li<lists.length; ++li){
          var nodes = lists[li];
          for (var i=0;i<nodes.length;i++){
            var item = nodes[i];
            var ts = (item.getAttribute('data-ts')||'').toLowerCase();
            var name = (item.getAttribute('data-name')||'').toLowerCase();
            var note = (item.getAttribute('data-note')||'').toLowerCase();
            var mat = (item.getAttribute('data-materia')||'').toLowerCase();
            var prof = (item.getAttribute('data-prof')||'').toLowerCase();
            var ok = true;
            if (fm.length && mat.indexOf(fm) === -1 && name.indexOf(fm) === -1 && note.indexOf(fm) === -1) ok = false;
            if (fp.length && prof.indexOf(fp) === -1 && name.indexOf(fp) === -1 && note.indexOf(fp) === -1) ok = false;
            if (fn.length && name.indexOf(fn) === -1 && note.indexOf(fn) === -1) ok = false;
            if (fdate.length && ts.indexOf(fdate) === -1) ok = false;
            item.style.display = ok ? '' : 'none';
          }
        }
      }
      function clearNotifFilters(){
        document.getElementById('nf_materia').value='';
        document.getElementById('nf_prof').value='';
        document.getElementById('nf_name').value='';
        document.getElementById('nf_date').value='';
        applyNotifFilters();
      }

      // marcar directamente sin abrir modal (boton inline) => marca leído
      function markInline(idx){
        try {
          var noteEl = document.getElementById('notif_note_raw_' + idx);
          var uidEl  = document.getElementById('notif_uid_' + idx);
          var tsEl   = document.getElementById('notif_ts_' + idx);
          if (!noteEl) return;
          var note = noteEl.textContent || noteEl.innerText || '';
          var uid  = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
          var ts   = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
          fetch('/notifications_mark', {
            method:'POST',
            headers:{'Content-Type':'application/x-www-form-urlencoded'},
            body: 'action=mark&ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note)
          }).then(function(resp){
            if (resp.ok) {
              try {
                var nodes = document.querySelectorAll('#list_unread .notif-item');
                for (var i=0;i<nodes.length;i++){
                  var el = nodes[i];
                  if ((el.getAttribute('data-ts')||'') === ts && (el.getAttribute('data-uid')||'') === uid && ((el.getAttribute('data-note')||'').indexOf((note||'').slice(0,20))!==-1)) {
                    var badge = el.querySelector('.badge-new'); if (badge) badge.parentNode.removeChild(badge);
                    el.parentNode.removeChild(el);
                    var rl = document.getElementById('list_read');
                    if (rl) rl.insertBefore(el, rl.firstChild);
                    updateCounts();
                    break;
                  }
                }
              } catch(e){}
            }
          }).catch(function(){});
        } catch(e){}
      }

      // marcar inline desde leídas -> volver a no leída (unmark)
      function toggleInline(idx){
        try {
          var noteEl = document.getElementById('notif_note_raw_' + idx);
          var uidEl  = document.getElementById('notif_uid_' + idx);
          var tsEl   = document.getElementById('notif_ts_' + idx);
          if (!noteEl) return;
          var note = noteEl.textContent || noteEl.innerText || '';
          var uid  = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
          var ts   = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
          fetch('/notifications_mark', {
            method:'POST',
            headers:{'Content-Type':'application/x-www-form-urlencoded'},
            body: 'action=unmark&ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note)
          }).then(function(resp){
            if (resp.ok) {
              try {
                var nodes = document.querySelectorAll('#list_read .notif-item');
                for (var i=0;i<nodes.length;i++){
                  var el = nodes[i];
                  if ((el.getAttribute('data-ts')||'') === ts && (el.getAttribute('data-uid')||'') === uid && ((el.getAttribute('data-note')||'').indexOf((note||'').slice(0,20))!==-1)) {
                    // add badge-new
                    var b = document.createElement('div'); b.className='badge-new'; b.textContent='Nuevo';
                    var meta = el.querySelector('.notif-meta');
                    if (meta) el.insertBefore(b, meta);
                    el.parentNode.removeChild(el);
                    document.getElementById('list_unread').insertBefore(el, document.getElementById('list_unread').firstChild);
                    updateCounts();
                    break;
                  }
                }
              } catch(e){}
            }
          }).catch(function(){});
        } catch(e){}
      }

      // abrir mini-modal (pequeña ventana centrada) y marcar leído si viene de unread
      function openNotif(idx){
        currentIdx = idx;
        var noteEl = document.getElementById('notif_note_raw_' + idx);
        var uidEl  = document.getElementById('notif_uid_' + idx);
        var tsEl   = document.getElementById('notif_ts_' + idx);
        var nameEl = document.getElementById('notif_name_' + idx);
        var accEl  = document.getElementById('notif_acc_' + idx);
        var itemEl = document.querySelector('.notif-item[data-idx=\"' + idx + '\"]');
        if (!noteEl) return;
        var note = noteEl.textContent || noteEl.innerText || '';
        var uid  = uidEl ? (uidEl.textContent || uidEl.innerText || '') : '';
        var ts   = tsEl ? (tsEl.textContent || tsEl.innerText || '') : '';
        var name = nameEl ? (nameEl.textContent || nameEl.innerText || '') : '';
        var acc  = accEl ? (accEl.textContent || accEl.innerText || '') : '';

        // display: replace 'teacher' -> 'maestro'
        var noteDisplay = (note||'').replace(/teacher/gi,'maestro');

        // heurística tipo
        var tipo = 'Notificación';
        var ln = (note||'').toLowerCase();
        if (ln.indexOf('tarjeta')!==-1 && ln.indexOf('no registrada')!==-1) tipo = 'Tarjeta desconocida';
        else if (ln.indexOf('fuera de materia')!==-1 || ln.indexOf('no pertenece')!==-1) tipo = 'Alerta (Denegado)';
        else if (ln.indexOf('fuera de horario')!==-1 || ln.indexOf('no hay clase')!==-1) {
          if (ln.indexOf('maestro')!==-1 || ln.indexOf('teacher')!==-1) tipo = 'Informativa (Maestro)'; else tipo = 'Informativa (Alumno)';
        }

        document.getElementById('modal_type').textContent = tipo;
        // prefer prof extracted in data-prof
        var prof = (itemEl && itemEl.getAttribute('data-prof')) ? itemEl.getAttribute('data-prof') : '';
        var meta = ts;
        if (prof) meta += ' • ' + prof;
        else if (name) meta += ' • ' + name;
        if (acc) meta += ' • ' + acc;
        document.getElementById('modal_meta').textContent = meta;
        document.getElementById('modal_body').textContent = noteDisplay;

        var big = '';
        big += '<b>UID:</b> ' + (uid?uid:'-') + '<br/>';
        big += '<b>Hora:</b> ' + (ts?ts:'-') + '<br/>';
        big += '<b>Tipo:</b> ' + tipo + '<br/>';
        if (prof) big += '<b>Maestro:</b> ' + prof + '<br/>';
        else if (name) big += '<b>Nombre:</b> ' + name + '<br/>';
        if (acc) big += '<b>Cuenta:</b> ' + acc + '<br/>';
        document.getElementById('modal_biginfo').innerHTML = big;

        // prepare profile/history links only if not 'Tarjeta desconocida'
        var profileEl = document.getElementById('modal_profile_link');
        var historyEl = document.getElementById('modal_history_link');
        var markBtn = document.getElementById('modal_mark_btn');
        if (tipo === 'Tarjeta desconocida') {
          profileEl.style.display = 'none';
          historyEl.style.display = 'none';
          markBtn.style.display = 'inline-block';
        } else {
          profileEl.style.display = 'inline-block';
          historyEl.style.display = 'inline-block';
        }

        var profile_base = '/capture_edit?uid=' + encodeURIComponent(uid);
        if (prof) profile_base = '/teachers_all?search_uid=' + encodeURIComponent(uid);
        else if ((note||'').toLowerCase().indexOf('maestro')!==-1 || (note||'').toLowerCase().indexOf('teacher')!==-1) profile_base = '/teachers_all?search_uid=' + encodeURIComponent(uid);
        else profile_base = '/students_all?search_uid=' + encodeURIComponent(uid);
        profileEl.href = profile_base;

        // history link will include uid and ts (date prefix)
        var hist = '/history'; var params=[];
        if (uid) params.push('uid=' + encodeURIComponent(uid));
        if (ts && ts.length>=10) params.push('date=' + encodeURIComponent(ts.substring(0,10)));
        if (params.length) hist += '?' + params.join('&');
        historyEl.href = hist;

        // ajustar texto/color del botón de marcar según si el item está en read o unread
        try {
          var inRead = false;
          var rl = document.getElementById('list_read');
          if (rl && rl.contains(itemEl)) inRead = true;
          if (inRead) {
            markBtn.textContent = 'Marcar no leído';
            markBtn.className = 'btn btn-orange';
          } else {
            markBtn.textContent = 'Marcar leído';
            markBtn.className = 'btn btn-green';
          }
        } catch(e){}

        // mostrar mini-modal
        var mb = document.getElementById('modal_back'); if (!mb) return; mb.style.display = 'flex';

        // mark on server if opening from unread (attempt to move)
        fetch('/notifications_mark', {
          method:'POST',
          headers:{'Content-Type':'application/x-www-form-urlencoded'},
          body: 'action=mark&ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note)
        }).then(function(resp){
          if (resp.ok) {
            try {
              var nodes = document.querySelectorAll('#list_unread .notif-item');
              for (var i=0;i<nodes.length;i++){
                var el = nodes[i];
                if ((el.getAttribute('data-ts')||'') === ts && (el.getAttribute('data-uid')||'') === uid && ((el.getAttribute('data-note')||'').indexOf((note||'').slice(0,20))!==-1)) {
                  var badge = el.querySelector('.badge-new'); if (badge) badge.parentNode.removeChild(badge);
                  el.parentNode.removeChild(el);
                  var rl = document.getElementById('list_read');
                  if (rl) rl.insertBefore(el, rl.firstChild);
                  updateCounts();
                  // actualizar botón del modal (ahora ya está leído)
                  try { markBtn.textContent = 'Marcar no leído'; markBtn.className = 'btn btn-orange'; } catch(e){}
                  break;
                }
              }
            } catch(e){}
          }
        }).catch(function(){});
      }

      function closeModal(){ var mb = document.getElementById('modal_back'); if (!mb) return; mb.style.display='none'; currentIdx=-1; }

      // toggle mark from modal (works both ways)
      function toggleMark(){
        if (currentIdx < 0) return;
        var ts = document.getElementById('notif_ts_' + currentIdx).textContent;
        var uid = document.getElementById('notif_uid_' + currentIdx).textContent;
        var note = document.getElementById('notif_note_raw_' + currentIdx).textContent;
        fetch('/notifications_mark', {
          method:'POST',
          headers:{'Content-Type':'application/x-www-form-urlencoded'},
          body: 'action=toggle&ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note)
        }).then(function(r){ return r.json(); }).then(function(j){
          if (j && j.status) {
            try {
              var markBtn = document.getElementById('modal_mark_btn');
              if (j.read) {
                var nodes = document.querySelectorAll('#list_unread .notif-item');
                for (var i=0;i<nodes.length;i++){
                  var el = nodes[i];
                  if ((el.getAttribute('data-ts')||'') === ts && (el.getAttribute('data-uid')||'') === uid && ((el.getAttribute('data-note')||'').indexOf((note||'').slice(0,20))!==-1)) {
                    var badge = el.querySelector('.badge-new'); if (badge) badge.parentNode.removeChild(badge);
                    el.parentNode.removeChild(el);
                    document.getElementById('list_read').insertBefore(el, document.getElementById('list_read').firstChild);
                    updateCounts();
                    break;
                  }
                }
                // actualizar botón modal
                if (markBtn) { markBtn.textContent = 'Marcar no leído'; markBtn.className = 'btn btn-orange'; }
              } else {
                var nodes = document.querySelectorAll('#list_read .notif-item');
                for (var i=0;i<nodes.length;i++){
                  var el = nodes[i];
                  if ((el.getAttribute('data-ts')||'') === ts && (el.getAttribute('data-uid')||'') === uid && ((el.getAttribute('data-note')||'').indexOf((note||'').slice(0,20))!==-1)) {
                    var badge = document.createElement('div'); badge.className='badge-new'; badge.textContent='Nuevo';
                    var meta = el.querySelector('.notif-meta');
                    if (meta) el.insertBefore(badge, meta);
                    el.parentNode.removeChild(el);
                    document.getElementById('list_unread').insertBefore(el, document.getElementById('list_unread').firstChild);
                    updateCounts();
                    break;
                  }
                }
                // actualizar botón modal
                if (markBtn) { markBtn.textContent = 'Marcar leído'; markBtn.className = 'btn btn-green'; }
              }
            } catch(e){}
          }
        }).catch(function(){});
      }

      function deleteNotif(){
        if (currentIdx < 0) return;
        if (!confirm('Eliminar esta notificación?')) return;
        var ts = document.getElementById('notif_ts_' + currentIdx).textContent;
        var uid = document.getElementById('notif_uid_' + currentIdx).textContent;
        var note = document.getElementById('notif_note_raw_' + currentIdx).textContent;
        fetch('/notifications_delete', {
          method:'POST',
          headers:{'Content-Type':'application/x-www-form-urlencoded'},
          body: 'ts=' + encodeURIComponent(ts) + '&uid=' + encodeURIComponent(uid) + '&note=' + encodeURIComponent(note)
        }).then(function(r){ if (r.ok) {
          try {
            var sel = '#list_unread .notif-item, #list_read .notif-item';
            var nodes = document.querySelectorAll(sel);
            for (var i=0;i<nodes.length;i++){
              var el = nodes[i];
              if ((el.getAttribute('data-ts')||'') === ts && (el.getAttribute('data-uid')||'') === uid && ((el.getAttribute('data-note')||'').indexOf((note||'').slice(0,20))!==-1)) {
                el.parentNode.removeChild(el); updateCounts(); break;
              }
            }
          } catch(e){}
          closeModal();
        }}).catch(function(){});
      }

      function updateCounts(){
        var unread = document.querySelectorAll('#list_unread .notif-item').length;
        var read = document.querySelectorAll('#list_read .notif-item').length;
        var hUnread = document.querySelector('#hdr_unread');
        var hRead = document.querySelector('#hdr_read');
        if (hUnread) hUnread.textContent = 'No leídas (' + unread + ')';
        if (hRead) hRead.textContent = 'Leídas (' + read + ')';
        var badge = document.getElementById('unread_badge');
        if (badge) badge.textContent = String(unread);
        if (badge) badge.style.display = (unread>0 ? 'inline-block' : 'none');
      }

      document.addEventListener('keydown', function(e){ if (e.key === 'Escape') closeModal(); });
    </script>
  )rawliteral";

  html += "</div>" + htmlFooter();
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
  String key = notifKey(ts, uid, note);
  unmarkNotifReadLocal(key);
  server.send(200, "text/plain", "deleted");
}

// POST /notifications_mark -> action=mark|unmark|toggle
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
  if (action == "mark") { markNotifReadLocal(key); nowRead = true; }
  else if (action == "unmark") { unmarkNotifReadLocal(key); nowRead = false; }
  else if (action == "toggle") {
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

// NOTE: Se quitó el endpoint /notifications_open_servo y toda la lógica para abrir la puerta.
// Esto elimina la posibilidad de abrir el servo desde la UI de Notificaciones (según tu petición).
