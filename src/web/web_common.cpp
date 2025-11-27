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
  .btn-purple{ background:#8b5cf6; color:#fff; }
  .btn-orange{ background:#f59e0b; color:#fff; }

  .card { background:#fff; padding:22px; border-radius:16px; box-shadow:0 8px 30px rgba(2,6,23,0.08); margin:16px; }
  .grid { display:grid; grid-template-columns:repeat(auto-fit,minmax(280px,1fr)); gap:16px; padding:16px; }
  .icon { font-size:36px; display:block; margin-bottom:8px; }
  table { width:100%; border-collapse:collapse; margin-top:8px; }
  th, td { padding:8px; border:1px solid #e6eef6; text-align:center; }
  th { background:#0ea5b7; color:#fff; }
  .small { font-size:14px; color:#475569; }
  .notif { position:relative; display:inline-block; padding:6px 8px; border-radius:8px; background:#fff; color:#05345b; font-weight:700; margin-right:12px; }
  .notif .count { position:absolute; top:-6px; right:-6px; background:#ef4444; color:#fff; border-radius:50%; padding:2px 6px; font-size:12px; }

  /* FIXED footer */
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
    height: 56px;
  }

  .page-content {
    flex: 1 0 auto;
    padding: 12px;
    padding-bottom: 84px;
  }

  section h2 { margin-top: 0; color: #0369a1; }

  /* Nuevos estilos para la página de inicio mejorada */
  .hero-section {
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
    padding: 60px 20px;
    text-align: center;
    margin-bottom: 40px;
  }

  .hero-title {
    font-size: 3.5em;
    font-weight: 900;
    margin-bottom: 20px;
    text-shadow: 2px 2px 4px rgba(0,0,0,0.3);
    color: #ffffff;
  }

  .hero-subtitle {
    font-size: 1.3em;
    opacity: 0.95;
    max-width: 900px;
    margin: 0 auto;
    line-height: 1.6;
    font-weight: 500;
  }

  .section-container {
    max-width: 1200px;
    margin: 0 auto;
    padding: 0 20px;
  }

  .section-title {
    font-size: 2.5em;
    margin-bottom: 15px;
    color: #0f172a;
    border-bottom: 3px solid #0ea5e9;
    padding-bottom: 10px;
    font-weight: 800;
  }

  .section-description {
    font-size: 1.2em;
    color: #64748b;
    margin-bottom: 30px;
    line-height: 1.6;
  }

  .system-modes {
    background: white;
    padding: 30px;
    border-radius: 12px;
    box-shadow: 0 5px 20px rgba(0,0,0,0.08);
    margin-bottom: 40px;
  }

  .system-modes h3 {
    color: #0f172a;
    font-size: 1.8em;
    margin-bottom: 20px;
  }

  .mode-explanation {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    gap: 25px;
    margin-bottom: 25px;
  }

  .mode-item {
    padding: 20px;
    border-radius: 8px;
    border-left: 4px solid #10b981;
  }

  .mode-item.capture {
    background: white;
    border-left-color: #0ea5e9;
  }

  .mode-item.verification {
    background: white;
    border-left-color: #10b981;
  }

  .mode-item h4 {
    margin: 0 0 10px 0;
    color: #0f172a;
    font-size: 1.3em;
  }

  .mode-item p {
    margin: 0;
    color: #475569;
    line-height: 1.6;
  }

  .capture-modes {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(350px, 1fr));
    gap: 25px;
    margin-bottom: 50px;
  }

  .mode-group {
    background: white;
    padding: 25px;
    border-radius: 12px;
    box-shadow: 0 5px 20px rgba(0,0,0,0.08);
    border-left: 5px solid #0ea5e9;
  }

  .mode-group.teachers {
    border-left-color: #f59e0b;
  }

  .mode-group-title {
    font-size: 1.6em;
    margin-bottom: 15px;
    color: #0f172a;
  }

  .mode-description {
    color: #64748b;
    margin-bottom: 20px;
    line-height: 1.6;
  }

  .mode-options {
    display: flex;
    flex-direction: column;
    gap: 12px;
  }

  .mode-option {
    background: #f8fafc;
    padding: 15px;
    border-radius: 8px;
    border-left: 4px solid #10b981;
  }

  .mode-option h4 {
    margin: 0 0 8px 0;
    color: #0f172a;
    font-size: 1.1em;
  }

  .mode-option p {
    margin: 0 0 12px 0;
    color: #64748b;
    font-size: 0.95em;
    line-height: 1.5;
  }

  .btn-group {
    display: flex;
    gap: 10px;
    flex-wrap: wrap;
  }

  .btn-module {
    display: inline-block;
    padding: 10px 20px;
    background: linear-gradient(90deg, #0ea5e9, #0369a1);
    color: white;
    text-decoration: none;
    border-radius: 8px;
    font-weight: 600;
    transition: all 0.3s ease;
    border: none;
    cursor: pointer;
    font-size: 0.95em;
  }

  .btn-module:hover {
    background: linear-gradient(90deg, #0369a1, #0ea5e9);
    transform: translateY(-2px);
    box-shadow: 0 5px 15px rgba(14, 165, 233, 0.3);
  }

  .btn-module.green {
    background: linear-gradient(90deg, #10b981, #059669);
  }

  .btn-module.orange {
    background: linear-gradient(90deg, #f59e0b, #d97706);
  }

  .btn-module.purple {
    background: linear-gradient(90deg, #8b5cf6, #7c3aed);
  }

  .modules-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));
    gap: 25px;
    margin-top: 30px;
  }

  .module-card {
    background: white;
    padding: 25px;
    border-radius: 12px;
    box-shadow: 0 5px 20px rgba(0,0,0,0.08);
    transition: all 0.3s ease;
    border-top: 4px solid #0ea5e9;
  }

  .module-card.materias {
    border-top-color: #8b5cf6;
  }

  .module-card.estudiantes {
    border-top-color: #10b981;
  }

  .module-card.maestros {
    border-top-color: #f59e0b;
  }

  .module-card.horarios {
    border-top-color: #ef4444;
  }

  .module-card.historial {
    border-top-color: #06b6d4;
  }

  .module-card.sistema {
    border-top-color: #6366f1;
  }

  .module-card:hover {
    transform: translateY(-5px);
    box-shadow: 0 10px 30px rgba(0,0,0,0.12);
  }

  .module-card h3 {
    margin: 0 0 15px 0;
    color: #0f172a;
    font-size: 1.4em;
  }

  .module-card p {
    color: #64748b;
    line-height: 1.6;
    margin-bottom: 20px;
  }

  .feature-list {
    list-style: none;
    padding: 0;
    margin: 0 0 20px 0;
  }

  .feature-list li {
    padding: 5px 0;
    color: #475569;
    position: relative;
    padding-left: 20px;
  }

  .feature-list li::before {
    content: "✓";
    position: absolute;
    left: 0;
    color: #10b981;
    font-weight: bold;
  }

  .feature-tags {
    display: flex;
    flex-wrap: wrap;
    gap: 8px;
    margin-bottom: 15px;
  }

  .feature-tag {
    background: #e2e8f0;
    color: #475569;
    padding: 4px 12px;
    border-radius: 20px;
    font-size: 0.8em;
    font-weight: 600;
  }

  /* Responsive */
  @media (max-width: 800px) {
    .nav { flex-wrap: wrap; }
    .grid { grid-template-columns: 1fr; }
    footer { height: 64px; }
    .page-content { padding-bottom: 96px; }
    
    .hero-title {
      font-size: 2.5em;
    }
    
    .hero-subtitle {
      font-size: 1.1em;
    }
    
    .capture-modes {
      grid-template-columns: 1fr;
    }
    
    .modules-grid {
      grid-template-columns: 1fr;
    }
    
    .btn-group {
      flex-direction: column;
    }
    
    .section-title {
      font-size: 2em;
    }
    
    .mode-explanation {
      grid-template-columns: 1fr;
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
    
    .hero-title {
      font-size: 2em;
    }
    
    .mode-group,
    .module-card {
      padding: 20px;
    }
    
    .system-modes {
      padding: 20px;
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

  // Menú de navegación principal (se eliminó botón Capturar del nav)
  h += "<div class='nav'>";
  h += "<a class='btn btn-blue' href='/schedules'>Horarios</a>";
  h += "<a class='btn btn-blue' href='/materias'>Materias</a>";
  h += "<a class='btn btn-blue' href='/students_all'>Alumnos</a>";
  h += "<a class='btn btn-blue' href='/teachers_all'>Maestros</a>";
  h += "<a class='btn btn-blue' href='/history'>Historial</a>";
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

  // Cambié el título de la página para que coincida con encabezado
  String html = htmlHeader("Inicio - Control de Acceso - Laboratorio");

  // Hero Section (título alineado a encabezado)
  html += "<section class='hero-section'>";
  html += "<h1 class='hero-title'>Control de Acceso - Laboratorio</h1>";
  html += "<p class='hero-subtitle'>Sistema automatizado de gestión y control de acceso para laboratorios mediante tecnología RFID. Garantiza seguridad, control de asistencia y gestión eficiente de estudiantes y personal académico mediante un control preciso basado en horarios y permisos específicos.</p>";
  html += "</section>";

  // Sección de Modos de Operación del Sistema
  html += "<div class='section-container'>";
  html += "<div class='system-modes'>";
  html += "<h3>Modos de Operación del Sistema</h3>";
  html += "<p>El sistema opera en dos modos principales que trabajan de forma integrada para garantizar la seguridad y eficiencia del laboratorio:</p>";
  
  html += "<div class='mode-explanation'>";
  
  // Modo Captura
  html += "<div class='mode-item capture'>";
  html += "<h4>Modo Captura</h4>";
  html += "<p>Dedicado al registro y gestión de usuarios en el sistema. Permite la captura individual de estudiantes y maestros, así como la captura masiva por lotes mediante códigos QR. En este modo se configuran las tarjetas RFID, se asignan permisos y se vinculan los usuarios con sus materias correspondientes.</p>";
  html += "</div>";
  
  // Modo Verificación
  html += "<div class='mode-item verification'>";
  html += "<h4>Modo Verificación</h4>";
  html += "<p>Gestiona el control de acceso en tiempo real según los horarios establecidos. Verifica automáticamente los permisos de cada usuario, valida si está dentro de su horario autorizado y registra cada acceso al laboratorio. Opera de forma continua para garantizar la seguridad del espacio.</p>";
  html += "</div>";
  
  html += "</div>"; // cierre mode-explanation
  
  html += "</div>"; // cierre system-modes

  // Sección de Modos de Captura
  html += "<h2 class='section-title'>Sistema de Captura de Usuarios</h2>";
  html += "<p class='section-description'>El sistema cuenta con métodos de captura especializados para cada tipo de usuario, garantizando un registro seguro y eficiente de toda la comunidad académica.</p>";
  
  html += "<div class='capture-modes'>";
  
  // Captura para Estudiantes
  html += "<div class='mode-group'>";
  html += "<h3 class='mode-group-title'>Captura de Estudiantes</h3>";
  html += "<p class='mode-description'>Sistema especializado para el registro de estudiantes con dos modalidades de captura adaptadas a diferentes necesidades.</p>";
  
  html += "<div class='mode-options'>";
  
  html += "<div class='mode-option'>";
  html += "<h4>Captura Individual</h4>";
  html += "<p>Registro personalizado de estudiantes uno por uno. Ideal para altas individuales, reemplazo de tarjetas o corrección de datos específicos.</p>";
  html += "<div class='feature-tags'>";
  html += "<span class='feature-tag'>Registro personalizado</span>";
  html += "<span class='feature-tag'>Validación inmediata</span>";
  html += "<span class='feature-tag'>Asignación específica</span>";
  html += "</div>";
  html += "<div class='btn-group'>";
  html += "<a class='btn-module green' href='/capture_individual'>Iniciar Captura Individual</a>";
  html += "</div>";
  html += "</div>";
  
  html += "<div class='mode-option'>";
  html += "<h4>Captura por Lote con QR</h4>";
  html += "<p>Sistema masivo para registrar grupos completos de estudiantes. Genera códigos QR para auto-registro, perfecto para inicio de semestre o grupos grandes.</p>";
  html += "<div class='feature-tags'>";
  html += "<span class='feature-tag'>Registro masivo</span>";
  html += "<span class='feature-tag'>Auto-registro QR</span>";
  html += "<span class='feature-tag'>Gestión de grupos</span>";
  html += "</div>";
  html += "<div class='btn-group'>";
  html += "<a class='btn-module purple' href='/capture_batch'>Iniciar Captura por Lote</a>";
  html += "</div>";
  html += "</div>";
  
  html += "</div>"; // cierre mode-options
  
  html += "</div>"; // cierre mode-group estudiantes
  
  // Captura para Maestros
  html += "<div class='mode-group teachers'>";
  html += "<h3 class='mode-group-title'>Captura de Maestros</h3>";
  html += "<p class='mode-description'>Sistema especializado para el registro del personal docente con permisos extendidos y gestión de materias asignadas.</p>";
  
  html += "<div class='mode-options'>";
  
  html += "<div class='mode-option'>";
  html += "<h4>Captura Individual de Maestros</h4>";
  html += "<p>Registro personalizado de maestros con asignación de materias y permisos especiales. Control total sobre el acceso al laboratorio y gestión de horarios flexibles.</p>";
  html += "<div class='feature-tags'>";
  html += "<span class='feature-tag'>Permisos extendidos</span>";
  html += "<span class='feature-tag'>Asignación de materias</span>";
  html += "<span class='feature-tag'>Horarios flexibles</span>";
  html += "</div>";
  html += "<div class='btn-group'>";
  html += "<a class='btn-module orange' href='/teachers_all'>Gestionar Maestros</a>";
  html += "</div>";
  html += "</div>";
  
  html += "</div>"; // cierre mode-options
  
  html += "</div>"; // cierre mode-group maestros
  
  html += "</div>"; // cierre capture-modes

  // Sección de Módulos del Sistema
  html += "<h2 class='section-title'>Módulos del Sistema de Gestión</h2>";
  html += "<p class='section-description'>Sistema integral que cubre todos los aspectos necesarios para la administración eficiente del acceso al laboratorio y la gestión académica.</p>";
  
  html += "<div class='modules-grid'>";
  
  // Módulo Materias
  html += "<div class='module-card materias'>";
  html += "<h3>Gestión de Materias</h3>";
  html += "<p>Administra el catálogo completo de materias del laboratorio con control total sobre horarios, asignación de estudiantes y profesores.</p>";
  html += "<ul class='feature-list'>";
  html += "<li>Creación y edición de materias</li>";
  html += "<li>Asignación de horarios específicos</li>";
  html += "<li>Gestión de matrículas estudiantiles</li>";
  html += "<li>Control de aforo y capacidad</li>";
  html += "<li>Asignación de profesores responsables</li>";
  html += "</ul>";
  html += "<a class='btn-module' href='/materias'>Administrar Materias</a>";
  html += "</div>";
  
  // Módulo Estudiantes
  html += "<div class='module-card estudiantes'>";
  html += "<h3>Gestión de Estudiantes</h3>";
  html += "<p>Control completo del registro estudiantil con vinculación de tarjetas RFID, asignación a materias y seguimiento de asistencia.</p>";
  html += "<ul class='feature-list'>";
  html += "<li>Registro y edición de estudiantes</li>";
  html += "<li>Vincular tarjetas RFID</li>";
  html += "<li>Asignación a múltiples materias</li>";
  html += "<li>Control de permisos y acceso</li>";
  html += "<li>Estadísticas de asistencia</li>";
  html += "</ul>";
  html += "<a class='btn-module' href='/students_all'>Gestionar Estudiantes</a>";
  html += "</div>";
  
  // Módulo Maestros
  html += "<div class='module-card maestros'>";
  html += "<h3>Gestión de Maestros</h3>";
  html += "<p>Administración del personal docente con permisos especiales, asignación de materias y control de acceso privilegiado.</p>";
  html += "<ul class='feature-list'>";
  html += "<li>Registro de personal docente</li>";
  html += "<li>Permisos de acceso extendidos</li>";
  html += "<li>Asignación a materias específicas</li>";
  html += "<li>Horarios flexibles</li>";
  html += "<li>Gestión de supervisiones</li>";
  html += "</ul>";
  html += "<a class='btn-module' href='/teachers_all'>Gestionar Maestros</a>";
  html += "</div>";
  
  // Módulo Horarios
  html += "<div class='module-card horarios'>";
  html += "<h3>Gestión de Horarios</h3>";
  html += "<p>Configuración detallada de horarios de acceso al laboratorio por materia, con control de conflictos y optimización de espacios.</p>";
  html += "<ul class='feature-list'>";
  html += "<li>Definición de franjas horarias</li>";
  html += "<li>Asignación por materia</li>";
  html += "<li>Detección de conflictos</li>";
  html += "<li>Calendario académico</li>";
  html += "<li>Gestión de excepciones</li>";
  html += "</ul>";
  html += "<a class='btn-module' href='/schedules'>Gestionar Horarios</a>";
  html += "</div>";
  
  // Módulo Historial
  html += "<div class='module-card historial'>";
  html += "<h3>Historial de Accesos</h3>";
  html += "<p>Registro completo y detallado de todos los accesos al laboratorio con herramientas de análisis y exportación de datos.</p>";
  html += "<ul class='feature-list'>";
  html += "<li>Registro temporal de accesos</li>";
  html += "<li>Filtrado por usuario o materia</li>";
  html += "<li>Exportación a formato CSV</li>";
  html += "<li>Estadísticas de uso</li>";
  html += "<li>Reportes de asistencia</li>";
  html += "</ul>";
  html += "<a class='btn-module' href='/history'>Consultar Historial</a>";
  html += "</div>";
  
  // Módulo Notificaciones (ahora con misma estructura que los demás)
  html += "<div class='module-card sistema'>";
  html += "<h3>Notificaciones</h3>";
  html += "<p>Revise y gestione las notificaciones generadas por el sistema (alertas de acceso, tarjetas desconocidas, entradas fuera de horario, etc.).</p>";
  html += "<ul class='feature-list'>";
  html += "<li>Notificaciones por intentos denegados (fuera de materia / fuera de horario)</li>";
  html += "<li>Alertas de tarjetas desconocidas</li>";
  html += "<li>Informes de entradas fuera de horario</li>";
  html += "<li>Historial y exportación</li>";
  html += "<li>Gestión y borrado de notificaciones</li>";
  html += "</ul>";
  html += "<p class='small'>Notificaciones pendientes: " + String(notifCount()) + "</p>";
  html += "<a class='btn-module' href='/notifications'>Ver Notificaciones</a>";
  html += "</div>";
  
  html += "</div>"; // cierre modules-grid
  
  html += "</div>"; // cierre section-container

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
  html += "<div class='card'><h2>Estado del Dispositivo</h2>";
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
  html += "<div style='margin-top:10px'><a class='btn btn-blue' href='/users.csv'>Descargar Usuarios</a> "
          "<a class='btn btn-blue' href='/'>Volver</a></div></div>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}
