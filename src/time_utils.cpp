// src/time_utils.cpp
#include "time_utils.h"
#include "globals.h"
#include <time.h>
#include <sys/time.h>

// Fallback offset si NTP no está disponible
static const long LOCAL_TZ_OFFSET_SEC = -6L * 3600L;

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

String uidBytesToString(byte *uid, byte len) {
  String s;
  s.reserve(len * 2);
  for (byte i = 0; i < len; ++i) {
    char tmp[3];
    snprintf(tmp, sizeof(tmp), "%02X", uid[i]);
    s += tmp;
  }
  s.toUpperCase();
  return s;
}

static bool parseHHMMPermissive(const String &t, int &outH, int &outM) {
  String s = t; s.trim();
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
  outH = h; outM = m; return true;
}

String currentScheduledMateria() {
  time_t epoch = time(nullptr);
  time_t local_epoch = epoch + LOCAL_TZ_OFFSET_SEC;
  struct tm tm_now;
#if defined(_MSC_VER)
  gmtime_s(&tm_now, &local_epoch);
#else
  gmtime_r(&local_epoch, &tm_now);
#endif

  int wday = tm_now.tm_wday;
  int dayIndex = -1;
  if (wday >= 1 && wday <= 6) dayIndex = wday - 1;
  if (dayIndex < 0) return String();

  int nowMin = tm_now.tm_hour * 60 + tm_now.tm_min;
  auto schedules = loadSchedules();

  for (auto &s : schedules) {
    String schedDay = s.day; schedDay.trim();
    if (schedDay != DAYS[dayIndex]) continue;
    int sh, sm, eh, em;
    if (!parseHHMMPermissive(s.start, sh, sm)) continue;
    if (!parseHHMMPermissive(s.end, eh, em)) continue;
    int smin = sh * 60 + sm;
    int emin = eh * 60 + em;
    if (smin <= nowMin && nowMin <= emin) {
      String schedMat = s.materia;
      int sep = schedMat.indexOf("||");
      if (sep >= 0) schedMat = schedMat.substring(0, sep);
      schedMat.trim();
      return schedMat;
    }
  }
  return String();
}

void setTimeFromEpoch(uint32_t epoch_seconds) {
  struct timeval tv;
  tv.tv_sec = (time_t)epoch_seconds;
  tv.tv_usec = 0;
  if (settimeofday(&tv, nullptr) == 0) {
    delay(10);
  } else {
    Serial.println("WARN: settimeofday falló.");
  }
}

uint32_t getEpochNow() {
  return (uint32_t)time(nullptr);
}

void printLocalTimeToSerial() {
  Serial.println(nowISO());
}
