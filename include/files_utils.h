#pragma once
// Declaraciones públicas de utilidades de archivos usadas por el proyecto.
// Este header **no** vuelve a declarar Course/ScheduleEntry (ya están en globals.h).
#include <vector>
#include <Arduino.h>
#include <FS.h>
#include "globals.h"   // <-- usar las definiciones de structs desde aquí

// --- Parse / I/O básico ---
std::vector<String> parseQuotedCSVLine(const String &line);
bool appendLineToFile(const char *path, const String &line);
bool writeAllLines(const char *path, const std::vector<String> &lines);
void initFiles();

// --- Schedules ---
std::vector<ScheduleEntry> loadSchedules();
// Nota: no repetir argumentos por defecto si ya está en globals.h
bool slotOccupied(const String &day, const String &start, const String &materiaFilter);
void addScheduleSlot(const String &materia, const String &day, const String &start, const String &end);

// --- Courses ---
std::vector<Course> loadCourses();
bool courseExists(const String &materia);
void addCourse(const String &materia, const String &prof);
void writeCourses(const std::vector<Course> &list);

// --- Usuarios / Students helpers ---
String findAnyUserByUID(const String &uid);
bool existsUserUidMateria(const String &uid, const String &materia);
bool existsUserAccountMateria(const String &account, const String &materia);
std::vector<String> usersForMateria(const String &materia);

// --- Notifications / logs ---
void addNotification(const String &uid, const String &name, const String &account, const String &note);
std::vector<String> readNotifications(int limit);
int notifCount();
void clearNotifications();

// --- Teachers helpers (usadas en rfid_handler y web) ---
String findTeacherByUID(const String &uid);
bool teacherNameExists(const String &name);
std::vector<String> teachersForMateriaFile(const String &materia);
