#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>

#include "web_common.h"
#include "globals.h"
#include "config.h"

// ==================== CABECERA HTML GLOBAL ====================
// Construye la cabecera HTML común.
String htmlHeader(const char* title) {
  int nCount = notifCount();
  String h = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>"; h += title; h += "</title>";

  h += R"rawliteral(
  <style>
  /* Make sure html/body fill viewport */
  html, body {
    height: 100%;
    margin: 0;
    padding: 0;
  }

  /* Global body */
  body {
    font-family:'Inter',Arial,Helvetica,sans-serif;
    background: linear-gradient(180deg,#f8fafc,#eef2ff);
    color: #111;
    display: flex;
    flex-direction: column;
    min-height: 100vh;
  }

  /* Container holds header, content and footer */
  .container {
    display: flex;
    flex-direction: column;
    min-height: 100vh;
    /* no spacing tricks here — footer is fixed so we ensure content has bottom padding */
  }

  .topbar {
    background: linear-gradient(90deg,#0f172a,#0369a1);
    color: #fff;
    padding: 12px 16px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    box-shadow: 0 6px 18px rgba(2,6,23,0.15);
  }
  .title { font-weight:900; font-size:20px; cursor:pointer; }
  .nav { display:flex; gap:8px; align-items:center; flex-wrap:wrap; }

  .btn { display:inline-block; padding:8px 12px; border-radius:8px; text-decoration:none; font-weight:700; border:none; cursor:pointer; color:#fff; }
  .btn-green{ background:#10b981; color:#fff; }
  .btn-red{ background:#ef4444; color:#fff; }
  .btn-blue{ background:#06b6d4; color:#05345b; }

  .card { background:#fff; padding:22px; border-radius:16px; box-shadow:0 8px 30px rgba(2,6,23,0.08); margin:16px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(280px,1fr)); gap:16px; padding:16px; }
  .icon { font-size:36px; display:block; margin-bottom:8px; }
  table { width:100%; border-collapse:collapse; margin-top:8px; }
  th, td { padding:8px; border:1px solid #e6eef6; text-align:center; }
  th { background:#0ea5b7; color:#fff; }
  .small { font-size:14px; color:#475569; }
  .notif { position:relative; display:inline-block; padding:6px 8px; border-radius:8px; background:#fff; color:#05345b; font-weight:700; margin-right:12px; }
  .notif .count { position:absolute; top:-6px; right:-6px; background:#ef4444; color:#fff; border-radius:50%; padding:2px 6px; font-size:12px; }

  /* FIXED footer: always visible at bottom of viewport.
     To avoid overlapping content we add bottom padding to the content wrapper (.page-content). */
  footer {
    position: fixed;
    left: 0;
    right: 0;
    bottom: 0;
    background: linear-gradient(90deg,#0f172a,#0369a1);
    color: #fff;
    text-align: center;
    padding: 12px 8px;
    font-size: 14px;
    font-weight: 600;
    z-index: 999;
    box-shadow: 0 -4px 12px rgba(2,6,23,0.12);
    height: 56px; /* fixed footer height for layout calculations */
  }

  /* content wrapper — add bottom padding equal (or slightly larger) than footer height
     so that page content never hides beneath fixed footer. */
  .page-content {
    flex: 1 0 auto;
    padding: 12px;
    padding-bottom: 84px; /* footer height (56px) + margin (28px) to be safe on small screens */
  }

  section h2 { margin-top: 0; color: #0369a1; }

  @media (max-width:800px) {
    .nav { flex-wrap: wrap; }
    .grid { grid-template-columns: 1fr; }
    footer { height: 64px; }
    .page-content { padding-bottom: 96px; }
  }
  </style>
  )rawliteral";

  h += "</head><body>";
  h += "<div class='container'>";

  // Barra superior con título y notificaciones
  h += "<div class='topbar'><div style='display:flex;gap:12px;align-items:center'>";
  h += "<div class='title' onclick='location.href=\"/\"'>Control de Acceso - Laboratorio</div>";
  h += "<a class='notif' href='/notifications' title='Notificaciones'>🔔";
  if (nCount>0) h += "<span class='count'>" + String(nCount) + "</span>";
  h += "</a></div>";

  // Menú de navegación principal (añadido botón Maestros)
  h += "<div class='nav'>";
  h += "<a class='btn btn-blue' href='/capture'>🎴 Capturar</a>";
  h += "<a class='btn btn-blue' href='/schedules'>📅 Horarios</a>";
  h += "<a class='btn btn-blue' href='/materias'>📚 Materias</a>";
  h += "<a class='btn btn-blue' href='/students_all'>🧑‍🎓 Alumnos</a>";
  h += "<a class='btn btn-blue' href='/teachers_all'>👩‍🏫 Maestros</a>"; // <-- NUEVO botón para Maestros
  h += "<a class='btn btn-blue' href='/history'>📜 Historial</a>";
  h += "<a class='btn btn-blue' href='/status'>🔧 Estado ESP</a>";
  h += "</div></div>";

  // Contenedor principal (contenido de la página)
  h += "<div class='page-content'>";

  return h;
}

// ==================== PIE DE PÁGINA CON AUTORES ====================
// Construye el footer fijo con autores del proyecto.
String htmlFooter() {
  String f = "</div>";
  f += "<footer>Proyecto desarrollado por: Kevin González Gutiérrez • Julio César Sánchez Méndez • Dylan Adayr de la Rosa Ramos</footer>";
  f += "</div></body></html>";
  return f;
}

// ==================== PÁGINA PRINCIPAL (INICIO) ====================
// Handler para la ruta raíz: muestra panel principal con accesos rápidos.
void handleRoot() {
  captureMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  String html = htmlHeader("Inicio");

  html += "<section class='card'>";
  html += "<h2>🎛️ Control del Laboratorio</h2>";
  html += "<p class='small'>Este sistema permite gestionar el acceso de los estudiantes al laboratorio mediante tarjetas RFID. "
          "Cada usuario tiene una tarjeta registrada y los horarios definidos por materia, lo que garantiza un control preciso y automatizado.</p>";
  html += "<div style='display:flex;gap:8px;justify-content:flex-end;margin-top:12px'>"
          "<a class='btn btn-green' href='/capture'>🎴 Capturar Tarjeta</a>"
          "<a class='btn btn-blue' href='/schedules'>📅 Ver Horarios</a></div>";
  html += "</section>";

  // Paneles con accesos a secciones principales
  html += "<section class='grid'>";
  html += "<div class='card'><span class='icon'>📚</span><h3>Gestión de Materias</h3>"
          "<p class='small'>Administra las materias del laboratorio, asigna horarios y controla qué alumnos pertenecen a cada una.</p>"
          "<a class='btn btn-green' href='/materias'>Ir a Materias</a></div>";

  html += "<div class='card'><span class='icon'>🧑‍🎓</span><h3>Gestión de Alumnos</h3>"
          "<p class='small'>Consulta la lista de alumnos registrados, asigna sus tarjetas RFID y vincúlalos con sus materias correspondientes.</p>"
          "<a class='btn btn-green' href='/students_all'>Ver Alumnos</a></div>";

  html += "<div class='card'><span class='icon'>👩‍🏫</span><h3>Gestión de Maestros</h3>"
          "<p class='small'>Administra los maestros del laboratorio: revisa su lista, edítalos o elimínalos fácilmente.</p>"
          "<a class='btn btn-green' href='/teachers_all'>Ver Maestros</a></div>";

  html += "<div class='card'><span class='icon'>📜</span><h3>Historial de Accesos</h3>"
          "<p class='small'>Consulta el historial completo de entradas al laboratorio, exporta registros en formato CSV y analiza la asistencia.</p>"
          "<a class='btn btn-blue' href='/history'>Ver Historial</a></div>";

  html += "<div class='card'><span class='icon'>🔧</span><h3>Estado del Sistema</h3>"
          "<p class='small'>Visualiza el estado actual del ESP32, la memoria utilizada, la IP asignada y los usuarios registrados.</p>"
          "<a class='btn btn-blue' href='/status'>Ver Estado</a></div>";

  html += "</section>";

  html += htmlFooter();
  server.send(200,"text/html",html);
}

// ==================== ESTADO DEL DISPOSITIVO ====================
// Muestra información básica del ESP32: IP, uptime, memoria y conteo de usuarios.
void handleStatus() {
  size_t total = SPIFFS.totalBytes();
  size_t used = SPIFFS.usedBytes();
  float pct = (total>0) ? (used * 100.0f) / (float)total : 0;

  String html = htmlHeader("Estado ESP32");
  html += "<div class='card'><h2>🔧 Estado del Dispositivo</h2>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>Tiempo activo:</b> " + String(millis()/1000) + " segundos</p>";
  html += "<p><b>Memoria RAM libre:</b> " + String(ESP.getFreeHeap()) + " bytes</p>";
  html += "<p><b>Almacenamiento SPIFFS:</b> " + String(used) + " / " + String(total) + " bytes (" + String(pct,1) + "%)</p>";

  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  int usersCount = 0;
  if (fu) {
    fu.readStringUntil('\n');
    while (fu.available()) { String l = fu.readStringUntil('\n'); l.trim(); if (l.length()) usersCount++; }
    fu.close();
  }
  html += "<p><b>Usuarios registrados:</b> " + String(usersCount) + "</p>";
  html += "<div style='margin-top:10px'><a class='btn btn-blue' href='/users.csv'>📥 Descargar Usuarios</a> "
          "<a class='btn btn-blue' href='/'>Volver</a></div></div>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}
