#pragma once
// Utilidades del proyecto orientadas al backend.

#include <vector>
#include <Arduino.h>
#include "globals.h"

// --- Schedules ---
std::vector<ScheduleEntry> loadSchedules();
bool slotOccupied(const String &day, const String &start, const String &materiaFilter = String());
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
std::vector<String> readNotifications(int limit = 200);
int notifCount();
void clearNotifications();

// --- Teachers helpers ---
String findTeacherByUID(const String &uid);
bool teacherNameExists(const String &name);
std::vector<String> teachersForMateria(const String &materia);