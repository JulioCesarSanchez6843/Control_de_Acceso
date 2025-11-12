#pragma once
/* files_utils.h
   Helpers para manejo de SPIFFS, CSV, modelos (Course / ScheduleEntry)
   Proporciona funciones usadas por web_routes.cpp y rfid_handler.cpp
*/

#include <Arduino.h>
#include <vector>

// NOTA: rutas/constantes de archivo deben estar declaradas en globals.h como
// extern const char* USERS_FILE; etc., y definidas en globals.cpp.
// No uses macros con el mismo nombre, eso rompe el preprocesador.

// ---------- Small helpers ----------
String nowISO();                     // devuelve "YYYY-MM-DD HH:MM:SS"
String uidBytesToString(byte *uid, byte len);
String urlEncode(const String &s);
String escapeHtml(const String &s);

// ---------- CSV parser ----------
std::vector<String> parseQuotedCSVLine(const String &line);

// ---------- File utilities ----------
void appendLineToFile(const char* path, const String &line);
void writeAllLines(const char* path, const std::vector<String> &lines);
void initFiles(); // crea archivos con headers si no existen

// ---------- Models ----------
struct Course {
  String materia;
  String profesor;
  String created_at;
};

struct ScheduleEntry {
  String materia;
  String day;
  String start;
  String end;
};

// ---------- Courses helpers ----------
std::vector<Course> loadCourses();
bool courseExists(const String &materia);
void addCourse(const String &materia, const String &prof);
void writeCourses(const std::vector<Course> &list);

// ---------- Schedules helpers ----------
std::vector<ScheduleEntry> loadSchedules();
bool slotOccupied(const String &day, const String &start, const String &materiaFilter = String());
void addScheduleSlot(const String &materia, const String &day, const String &start, const String &end);

// ---------- Users / Notifications ----------
String findAnyUserByUID(const String &uid);
bool existsUserUidMateria(const String &uid, const String &materia);
std::vector<String> usersForMateria(const String &materia);

void addNotification(const String &uid, const String &name, const String &account, const String &note);
std::vector<String> readNotifications(int limit = 200);
int notifCount();
void clearNotifications();
