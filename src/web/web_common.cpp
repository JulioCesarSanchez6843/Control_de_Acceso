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

  /* Hero Section */
  .hero-card {
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    padding: 40px 20px;
    margin: 0;
    border-radius: 0;
  }
  
  .hero-content {
    max-width: 1200px;
    margin: 0 auto;
  }
  
  .hero-card h1 {
    font-size: 2.5em;
    margin-bottom: 20px;
    text-align: center;
    font-weight: 800;
  }
  
  .hero-description {
    font-size: 1.2em;
    text-align: center;
    margin-bottom: 40px;
    opacity: 0.9;
    line-height: 1.6;
  }
  
  /* Grid de Modos */
  .modes-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(400px, 1fr));
    gap: 30px;
    margin-top: 30px;
  }
  
  .mode-card {
    background: rgba(255, 255, 255, 0.1);
    backdrop-filter: blur(10px);
    padding: 30px;
    border-radius: 20px;
    border: 1px solid rgba(255, 255, 255, 0.2);
    transition: transform 0.3s ease, box-shadow 0.3s ease;
  }
  
  .mode-card:hover {
    transform: translateY(-5px);
    box-shadow: 0 15px 30px rgba(0, 0, 0, 0.2);
  }
  
  .mode-icon {
    font-size: 3em;
    margin-bottom: 15px;
    display: block;
  }
  
  .mode-card h3 {
    font-size: 1.5em;
    margin-bottom: 15px;
    color: white;
  }
  
  .mode-card p {
    margin-bottom: 20px;
    line-height: 1.6;
    opacity: 0.9;
  }
  
  .mode-features {
    list-style: none;
    padding: 0;
    margin-bottom: 25px;
  }
  
  .mode-features li {
    padding: 8px 0;
    border-bottom: 1px solid rgba(255, 255, 255, 0.1);
  }
  
  /* Botón con efecto pulse */
  .btn-pulse {
    background: #ff6b6b;
    animation: pulse 2s infinite;
  }
  
  @keyframes pulse {
    0% { transform: scale(1); }
    50% { transform: scale(1.05); }
    100% { transform: scale(1); }
  }
  
  /* Sección de Módulos */
  .modules-section {
    padding: 60px 20px;
    background: #f8fafc;
  }
  
  .section-title {
    text-align: center;
    font-size: 2.2em;
    margin-bottom: 15px;
    color: #0f172a;
  }
  
  .section-subtitle {
    text-align: center;
    font-size: 1.1em;
    color: #64748b;
    margin-bottom: 50px;
    max-width: 600px;
    margin-left: auto;
    margin-right: auto;
  }
  
  .modules-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(320px, 1fr));
    gap: 25px;
    max-width: 1200px;
    margin: 0 auto;
  }
  
  .module-card {
    background: white;
    padding: 25px;
    border-radius: 16px;
    box-shadow: 0 5px 20px rgba(0, 0, 0, 0.08);
    transition: all 0.3s ease;
    border: 1px solid #e2e8f0;
  }
  
  .module-card:hover {
    transform: translateY(-8px);
    box-shadow: 0 15px 40px rgba(0, 0, 0, 0.15);
  }
  
  .module-header {
    display: flex;
    align-items: center;
    margin-bottom: 15px;
  }
  
  .module-icon {
    font-size: 2em;
    margin-right: 15px;
  }
  
  .module-card h3 {
    color: #0f172a;
    margin: 0;
    font-size: 1.3em;
  }
  
  .module-card p {
    color: #64748b;
    line-height: 1.6;
    margin-bottom: 20px;
  }
  
  .module-features {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 20px;
  }
  
  .feature-tag {
    background: #e2e8f0;
    color: #475569;
    padding: 4px 12px;
    border-radius: 20px;
    font-size: 0.8em;
    font-weight: 600;
  }
  
  .btn-module {
    display: inline-block;
    width: 100%;
    text-align: center;
    padding: 12px;
    background: linear-gradient(90deg, #0ea5e9, #0369a1);
    color: white;
    text-decoration: none;
    border-radius: 10px;
    font-weight: 600;
    transition: all 0.3s ease;
  }
  
  .btn-module:hover {
    background: linear-gradient(90deg, #0369a1, #0ea5e9);
    transform: translateY(-2px);
  }
  
  /* Responsive */
  @media (max-width: 800px) {
    .nav { flex-wrap: wrap; }
    .grid { grid-template-columns: 1fr; }
    footer { height: 64px; }
    .page-content { padding-bottom: 96px; }
    
    .hero-card h1 {
      font-size: 2em;
    }
    
    .modes-grid {
      grid-template-columns: 1fr;
    }
    
    .modules-grid {
      grid-template-columns: 1fr;
    }
    
    .mode-card, .module-card {
      padding: 20px;
    }
  }

  @media (max-width: 480px) {
    .topbar {
      flex-direction: column;
      gap: 12px;
    }
    
    .nav {
      justify-content: center;
    }
    
    .hero-card h1 {
      font-size: 1.8em;
    }
    
    .modes-grid {
      grid-template-columns: 1fr;
    }
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

// ==================== PÁGINA PRINCIPAL (INICIO) - MEJORADA ====================
void handleRoot() {
  captureMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  String html = htmlHeader("Inicio - Sistema de Control de Acceso");

  // Hero Section - Explicación principal del sistema
  html += "<section class='hero-card'>";
  html += "<div class='hero-content'>";
  html += "<h1>🚀 Sistema de Control de Acceso Inteligente</h1>";
  html += "<p class='hero-description'>Sistema automatizado para la gestión y control de acceso al laboratorio mediante tecnología RFID, diseñado para garantizar seguridad y eficiencia en el acceso de estudiantes y personal académico.</p>";
  
  // Tarjetas de modos del sistema
  html += "<div class='modes-grid'>";
  
  // Modo Captura
  html += "<div class='mode-card capture-mode'>";
  html += "<div class='mode-icon'>🎴</div>";
  html += "<h3>Modo Captura</h3>";
  html += "<p>Registro y vinculación de nuevas tarjetas RFID con usuarios del sistema. Permite agregar estudiantes y maestros de forma rápida y segura.</p>";
  html += "<ul class='mode-features'>";
  html += "<li>🔍 Detección automática de tarjetas</li>";
  html += "<li>👤 Vinculación con datos personales</li>";
  html += "<li>📝 Asignación a materias</li>";
  html += "</ul>";
  html += "<a class='btn btn-pulse' href='/capture'>Iniciar Captura</a>";
  html += "</div>";
  
  // Modo Verificación
  html += "<div class='mode-card verify-mode'>";
  html += "<div class='mode-icon'>✅</div>";
  html += "<h3>Modo Verificación</h3>";
  html += "<p>Sistema automático que valida el acceso según horarios establecidos, verificando permisos y registrando cada entrada al laboratorio.</p>";
  html += "<ul class='mode-features'>";
  html += "<li>⏰ Validación por horarios</li>";
  html += "<li>📊 Control de asistencia</li>";
  html += "<li>🚨 Notificaciones en tiempo real</li>";
  html += "</ul>";
  html += "<a class='btn btn-green' href='/schedules'>Configurar Horarios</a>";
  html += "</div>";
  
  html += "</div>"; // cierre modes-grid
  html += "</div>"; // cierre hero-content
  html += "</section>";

  // Sección de Módulos del Sistema
  html += "<section class='modules-section'>";
  html += "<h2 class='section-title'>📦 Módulos del Sistema</h2>";
  html += "<p class='section-subtitle'>Explora todas las funcionalidades disponibles para la gestión completa del laboratorio</p>";
  
  html += "<div class='modules-grid'>";
  
  // Módulo Materias
  html += "<div class='module-card'>";
  html += "<div class='module-header'>";
  html += "<span class='module-icon'>📚</span>";
  html += "<h3>Gestión de Materias</h3>";
  html += "</div>";
  html += "<p>Administra el catálogo completo de materias, asigna horarios específicos y controla la matrícula de estudiantes en cada una.</p>";
  html += "<div class='module-features'>";
  html += "<span class='feature-tag'>Horarios</span>";
  html += "<span class='feature-tag'>Matrículas</span>";
  html += "<span class='feature-tag'>Aulas</span>";
  html += "</div>";
  html += "<a class='btn-module' href='/materias'>Administrar Materias</a>";
  html += "</div>";
  
  // Módulo Estudiantes
  html += "<div class='module-card'>";
  html += "<div class='module-header'>";
  html += "<span class='module-icon'>🧑‍🎓</span>";
  html += "<h3>Gestión de Estudiantes</h3>";
  html += "</div>";
  html += "<p>Gestiona el registro de estudiantes, asigna tarjetas RFID y vincula cada alumno con sus materias correspondientes.</p>";
  html += "<div class='module-features'>";
  html += "<span class='feature-tag'>RFID</span>";
  html += "<span class='feature-tag'>Matrículas</span>";
  html += "<span class='feature-tag'>Estadísticas</span>";
  html += "</div>";
  html += "<a class='btn-module' href='/students_all'>Ver Estudiantes</a>";
  html += "</div>";
  
  // Módulo Maestros
  html += "<div class='module-card'>";
  html += "<div class='module-header'>";
  html += "<span class='module-icon'>👩‍🏫</span>";
  html += "<h3>Gestión de Maestros</h3>";
  html += "</div>";
  html += "<p>Administra el personal docente, gestiona permisos de acceso y asigna responsabilidades sobre las materias.</p>";
  html += "<div class='module-features'>";
  html += "<span class='feature-tag'>Permisos</span>";
  html += "<span class='feature-tag'>Asignaciones</span>";
  html += "<span class='feature-tag'>Accesos</span>";
  html += "</div>";
  html += "<a class='btn-module' href='/teachers_all'>Ver Maestros</a>";
  html += "</div>";
  
  // Módulo Historial
  html += "<div class='module-card'>";
  html += "<div class='module-header'>";
  html += "<span class='module-icon'>📜</span>";
  html += "<h3>Historial de Accesos</h3>";
  html += "</div>";
  html += "<p>Consulta el registro completo de entradas al laboratorio, genera reportes y analiza patrones de asistencia.</p>";
  html += "<div class='module-features'>";
  html += "<span class='feature-tag'>Reportes</span>";
  html += "<span class='feature-tag'>Estadísticas</span>";
  html += "<span class='feature-tag'>Exportar</span>";
  html += "</div>";
  html += "<a class='btn-module' href='/history'>Ver Historial</a>";
  html += "</div>";
  
  // Módulo Sistema
  html += "<div class='module-card'>";
  html += "<div class='module-header'>";
  html += "<span class='module-icon'>🔧</span>";
  html += "<h3>Estado del Sistema</h3>";
  html += "</div>";
  html += "<p>Monitorea el estado del dispositivo, recursos del sistema y configuración general del equipo.</p>";
  html += "<div class='module-features'>";
  html += "<span class='feature-tag'>Monitorización</span>";
  html += "<span class='feature-tag'>Diagnóstico</span>";
  html += "<span class='feature-tag'>Configuración</span>";
  html += "</div>";
  html += "<a class='btn-module' href='/status'>Ver Estado</a>";
  html += "</div>";
  
  html += "</div>"; // cierre modules-grid
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