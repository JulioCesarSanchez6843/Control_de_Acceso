// src/web/notifications.cpp
#include "notifications.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>
#include <algorithm> // std::reverse

// Detecta el tipo de notificación a partir del texto de la nota.
// Regresa: "Alerta (Denegado)", "Informativa", "Tarjeta desconocida" o "Otro"
static String detectNotificationType(const String &note) {
  String n = note;
  n.toLowerCase();

  if (n.indexOf("intento fuera de materia") >= 0 ||
      n.indexOf("fuera de materia") >= 0 ||
      n.indexOf("no pertenece") >= 0) {
    return String("Alerta (Denegado)");
  }

  if (n.indexOf("entrada fuera de horario") >= 0 ||
      n.indexOf("fuera de horario") >= 0 ||
      n.indexOf("no hay clase") >= 0) {
    return String("Informativa");
  }

  if (n.indexOf("no registrada") >= 0 ||
      n.indexOf("no registrado") >= 0 ||
      n.indexOf("tarjeta desconocida") >= 0 ||
      n.indexOf("tarjeta no registrada") >= 0) {
    return String("Tarjeta desconocida");
  }

  return String("Otro");
}

// /notifications (GET)
void handleNotificationsPage() {
  String html = htmlHeader("Notificaciones");
  html += "<div class='card'><h2>Notificaciones</h2>";
  html += "<div style='margin-bottom:8px'>"
          "<form method='POST' action='/notifications_clear' onsubmit='return confirm(\"Borrar todas las notificaciones? Esta acción es irreversible.\");' style='display:inline'>"
          "<input class='btn btn-red' type='submit' value='🗑️ Borrar Notificaciones'></form> "
          "<a class='btn btn-blue' href='/'>Inicio</a></div>";

  auto nots = readNotifications(200);

  // Mostrar más recientes arriba: invertimos el vector
  if (nots.size() > 1) std::reverse(nots.begin(), nots.end());

  if (nots.size() == 0) {
    html += "<p>No hay notificaciones.</p>";
  } else {
    // cabecera con columna Tipo
    html += "<table><tr><th>Tipo</th><th>Timestamp</th><th>UID</th><th>Nombre</th><th>Cuenta</th><th>Nota</th></tr>";
    for (auto &ln : nots) {
      auto c = parseQuotedCSVLine(ln);
      String ts = (c.size() > 0 ? c[0] : "");
      String uid = (c.size() > 1 ? c[1] : "");
      String name = (c.size() > 2 ? c[2] : "");
      String acc = (c.size() > 3 ? c[3] : "");
      String note = (c.size() > 4 ? c[4] : "");

      // Determinar tipo
      String tipo = detectNotificationType(note);

      // Color por tipo (claro)
      String rowStyle = "";
      if (tipo == "Alerta (Denegado)") rowStyle = " style='background:#ffe6e6'";      // ligero rojo
      else if (tipo == "Informativa") rowStyle = " style='background:#fffde6'";       // ligero amarillo
      else if (tipo == "Tarjeta desconocida") rowStyle = " style='background:#e6f2ff'"; // ligero azul
      else rowStyle = "";

      html += "<tr" + rowStyle + ">";
      html += "<td>" + tipo + "</td>";
      html += "<td>" + ts + "</td>";
      html += "<td>" + uid + "</td>";
      html += "<td>" + name + "</td>";
      html += "<td>" + acc + "</td>";
      html += "<td>" + note + "</td>";
      html += "</tr>";
    }
    html += "</table>";
  }

  html += "</div>" + htmlFooter();
  server.send(200, "text/html", html);
}

void handleNotificationsClearPOST() {
  clearNotifications();
  server.sendHeader("Location", "/notifications");
  server.send(303, "text/plain", "Notificaciones borradas");
}
