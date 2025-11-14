// src/time_utils.cpp
// Implementaciones: nowISO(), uidBytesToString(), currentScheduledMateria()
// + utilidades para ajustar la hora manualmente.
// Nota: este módulo aplica un offset fijo (LOCAL_TZ_OFFSET_SEC) sobre el epoch
// para obtener la "hora local" sin depender de tzdata del sistema.

#include <Arduino.h>
#include "globals.h"
#include <time.h>
#include <sys/time.h> // settimeofday

// ---------------- CONFIG (ajusta si cambias de zona) ----------------
// Usar offset fijo en segundos respecto a UTC.
// Toluca / Ciudad de México: UTC-6 -> -6 * 3600 = -21600
// Si necesitas UTC-5 usa -18000, UTC+1 usa 3600, etc.
static const long LOCAL_TZ_OFFSET_SEC = -6L * 3600L;
// --------------------------------------------------------------------

// nowISO: devuelve fecha/hora local en formato "YYYY-MM-DD HH:MM:SS"
// Nota: calcula la hora local a partir del epoch (time(nullptr)) aplicando LOCAL_TZ_OFFSET_SEC
String nowISO() {
  time_t epoch = time(nullptr);
  // ajustar por offset fijo
  time_t local_epoch = epoch + LOCAL_TZ_OFFSET_SEC;
  // convertir a tm en UTC (puesto que ya aplicamos offset manual)
  struct tm tm_local;
#if defined(_MSC_VER)
  gmtime_s(&tm_local, &local_epoch);
#else
  gmtime_r(&local_epoch, &tm_local);
#endif

  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm_local);
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

// Helper: parse "HH:MM" permisivo -> devuelve true si parseó y llena sh,sm
static bool parseHHMMPermissive(const String &t, int &outH, int &outM) {
  String s = t;
  s.trim();
  int colon = s.indexOf(':');
  if (colon < 0) return false;
  String hs = s.substring(0, colon);
  String ms = s.substring(colon + 1);
  hs.trim(); ms.trim();
  if (hs.length() == 0 || ms.length() == 0) return false;
  int h = hs.toInt();
  int m = ms.toInt();
  // sanity checks
  if (h < 0 || h > 23) return false;
  if (m < 0 || m > 59) return false;
  outH = h;
  outM = m;
  return true;
}

// currentScheduledMateria: determina la materia activa según la hora local y schedules
// Retorna la parte "materia" de s.materia (si s.materia es "Materia||Profesor", retorna "Materia").
String currentScheduledMateria() {
  // Obtenemos epoch UTC y convertimos a hora local usando offset fijo
  time_t epoch = time(nullptr);
  time_t local_epoch = epoch + LOCAL_TZ_OFFSET_SEC;
  struct tm tm_now;
#if defined(_MSC_VER)
  gmtime_s(&tm_now, &local_epoch);
#else
  gmtime_r(&local_epoch, &tm_now);
#endif

  // Debug: imprime hora UTC y local (en Serial) para verificar
  {
    char bufUtc[32], bufLocal[32];
    time_t utc_epoch = epoch;
    struct tm tu;
#if defined(_MSC_VER)
    gmtime_s(&tu, &utc_epoch);
#else
    gmtime_r(&utc_epoch, &tu);
#endif
    strftime(bufUtc, sizeof(bufUtc), "%Y-%m-%d %H:%M", &tu);
    strftime(bufLocal, sizeof(bufLocal), "%Y-%m-%d %H:%M", &tm_now);
    Serial.printf("DEBUG now UTC: %s  local (offset %ld s): %s (tm_wday=%d)\n",
                  bufUtc, (long)LOCAL_TZ_OFFSET_SEC, bufLocal, tm_now.tm_wday);
  }

  int wday = tm_now.tm_wday; // 0=domingo,1=lunes,...6=sábado
  int dayIndex = -1;
  if (wday >= 1 && wday <= 6) dayIndex = wday - 1; // map lunes..sab -> 0..5
  if (dayIndex < 0) return String();

  int nowMin = tm_now.tm_hour * 60 + tm_now.tm_min;
  auto schedules = loadSchedules();

  // Debug: cuántos schedules cargados
  Serial.printf("DEBUG schedules loaded: %d\n", (int)schedules.size());
  for (auto &s : schedules) {
    Serial.printf("  sched owner='%s' day='%s' start='%s' end='%s'\n",
                  s.materia.c_str(), s.day.c_str(), s.start.c_str(), s.end.c_str());
  }

  for (auto &s : schedules) {
    String schedDay = s.day; schedDay.trim();
    if (schedDay != DAYS[dayIndex]) continue;

    int sh = -1, sm = -1, eh = -1, em = -1;
    bool okStart = parseHHMMPermissive(s.start, sh, sm);
    bool okEnd   = parseHHMMPermissive(s.end, eh, em);
    if (!okStart || !okEnd) {
      Serial.printf("WARN: schedule parse failed for owner='%s' day='%s' start='%s' end='%s'\n",
                    s.materia.c_str(), s.day.c_str(), s.start.c_str(), s.end.c_str());
      continue;
    }

    int smin = sh * 60 + sm;
    int emin = eh * 60 + em;
    if (smin <= nowMin && nowMin <= emin) {
      String schedMat = s.materia;
      int sep = schedMat.indexOf("||");
      if (sep >= 0) schedMat = schedMat.substring(0, sep);
      schedMat.trim();
      Serial.printf("DEBUG found schedule match: %s (now %02d:%02d between %02d:%02d - %02d:%02d)\n",
                    schedMat.c_str(), tm_now.tm_hour, tm_now.tm_min, sh, sm, eh, em);
      return schedMat;
    }
  }

  return String();
}

// ---------------------------
// Funciones para setear hora manualmente (desde PC o script).
// ---------------------------

// setTimeFromEpoch: ajusta el reloj del sistema (epoch = segundos desde 1970 UTC)
// Además ajusta la hora local internamente (no cambia LOCAL_TZ_OFFSET_SEC).
void setTimeFromEpoch(uint32_t epoch_seconds) {
  struct timeval tv;
  tv.tv_sec = (time_t)epoch_seconds;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) == 0) {
    // pequeño delay para que time() y gmtime_r lean la nueva hora
    delay(10);
  } else {
    Serial.println("WARN: settimeofday falló.");
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
