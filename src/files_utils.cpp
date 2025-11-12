// src/files_utils.cpp
#include <Arduino.h>
#include <SPIFFS.h>
#include <vector>
#include "files_utils.h"   // prototipos, structs (Course, ScheduleEntry) deben estar en este header
#include "globals.h"       // externs (USERS_FILE, etc.) y objetos globales

// ---------------- Time & UID helpers ----------------
String nowISO() {
  struct tm t;
  if (!getLocalTime(&t)) return "1970-01-01 00:00:00";
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

String uidBytesToString(byte *uid, byte len) {
  String s = "";
  for (byte i = 0; i < len; i++) {
    char buf[3];
    sprintf(buf, "%02X", uid[i]);
    s += String(buf);
  }
  s.toUpperCase();
  return s;
}

// ---------------- small utilities ----------------
String urlEncode(const String &s) {
  String out = "";
  char buf[4];
  for (int i = 0; i < s.length(); i++) {
    char c = s[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      sprintf(buf, "%%%02X", (uint8_t)c);
      out += String(buf);
    }
  }
  return out;
}

String escapeHtml(const String &s) {
  String o = "";
  for (int i = 0; i < s.length(); i++) {
    char c = s[i];
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

// ---------------- CSV parser (supports "" escaped quotes) ----------------
std::vector<String> parseQuotedCSVLine(const String &line) {
  std::vector<String> cols;
  int i = 0;
  int n = line.length();
  while (i < n) {
    // find next quote
    while (i < n && line[i] != '"') i++;
    if (i >= n) break;
    int start = i + 1;
    String cur = "";
    i = start;
    while (i < n) {
      if (line[i] == '"') {
        // escaped quote?
        if (i + 1 < n && line[i + 1] == '"') {
          cur += '"';
          i += 2;
          continue;
        } else {
          i++; // consume closing quote
          break;
        }
      } else {
        cur += line[i++];
      }
    }
    cols.push_back(cur);
    if (i < n && line[i] == ',') i++;
  }
  return cols;
}

// ---------------- File helpers ----------------
void appendLineToFile(const char* path, const String &line) {
  File f = SPIFFS.open(path, FILE_APPEND);
  if (!f) { Serial.printf("ERR append %s - open fail\n", path); return; }
  if (!f.println(line)) { Serial.printf("ERR append %s - write fail\n", path); }
  f.close();
}

void writeAllLines(const char* path, const std::vector<String> &lines) {
  File f = SPIFFS.open(path, FILE_WRITE);
  if (!f) { Serial.printf("ERR writeAll %s - open fail\n", path); return; }
  for (const String &L : lines) f.println(L);
  f.close();
}

// ---------------- Init files ----------------
void initFiles() {
  if (!SPIFFS.exists(USERS_FILE)) {
    File f = SPIFFS.open(USERS_FILE, FILE_WRITE);
    f.println("\"uid\",\"name\",\"account\",\"materia\",\"created_at\"");
    f.close();
  }
  if (!SPIFFS.exists(ATT_FILE)) {
    File f = SPIFFS.open(ATT_FILE, FILE_WRITE);
    f.println("\"timestamp\",\"uid\",\"name\",\"account\",\"materia\",\"mode\"");
    f.close();
  }
  if (!SPIFFS.exists(DENIED_FILE)) {
    File f = SPIFFS.open(DENIED_FILE, FILE_WRITE);
    f.println("\"timestamp\",\"uid\",\"note\"");
    f.close();
  }
  if (!SPIFFS.exists(SCHEDULES_FILE)) {
    File f = SPIFFS.open(SCHEDULES_FILE, FILE_WRITE);
    f.println("\"materia\",\"day\",\"start\",\"end\"");
    f.close();
  }
  if (!SPIFFS.exists(NOTIF_FILE)) {
    File f = SPIFFS.open(NOTIF_FILE, FILE_WRITE);
    f.println("\"timestamp\",\"uid\",\"name\",\"account\",\"note\"");
    f.close();
  }
  if (!SPIFFS.exists(COURSES_FILE)) {
    File f = SPIFFS.open(COURSES_FILE, FILE_WRITE);
    f.println("\"materia\",\"profesor\",\"created_at\"");
    f.close();
  }
}

// ---------------- Courses ----------------
// NOTE: struct Course and prototypes MUST be in include/files_utils.h
std::vector<Course> loadCourses() {
  std::vector<Course> res;
  File f = SPIFFS.open(COURSES_FILE, FILE_READ);
  if (!f) return res;
  String header = f.readStringUntil('\n');
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim(); if (l.length() == 0) continue;
    auto c = parseQuotedCSVLine(l);
    if (c.size() >= 3) { Course co; co.materia = c[0]; co.profesor = c[1]; co.created_at = c[2]; res.push_back(co); }
    else if (c.size() == 2) { Course co; co.materia = c[0]; co.profesor = c[1]; co.created_at = ""; res.push_back(co); }
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
  for (auto &c : list) lines.push_back("\"" + c.materia + "\"," + "\"" + c.profesor + "\"," + "\"" + c.created_at + "\"");
  writeAllLines(COURSES_FILE, lines);
}

// ---------------- Schedules ----------------
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

// NOTE: signature here MUST match declaration in header (no default args)
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

// ---------------- Users & notifications ----------------
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

void addNotification(const String &uid, const String &name, const String &account, const String &note) {
  String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + note + "\"";
  appendLineToFile(NOTIF_FILE, rec);
}

// NOTE: signature here MUST match declaration in header (no default arg)
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
  int sz = (int)res.size();
  int start = sz - limit;
  if (start < 0) start = 0;
  std::vector<String> out;
  for (int i = start; i < sz; i++) out.push_back(res[i]);
  return out;
}

int notifCount() {
  File f = SPIFFS.open(NOTIF_FILE, FILE_READ);
  if (!f) return 0;
  int count = 0;
  String header = f.readStringUntil('\n');
  while (f.available()) { String l = f.readStringUntil('\n'); l.trim(); if (l.length()) count++; }
  f.close();
  return count;
}

void clearNotifications() {
  writeAllLines(NOTIF_FILE, std::vector<String>{String("\"timestamp\",\"uid\",\"name\",\"account\",\"note\"")});
}
