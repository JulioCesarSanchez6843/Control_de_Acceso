#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>
#include "edit.h"
#include "globals.h"
#include "web_common.h"
#include "files_utils.h"

// GET /edit?uid=...
// Muestra formulario para editar usuario identificado por UID.
void handleEditGet() {
  if (!server.hasArg("uid")) { server.send(400,"text/plain","uid required"); return; }
  String uid = server.arg("uid");
  String line = findAnyUserByUID(uid);
  if (line.length()==0) { server.send(404,"text/plain","not found"); return; }
  auto c = parseQuotedCSVLine(line);
  String name = (c.size()>1?c[1]:"");
  String acc  = (c.size()>2?c[2]:"");
  String mat  = (c.size()>3?c[3]:"");
  String created = (c.size()>4?c[4]:"");
  String html = htmlHeader("Editar Usuario");
  html += "<div class='card'><h2>Editar Usuario</h2>";
  html += "<form method='POST' action='/edit_post'><input type='hidden' name='orig_uid' value='" + uid + "'>";
  html += "UID (no editable):<br><input value='" + uid + "' readonly><br>Name:<br><input name='name' value='" + name + "' required><br>Cuenta:<br><input name='account' value='" + acc + "' required maxlength='7' minlength='7'><br>Materia:<br>";
  auto courses = loadCourses();
  html += "<select name='materia'>";
  for (auto &c2 : courses) {
    html += "<option value='" + c2.materia + "'";
    if (c2.materia == mat) html += " selected";
    html += ">" + c2.materia + " (" + c2.profesor + ")</option>";
  }
  html += "</select><br><br><input class='btn btn-green' type='submit' value='Guardar'></form>";
  html += "<p class='small'>Registrado: " + created + "</p>";
  html += "<p style='margin-top:8px'><a class='btn btn-blue' href='/students_all'>Volver</a></p></div>" + htmlFooter();
  server.send(200,"text/html",html);
}

// POST /edit_post
// Recibe formulario de edición, valida y actualiza USERS_FILE reescribiéndolo.
void handleEditPost() {
  if (!server.hasArg("orig_uid") || !server.hasArg("name") || !server.hasArg("account") || !server.hasArg("materia")) { server.send(400,"text/plain","faltan"); return; }
  String uid = server.arg("orig_uid");
  String name = server.arg("name");
  String acc = server.arg("account");
  String mat = server.arg("materia");
  bool ok=true; if (acc.length()!=7) ok=false; for (size_t i=0;i<acc.length();i++) if (!isDigit(acc[i])) ok=false;
  if (!ok) { server.send(400,"text/plain","Cuenta invalida"); return; }
  if (!courseExists(mat)) { server.send(400,"text/plain","Materia no registrada"); return; }

  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) { server.send(500,"text/plain","no file"); return; }
  std::vector<String> lines; String header = f.readStringUntil('\n'); lines.push_back(header);
  bool found=false;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (!l.length()) continue;
    auto c = parseQuotedCSVLine(l);
    if (!found && c.size()>0 && c[0]==uid) {
      String created = (c.size()>4?c[4]:nowISO());
      String newL = "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + acc + "\"," + "\"" + mat + "\"," + "\"" + created + "\"";
      lines.push_back(newL); found=true;
    } else lines.push_back(l);
  }
  f.close();
  if (!found) { server.send(404,"text/plain","UID not found"); return; }
  writeAllLines(USERS_FILE, lines);
  server.sendHeader("Location","/"); server.send(303,"text/plain","OK");
}
