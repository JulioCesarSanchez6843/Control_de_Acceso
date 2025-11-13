#include <Arduino.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>

#include "web_common.h"
#include "globals.h"
#include "config.h"

// HTML header / footer (copiado y usado por módulos web)
String htmlHeader(const char* title) {
  int nCount = notifCount();
  String h = "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>"; h += title; h += "</title>";

  h += R"rawliteral(
    <style>
    body{font-family:Inter,Arial,Helvetica,sans-serif;background:linear-gradient(180deg,#f8fafc,#eef2ff);color:#111;margin:0;padding:0}
    .container{min-height:100vh;display:flex;flex-direction:column}
    .topbar{background:linear-gradient(90deg,#0f172a,#0369a1);color:#fff;padding:12px 16px;display:flex;align-items:center;justify-content:space-between;box-shadow:0 6px 18px rgba(2,6,23,0.15)}
    .title{font-weight:900;font-size:20px;cursor:pointer}
    .nav{display:flex;gap:8px;align-items:center}
    .btn{display:inline-block;padding:8px 12px;border-radius:8px;text-decoration:none;font-weight:700;border:none;cursor:pointer;color:#fff}
    .btn-green{background:#10b981;color:#fff}
    .btn-red{background:#ef4444;color:#fff}
    .btn-blue{background:#06b6d4;color:#05345b}
    .card{background:#fff;padding:18px;border-radius:12px;box-shadow:0 8px 30px rgba(2,6,23,0.06);margin:16px}
    .grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:16px;padding:16px}
    .icon{font-size:28px;display:block}
    table{width:100%;border-collapse:collapse;margin-top:8px}
    th,td{padding:8px;border:1px solid #e6eef6;text-align:center}
    th{background:#0ea5b7;color:#fff}
    .small{font-size:13px;color:#475569}
    .slot-add{background:#10b981;color:#fff;padding:6px 8px;border-radius:6px;text-decoration:none}
    .slot-occupied{background:#ef4444;color:#fff;padding:6px 8px;border-radius:6px}
    .notif{position:relative;display:inline-block;padding:6px 8px;border-radius:8px;background:#fff;color:#05345b;font-weight:700;margin-right:12px}
    .notif .count{position:absolute;top:-6px;right:-6px;background:#ef4444;color:#fff;border-radius:50%;padding:2px 6px;font-size:12px}
    @media (max-width:800px){ .nav{flex-wrap:wrap} .grid{grid-template-columns:1fr} }
    .filters{display:flex;gap:8px;margin-bottom:10px;flex-wrap:wrap}
    .filters input,.filters select{padding:6px;border-radius:6px;border:1px solid #dbeafe}
    .search-btn{padding:6px 10px;border-radius:6px;border:none;cursor:pointer}
    pre{margin:0;font-family:monospace}
    </style>
  )rawliteral";

  h += "</head><body>";
  h += "<div class='container'>";
  h += "<div class='topbar'><div style='display:flex;gap:12px;align-items:center'><div class='title' onclick='location.href=\"/\"'>Control de Acceso - Laboratorio</div>";
  h += "<a class='notif' href='/notifications' title='Notificaciones'>🔔";
  if (nCount>0) h += "<span class='count'>" + String(nCount) + "</span>";
  h += "</a>";
  h += "</div>";
  h += "<div class='nav'>";
  h += "<a class='btn btn-blue' href='/capture'>🎴 Capturar</a>";
  h += "<a class='btn btn-blue' href='/schedules'>📅 Horarios</a>";
  h += "<a class='btn btn-blue' href='/schedules/edit'>✏️ Editar Horarios</a>";
  h += "<a class='btn btn-blue' href='/materias'>📚 Materias</a>";
  h += "<a class='btn btn-blue' href='/students_all'>🧑‍🎓 Alumnos</a>";
  h += "<a class='btn btn-blue' href='/history'>📜 Historial</a>";
  h += "<a class='btn btn-blue' href='/status'>🔧 Estado ESP</a>";
  h += "</div></div>";
  h += "<div style='flex:1;padding:12px'>"; // content wrapper
  return h;
}

String htmlFooter() {
  String f = "<div style='text-align:center;color:#7b8794;font-size:12px;margin:16px'>Proyecto - Registro Asistencia • ESP32</div></div></div></body></html>";
  return f;
}

// Minimal root and status handlers (usados por web_routes)
void handleRoot() {
  captureMode = false;
  captureUID = ""; captureName = ""; captureAccount = ""; captureDetectedAt = 0;

  String html = htmlHeader("Inicio");
  html += "<div class='grid' style='padding:20px'>";
  html += "<div class='card' style='display:flex;flex-direction:column;justify-content:space-between;min-height:160px'>";
  html += "<div><span class='icon'>🎛️</span><h2>Control del Laboratorio</h2><p class='small'>Gestiona accesos, materias y horarios desde la interfaz web.</p></div>";
  html += "<div style='display:flex;gap:8px;justify-content:flex-end'><a class='btn btn-green' href='/capture'>🎴 Capturar tarjeta</a> <a class='btn btn-blue' href='/schedules'>📅 Ver Horarios</a></div>";
  html += "</div>";

  html += "<div class='card' style='min-height:160px'><div><span class='icon'>📚</span><h3>Materias</h3><p class='small'>Agregar o editar materias y ver sus horarios.</p></div><div style='display:flex;gap:8px;justify-content:flex-end'><a class='btn btn-green' href='/materias'>Ir a Materias</a></div></div>";

  html += "<div class='card' style='min-height:160px'><div><span class='icon'>🧑‍🎓</span><h3>Alumnos</h3><p class='small'>Administrar lista de alumnos y sus materias.</p></div><div style='display:flex;gap:8px;justify-content:flex-end'><a class='btn btn-green' href='/students_all'>Ver Alumnos</a></div></div>";

  html += "<div class='card' style='min-height:160px'><div><span class='icon'>📜</span><h3>Historial</h3><p class='small'>Ver historial de accesos y descargar CSV.</p></div><div style='display:flex;gap:8px;justify-content:flex-end'><a class='btn btn-blue' href='/history'>Ver Historial</a></div></div>";

  html += "</div>";
  html += htmlFooter();
  server.send(200,"text/html",html);
}

void handleStatus() {
  size_t total = SPIFFS.totalBytes();
  size_t used = SPIFFS.usedBytes();
  float pct = 0;
  if (total>0) pct = (used * 100.0f) / (float)total;
  String html = htmlHeader("Estado ESP32");
  html += "<div class='card'><h2>Estado del dispositivo</h2>";
  html += "<p><b>IP:</b> " + WiFi.localIP().toString() + "</p>";
  html += "<p><b>Up time:</b> " + String(millis()/1000) + " s</p>";
  html += "<p><b>RAM libre:</b> " + String(ESP.getFreeHeap()) + " bytes</p>";
  html += "<p><b>SPIFFS total:</b> " + String(total) + " bytes</p>";
  html += "<p><b>SPIFFS usados:</b> " + String(used) + " bytes (" + String(pct,1) + "%)</p>";
  File fu = SPIFFS.open(USERS_FILE, FILE_READ);
  int usersCount = 0;
  if (fu) {
    String head = fu.readStringUntil('\n');
    while (fu.available()) { String l = fu.readStringUntil('\n'); l.trim(); if (l.length()) usersCount++; }
    fu.close();
  }
  html += "<p><b>Usuarios registrados:</b> " + String(usersCount) + "</p>";
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/users.csv'>📥 Descargar usuarios</a> <a class='btn btn-blue' href='/'>Volver</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}
