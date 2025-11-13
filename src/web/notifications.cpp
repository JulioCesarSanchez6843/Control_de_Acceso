#include "notifications.h"
#include "web_common.h"
#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include <SPIFFS.h>

// /notifications (GET)
void handleNotificationsPage() {
  String html = htmlHeader("Notificaciones");
  html += "<div class='card'><h2>Notificaciones</h2>";
  html += "<div style='margin-bottom:8px'><form method='POST' action='/notifications_clear' onsubmit='return confirm(\"Borrar todas las notificaciones? Esta acción es irreversible.\");' style='display:inline'><input class='btn btn-red' type='submit' value='🗑️ Borrar Notificaciones'></form> <a class='btn btn-blue' href='/'>Volver</a></div>";
  auto nots = readNotifications(200);
  if (nots.size()==0) html += "<p>No hay notificaciones.</p>";
  else {
    html += "<table><tr><th>Timestamp</th><th>UID</th><th>Nombre</th><th>Cuenta</th><th>Nota</th></tr>";
    for (auto &ln : nots) {
      auto c = parseQuotedCSVLine(ln);
      String ts = (c.size()>0?c[0]:"");
      String uid = (c.size()>1?c[1]:"");
      String name = (c.size()>2?c[2]:"");
      String acc = (c.size()>3?c[3]:"");
      String note = (c.size()>4?c[4]:"");
      html += "<tr><td>" + ts + "</td><td>" + uid + "</td><td>" + name + "</td><td>" + acc + "</td><td>" + note + "</td></tr>";
    }
    html += "</table>";
  }
  html += "</div>" + htmlFooter();
  server.send(200,"text/html",html);
}

void handleNotificationsClearPOST() {
  clearNotifications();
  server.sendHeader("Location","/notifications"); server.send(303,"text/plain","Notificaciones borradas");
}
