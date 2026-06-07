#include "time_utils.h"
#include "globals.h"
#include "db_sync.h"

#include <time.h>
#include <sys/time.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// Fallback offset si NTP no está disponible
static const long LOCAL_TZ_OFFSET_SEC = -6L * 3600L;

// ------------------------------------------------------------
// Fecha / hora
// ------------------------------------------------------------
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

// ------------------------------------------------------------
// Helpers internos
// ------------------------------------------------------------
static bool parseHHMMPermissive(const String &t, int &outH, int &outM) {
  String s = t;
  s.trim();
  int colon = s.indexOf(':');
  if (colon < 0) return false;

  String hs = s.substring(0, colon);
  String ms = s.substring(colon + 1);
  hs.trim();
  ms.trim();

  if (hs.length() == 0 || ms.length() == 0) return false;

  int h = hs.toInt();
  int m = ms.toInt();

  if (h < 0 || h > 23) return false;
  if (m < 0 || m > 59) return false;

  outH = h;
  outM = m;
  return true;
}

static String normalizeText(String s) {
  s.trim();
  s.toLowerCase();
  return s;
}

static String jsonVariantToString(JsonVariantConst v) {
  if (v.isNull()) return "";

  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    return s ? String(s) : String();
  }

  if (v.is<String>()) {
    return v.as<String>();
  }

  if (v.is<bool>()) {
    return v.as<bool>() ? "true" : "false";
  }

  if (v.is<long>()) {
    return String(v.as<long>());
  }

  if (v.is<unsigned long>()) {
    return String(v.as<unsigned long>());
  }

  if (v.is<int>()) {
    return String(v.as<int>());
  }

  if (v.is<float>()) {
    return String(v.as<float>(), 4);
  }

  if (v.is<double>()) {
    return String(v.as<double>(), 4);
  }

  return "";
}

static String jsonGetAny(const JsonObjectConst &o, const char* const keys[], size_t n) {
  for (size_t i = 0; i < n; ++i) {
    const char* k = keys[i];
    if (!k) continue;
    if (o.containsKey(k)) {
      String s = jsonVariantToString(o[k]);
      s.trim();
      if (s.length()) return s;
    }
  }
  return "";
}

template <typename T>
static void visitJsonItems(JsonVariantConst root, T callback) {
  if (root.is<JsonArrayConst>()) {
    JsonArrayConst arr = root.as<JsonArrayConst>();
    for (JsonVariantConst item : arr) {
      callback(item);
    }
    return;
  }

  if (root.is<JsonObjectConst>()) {
    JsonObjectConst o = root.as<JsonObjectConst>();
    const char* keys[] = {"data", "horarios", "items", "result", "rows", "response"};
    for (const char* key : keys) {
      if (o.containsKey(key) && o[key].is<JsonArrayConst>()) {
        JsonArrayConst arr = o[key].as<JsonArrayConst>();
        for (JsonVariantConst item : arr) {
          callback(item);
        }
        return;
      }
    }

    // Si no hay arreglo interno, tratamos el objeto como un solo registro
    callback(root);
  }
}

static String normalizeDayName(String s) {
  s.trim();
  s.toLowerCase();

  // Acepta variantes comunes
  if (s == "mon" || s == "monday" || s == "lun" || s == "lunes") return "lunes";
  if (s == "tue" || s == "tuesday" || s == "mar" || s == "martes") return "martes";
  if (s == "wed" || s == "wednesday" || s == "mie" || s == "miercoles" || s == "miércoles") return "miercoles";
  if (s == "thu" || s == "thursday" || s == "jue" || s == "jueves") return "jueves";
  if (s == "fri" || s == "friday" || s == "vie" || s == "viernes") return "viernes";
  if (s == "sat" || s == "saturday" || s == "sab" || s == "sábado" || s == "sabado") return "sabado";
  if (s == "sun" || s == "sunday" || s == "dom" || s == "domingo") return "domingo";

  return s;
}

static String serverScheduleDayToNormalized(String s) {
  s.trim();
  s.toLowerCase();

  // Si el backend manda números de día, también lo aceptamos:
  // 1=lunes ... 7=domingo
  if (s.length() == 1 && isdigit((unsigned char)s[0])) {
    int d = s.toInt();
    switch (d) {
      case 1: return "lunes";
      case 2: return "martes";
      case 3: return "miercoles";
      case 4: return "jueves";
      case 5: return "viernes";
      case 6: return "sabado";
      case 7: return "domingo";
      default: break;
    }
  }

  return normalizeDayName(s);
}

static bool sameDayAsNow(const String &serverDay, const String &nowDay) {
  String a = serverScheduleDayToNormalized(serverDay);
  String b = normalizeDayName(nowDay);
  return a == b;
}

static bool serverSeemsReady() {
  return (WiFi.status() == WL_CONNECTED) && pingServer();
}

// ------------------------------------------------------------
// Horarios desde servidor
// ------------------------------------------------------------
String currentScheduledMateria() {
  time_t epoch = time(nullptr);
  time_t local_epoch = epoch + LOCAL_TZ_OFFSET_SEC;
  struct tm tm_now;
#if defined(_MSC_VER)
  gmtime_s(&tm_now, &local_epoch);
#else
  gmtime_r(&local_epoch, &tm_now);
#endif

  // En tm_wday: 0=domingo, 1=lunes, ... 6=sábado
  int wday = tm_now.tm_wday;
  int dayIndex = -1;
  if (wday >= 1 && wday <= 6) dayIndex = wday - 1;
  if (dayIndex < 0) return String();

  int nowMin = tm_now.tm_hour * 60 + tm_now.tm_min;

  // Nombre del día actual usando globals.h
  String nowDay = DAYS[dayIndex];
  nowDay = normalizeDayName(nowDay);

  if (!serverSeemsReady()) {
    return String();
  }

  String payload = listHorarios();
  if (payload.length() == 0) {
    return String();
  }

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: JSON invalido en listHorarios(): %s\n", de.c_str());
    return String();
  }

  String foundMateria = String();

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (foundMateria.length() > 0) return;
    if (!item.is<JsonObjectConst>()) return;

    JsonObjectConst o = item.as<JsonObjectConst>();

    const char* dayKeys[]   = {"day", "dia", "weekday", "day_name", "nombre_dia", "nombreDia"};
    const char* startKeys[]  = {"start", "inicio", "hora_inicio", "start_time", "horaInicio"};
    const char* endKeys[]    = {"end", "fin", "hora_fin", "end_time", "horaFin"};
    const char* matKeys[]    = {"materia", "subject", "asignatura", "nombre_materia", "nombreMateria"};
    const char* profKeys[]   = {"profesor", "teacher", "docente", "nombre_profesor", "nombreProfesor"};

    String schedDay = jsonGetAny(o, dayKeys, sizeof(dayKeys) / sizeof(dayKeys[0]));
    if (schedDay.length() == 0) return;
    if (!sameDayAsNow(schedDay, nowDay)) return;

    String start = jsonGetAny(o, startKeys, sizeof(startKeys) / sizeof(startKeys[0]));
    String end   = jsonGetAny(o, endKeys, sizeof(endKeys) / sizeof(endKeys[0]));
    if (start.length() == 0 || end.length() == 0) return;

    int sh, sm, eh, em;
    if (!parseHHMMPermissive(start, sh, sm)) return;
    if (!parseHHMMPermissive(end, eh, em)) return;

    int smin = sh * 60 + sm;
    int emin = eh * 60 + em;
    if (!(smin <= nowMin && nowMin <= emin)) return;

    String materia = jsonGetAny(o, matKeys, sizeof(matKeys) / sizeof(matKeys[0]));
    if (materia.length() == 0) return;
    materia.trim();

    String profesor = jsonGetAny(o, profKeys, sizeof(profKeys) / sizeof(profKeys[0]));
    profesor.trim();

    // Si viene como "Materia||Profesor", la dejamos igual;
    // si viene separado, lo unimos para que rfid_handler.cpp pueda usarlo.
    if (materia.indexOf("||") < 0 && profesor.length() > 0) {
      foundMateria = materia + "||" + profesor;
    } else {
      foundMateria = materia;
    }
  });

  return foundMateria;
}

// ------------------------------------------------------------
// Tiempo del sistema
// ------------------------------------------------------------
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