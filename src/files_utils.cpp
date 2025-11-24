#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include "rfid_handler.h"  
#include <SPIFFS.h>
#include <algorithm>

// --- Parseo CSV (líneas con campos entre comillas) ---
std::vector<String> parseQuotedCSVLine(const String &line) {
  std::vector<String> cols;
  int i = 0;
  int n = line.length();
  while (i < n) {
    // buscar comilla inicial
    while (i < n && line[i] != '"') i++;
    if (i >= n) break;
    int start = i + 1;
    int end = line.indexOf('"', start);
    if (end == -1) {
      // sin comilla de cierre -> tomar el resto
      cols.push_back(line.substring(start));
      break;
    }
    cols.push_back(line.substring(start, end));
    i = end + 1;
    if (i < n && line[i] == ',') i++;
  }
  return cols;
}

// Añade una línea al final de un archivo. True si tiene éxito.
bool appendLineToFile(const char *path, const String &line) {
  File f = SPIFFS.open(path, FILE_APPEND);
  if (!f) {
    Serial.printf("ERR append %s\n", path);
    return false;
  }
  f.println(line);
  f.close();
  return true;
}

// Sobrescribe un archivo con todas las líneas dadas. True si tiene éxito.
bool writeAllLines(const char *path, const std::vector<String> &lines) {
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) {
    Serial.printf("ERR writeAll %s\n", path);
    return false;
  }
  for (const String &L : lines) f.println(L);
  f.close();
  return true;
}

// Crea archivos CSV básicos si no existen (cabeceras).
void initFiles() {
  if (!SPIFFS.exists(USERS_FILE)) {
    File f = SPIFFS.open(USERS_FILE, FILE_WRITE);
    if (f) {
      f.println("\"uid\",\"name\",\"account\",\"materia\",\"created_at\"");
      f.close();
    }
  }
  if (!SPIFFS.exists(ATT_FILE)) {
    File f = SPIFFS.open(ATT_FILE, FILE_WRITE);
    if (f) {
      f.println("\"timestamp\",\"uid\",\"name\",\"account\",\"materia\",\"mode\"");
      f.close();
    }
  }
  if (!SPIFFS.exists(DENIED_FILE)) {
    File f = SPIFFS.open(DENIED_FILE, FILE_WRITE);
    if (f) {
      f.println("\"timestamp\",\"uid\",\"note\"");
      f.close();
    }
  }
  if (!SPIFFS.exists(SCHEDULES_FILE)) {
    File f = SPIFFS.open(SCHEDULES_FILE, FILE_WRITE);
    if (f) {
      f.println("\"materia\",\"day\",\"start\",\"end\"");
      f.close();
    }
  }
  if (!SPIFFS.exists(NOTIF_FILE)) {
    File f = SPIFFS.open(NOTIF_FILE, FILE_WRITE);
    if (f) {
      f.println("\"timestamp\",\"uid\",\"name\",\"account\",\"note\"");
      f.close();
    }
  }
  if (!SPIFFS.exists(COURSES_FILE)) {
    File f = SPIFFS.open(COURSES_FILE, FILE_WRITE);
    if (f) {
      f.println("\"materia\",\"profesor\",\"created_at\"");
      f.close();
    }
  }
}

// --- Horarios (schedules) ---
// Carga todas las entradas de SCHEDULES_FILE y las devuelve.
std::vector<ScheduleEntry> loadSchedules() {
  std::vector<ScheduleEntry> res;
  File f = SPIFFS.open(SCHEDULES_FILE, FILE_READ);
  if (!f) return res;
  String header = f.readStringUntil('\n');
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length() == 0) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 4) {
      ScheduleEntry e; e.materia = c[0]; e.day = c[1]; e.start = c[2]; e.end = c[3];
      res.push_back(e);
    }
  }
  f.close();
  return res;
}

// Comprueba si ya existe un slot para día+hora.
bool slotOccupied(const String &day, const String &start, const String &materiaFilter) {
  auto v = loadSchedules();
  for (auto &e : v) {
    if (materiaFilter.length() && e.materia != materiaFilter) continue;
    if (e.day == day && e.start == start) return true;
  }
  return false;
}

// Añade una entrada de horario al CSV de schedules.
void addScheduleSlot(const String &materia, const String &day, const String &start, const String &end) {
  String line = "\"" + materia + "\"," + "\"" + day + "\"," + "\"" + start + "\"," + "\"" + end + "\"";
  appendLineToFile(SCHEDULES_FILE, line);
}

// --- Cursos (courses) ---
// Carga cursos desde COURSES_FILE y los devuelve.
std::vector<Course> loadCourses() {
  std::vector<Course> res;
  File f = SPIFFS.open(COURSES_FILE, FILE_READ);
  if (!f) return res;
  String header = f.readStringUntil('\n');
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (l.length() == 0) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 3) {
      Course co; co.materia = c[0]; co.profesor = c[1]; co.created_at = c[2]; res.push_back(co);
    } else if (c.size() == 2) {
      Course co; co.materia = c[0]; co.profesor = c[1]; co.created_at = ""; res.push_back(co);
    }
  }
  f.close();
  return res;
}

// Devuelve true si la materia ya existe en cursos.
bool courseExists(const String &materia) {
  if (materia.length() == 0) return false;
  auto v = loadCourses();
  for (auto &c : v) if (c.materia == materia) return true;
  return false;
}

// Añade un curso con timestamp actual.
void addCourse(const String &materia, const String &prof) {
  String rec = "\"" + materia + "\"," + "\"" + prof + "\"," + "\"" + nowISO() + "\"";
  appendLineToFile(COURSES_FILE, rec);
}

// Sobrescribe el CSV de cursos con la lista proporcionada.
void writeCourses(const std::vector<Course> &list) {
  std::vector<String> lines;
  lines.push_back("\"materia\",\"profesor\",\"created_at\"");
  for (auto &c : list)
    lines.push_back("\"" + c.materia + "\"," + "\"" + c.profesor + "\"," + "\"" + c.created_at + "\"");
  // writeAllLines ahora devuelve bool; se omite la comprobación aquí.
  writeAllLines(COURSES_FILE, lines);
}

// --- Usuarios ---
// Busca cualquier fila de usuario cuyo UID coincida; devuelve la línea CSV completa o "".
String findAnyUserByUID(const String &uid) {
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) return "";
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0) continue;
    auto cols = parseQuotedCSVLine(line);
    if (cols.size() > 0 && cols[0] == uid) { f.close(); return line; }
  }
  f.close();
  return "";
}

// Comprueba existencia de usuario por UID + materia.
bool existsUserUidMateria(const String &uid, const String &materia) {
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0) continue;
    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 4 && c[0] == uid && c[3] == materia) { f.close(); return true; }
  }
  f.close();
  return false;
}

// Comprueba existencia de usuario por account + materia.
bool existsUserAccountMateria(const String &account, const String &materia) {
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0) continue;
    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 4 && c[2] == account && c[3] == materia) { f.close(); return true; }
  }
  f.close();
  return false;
}

// Devuelve todas las líneas de usuarios que pertenecen a una materia.
std::vector<String> usersForMateria(const String &materia) {
  std::vector<String> res;
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  if (!f) return res;
  String header = f.readStringUntil('\n');
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0) continue;
    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 4 && c[3] == materia) res.push_back(line);
  }
  f.close();
  return res;
}

// --- Notificaciones ---
// Añade una notificación al CSV con timestamp ahora.
void addNotification(const String &uid, const String &name, const String &account, const String &note) {
  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + note + "\"";
  appendLineToFile(NOTIF_FILE, rec);
}

// Lee todas las notificaciones y devuelve las últimas `limit`.
std::vector<String> readNotifications(int limit) {
  std::vector<String> res;
  File f = SPIFFS.open(NOTIF_FILE, FILE_READ);
  if (!f) return res;
  String header = f.readStringUntil('\n');
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length()) res.push_back(line);
  }
  f.close();
  int start = max(0, (int)res.size() - limit);
  std::vector<String> out;
  for (int i = start; i < (int)res.size(); i++) out.push_back(res[i]);
  return out;
}

// Cuenta las notificaciones (excluye cabecera).
int notifCount() {
  File f = SPIFFS.open(NOTIF_FILE, FILE_READ);
  if (!f) return 0;
  int count = 0;
  String header = f.readStringUntil('\n');
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (l.length()) count++;
  }
  f.close();
  return count;
}

// Restaura el CSV de notificaciones a la cabecera (borra todo).
void clearNotifications() {
  writeAllLines(NOTIF_FILE, std::vector<String>{String("\"timestamp\",\"uid\",\"name\",\"account\",\"note\"")});
}
