#include "files_utils.h"
#include "config.h"
#include "globals.h"
#include "rfid_handler.h"  // for nowISO()
#include <SPIFFS.h>
#include <algorithm>

// --- CSV parsing ---
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

// Append a line to a file. Return true on success.
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

// Write all lines (overwrite). Return true on success.
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

// --- schedules ---
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

bool slotOccupied(const String &day, const String &start, const String &materiaFilter) {
  auto v = loadSchedules();
  for (auto &e : v) {
    if (materiaFilter.length() && e.materia != materiaFilter) continue;
    if (e.day == day && e.start == start) return true;
  }
  return false;
}

void addScheduleSlot(const String &materia, const String &day, const String &start, const String &end) {
  String line = "\"" + materia + "\"," + "\"" + day + "\"," + "\"" + start + "\"," + "\"" + end + "\"";
  appendLineToFile(SCHEDULES_FILE, line);
}

// --- courses ---
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

bool courseExists(const String &materia) {
  if (materia.length() == 0) return false;
  auto v = loadCourses();
  for (auto &c : v) if (c.materia == materia) return true;
  return false;
}

void addCourse(const String &materia, const String &prof) {
  String rec = "\"" + materia + "\"," + "\"" + prof + "\"," + "\"" + nowISO() + "\"";
  appendLineToFile(COURSES_FILE, rec);
}

void writeCourses(const std::vector<Course> &list) {
  std::vector<String> lines;
  lines.push_back("\"materia\",\"profesor\",\"created_at\"");
  for (auto &c : list)
    lines.push_back("\"" + c.materia + "\"," + "\"" + c.profesor + "\"," + "\"" + c.created_at + "\"");
  // writeAllLines now returns bool; ignore return here (but it could be checked by caller)
  writeAllLines(COURSES_FILE, lines);
}

// --- users/helpers ---
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

// --- notifications ---
void addNotification(const String &uid, const String &name, const String &account, const String &note) {
  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + note + "\"";
  appendLineToFile(NOTIF_FILE, rec);
}

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

void clearNotifications() {
  writeAllLines(NOTIF_FILE, std::vector<String>{String("\"timestamp\",\"uid\",\"name\",\"account\",\"note\"")});
}
