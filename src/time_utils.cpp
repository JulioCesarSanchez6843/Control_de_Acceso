// src/time_utils.cpp
// Implementaciones: nowISO(), uidBytesToString(), currentScheduledMateria()
// + utilidades para ajustar la hora manualmente.

#include <Arduino.h>
#include "globals.h"
#include <time.h>
#include <sys/time.h> // settimeofday

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
// Retorna la parte "materia" de s.materia (si s.materia es "Materia||Profesor", retorna "Materia").
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
    // El campo s.materia puede ser "Materia" o "Materia||Profesor" (legacy o composite).
    // Para comparación de horario simplemente se compara la parte materia almacenada en s.materia
    // (si viene compuesta, se toma la parte antes de "||").
    String schedMat = s.materia;
    int sep = schedMat.indexOf("||");
    if (sep >= 0) schedMat = schedMat.substring(0, sep);

    if (s.day != DAYS[dayIndex]) continue;
    // s.start e.g. "07:00", s.end e.g. "09:00"
    if (s.start.length() < 5 || s.end.length() < 5) continue;
    int sh = s.start.substring(0,2).toInt();
    int sm = s.start.substring(3,5).toInt();
    int eh = s.end.substring(0,2).toInt();
    int em = s.end.substring(3,5).toInt();
    int smin = sh*60 + sm;
    int emin = eh*60 + em;
    if (smin <= nowMin && nowMin <= emin) return schedMat;
  }
  return String();
}

// ---------------------------
// Funciones para setear hora manualmente (desde PC o script).
// ---------------------------

// setTimeFromEpoch: ajusta el reloj del sistema (epoch = segundos desde 1970 UTC)
void setTimeFromEpoch(uint32_t epoch_seconds) {
  struct timeval tv;
  tv.tv_sec = (time_t)epoch_seconds;
  tv.tv_usec = 0;
  // aplicar la hora
  if (settimeofday(&tv, nullptr) == 0) {
    // actualizar tz (si ya tienes TZ definida en globals, repetirla aqui por si acaso)
    setenv("TZ", TZ, 1);
    tzset();
    // opcional: small delay para que getLocalTime lea la nueva hora
    delay(10);
  } else {
    // fallo (raro en ESP32), no hacemos nada
  }
}

// Devuelve epoch actual (segundos) según reloj del sistema
uint32_t getEpochNow() {
  time_t t = time(nullptr);
  return (uint32_t)t;
}

// Imprime hora local legible por Serial (usa nowISO())
void printLocalTimeToSerial() {
  Serial.println(nowISO());
}

