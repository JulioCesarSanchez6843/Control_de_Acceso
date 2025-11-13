// src/time_utils.cpp
// Implementaciones: nowISO(), uidBytesToString(), currentScheduledMateria()

#include <Arduino.h>
#include "globals.h"
#include <time.h>

// nowISO: devuelve fecha/hora local en formato "YYYY-MM-DD HH:MM:SS"
String nowISO() {
  struct tm t;
  if (!getLocalTime(&t)) return "1970-01-01 00:00:00";
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &t);
  return String(buf);
}

// uidBytesToString: convierte bytes UID a string hex (mayúsculas, sin separadores)
String uidBytesToString(byte *uid, byte len) {
  String s = "";
  for (byte i = 0; i < len; i++) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", uid[i]);
    s += String(buf);
  }
  s.toUpperCase();
  return s;
}

// currentScheduledMateria: determina la materia activa según la hora local y schedules
String currentScheduledMateria() {
  struct tm tm_now;
  if (!getLocalTime(&tm_now)) return String();
  int wday = tm_now.tm_wday; // 0=domingo,1=lunes,...6=sábado
  int dayIndex = -1;
  if (wday >= 1 && wday <= 6) dayIndex = wday - 1; // map lunes..sab -> 0..5
  if (dayIndex < 0) return String();

  int nowMin = tm_now.tm_hour * 60 + tm_now.tm_min;
  auto schedules = loadSchedules();
  for (auto &s : schedules) {
    if (s.day != DAYS[dayIndex]) continue;
    // s.start e.g. "07:00", s.end e.g. "09:00"
    if (s.start.length() < 5 || s.end.length() < 5) continue;
    int sh = s.start.substring(0,2).toInt();
    int sm = s.start.substring(3,5).toInt();
    int eh = s.end.substring(0,2).toInt();
    int em = s.end.substring(3,5).toInt();
    int smin = sh*60 + sm;
    int emin = eh*60 + em;
    if (smin <= nowMin && nowMin <= emin) return s.materia;
  }
  return String();
}
