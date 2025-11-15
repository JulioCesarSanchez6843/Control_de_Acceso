#include <Arduino.h>
#include "globals.h"
#include <time.h>
#include <sys/time.h> 

// Offset fijo en segundos respecto a UTC (ajustar según zona).
static const long LOCAL_TZ_OFFSET_SEC = -6L * 3600L; // UTC-6

// nowISO: devuelve "YYYY-MM-DD HH:MM:SS" de la hora local (aplica offset fijo).
String nowISO() {
  time_t epoch = time(nullptr);
  time_t local_epoch = epoch + LOCAL_TZ_OFFSET_SEC;
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

// uidBytesToString: convierte UID bytes a cadena hex (mayúsculas, sin separadores).
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

// parseHHMMPermissive: parsea "HH:MM" flexible; devuelve true y llena outH/outM.
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
  if (h < 0 || h > 23) return false;
  if (m < 0 || m > 59) return false;
  outH = h;
  outM = m;
  return true;
}

// currentScheduledMateria: devuelve la materia activa según schedules y hora local.
String currentScheduledMateria() {
  time_t epoch = time(nullptr);
  time_t local_epoch = epoch + LOCAL_TZ_OFFSET_SEC;
  struct tm tm_now;
#if defined(_MSC_VER)
  gmtime_s(&tm_now, &local_epoch);
#else
  gmtime_r(&local_epoch, &tm_now);
#endif

  // Debug: muestra UTC/local y weekday.
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

  int wday = tm_now.tm_wday; 
  int dayIndex = -1;
  if (wday >= 1 && wday <= 6) dayIndex = wday - 1; 
  if (dayIndex < 0) return String();

  int nowMin = tm_now.tm_hour * 60 + tm_now.tm_min;
  auto schedules = loadSchedules();

  // Debug: lista de schedules cargados.
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

// setTimeFromEpoch: ajusta el reloj del sistema al epoch proporcionado (UTC).
void setTimeFromEpoch(uint32_t epoch_seconds) {
  struct timeval tv;
  tv.tv_sec = (time_t)epoch_seconds;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) == 0) {
    delay(10); // permitir que time() se actualice
  } else {
    Serial.println("WARN: settimeofday falló.");
  }
}

// getEpochNow: devuelve epoch actual (segundos).
uint32_t getEpochNow() {
  time_t t = time(nullptr);
  return (uint32_t)t;
}

// printLocalTimeToSerial: imprime nowISO() por Serial.
void printLocalTimeToSerial() {
  Serial.println(nowISO());
}
