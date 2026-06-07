#include "files_utils.h"
#include "globals.h"
#include "time_utils.h"
#include "db_sync.h"

#include <ArduinoJson.h>
#include <algorithm>

// ------------------------------------------------------------
// Utilidades JSON / texto
// ------------------------------------------------------------

static String trimCopy(String s) {
  s.trim();
  return s;
}

static String lowerCopy(String s) {
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

  if (v.is<String>()) return v.as<String>();
  if (v.is<bool>()) return v.as<bool>() ? "true" : "false";
  if (v.is<long>()) return String(v.as<long>());
  if (v.is<unsigned long>()) return String(v.as<unsigned long>());
  if (v.is<int>()) return String(v.as<int>());
  if (v.is<float>()) return String(v.as<float>(), 4);
  if (v.is<double>()) return String(v.as<double>(), 4);

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
    for (JsonVariantConst item : arr) callback(item);
    return;
  }

  if (root.is<JsonObjectConst>()) {
    JsonObjectConst o = root.as<JsonObjectConst>();
    const char* keys[] = {"data", "items", "rows", "result", "response",
                          "horarios", "materias", "alumnos", "profesores",
                          "notifications", "notificaciones"};
    for (const char* key : keys) {
      if (o.containsKey(key) && o[key].is<JsonArrayConst>()) {
        JsonArrayConst arr = o[key].as<JsonArrayConst>();
        for (JsonVariantConst item : arr) callback(item);
        return;
      }
    }
    callback(root);
  }
}

static String csvEscape(String s) {
  s.replace("\"", "'");
  return s;
}

static String makeCsvLine(const std::vector<String> &cols) {
  String out;
  for (size_t i = 0; i < cols.size(); ++i) {
    if (i) out += ",";
    out += "\"";
    out += csvEscape(cols[i]);
    out += "\"";
  }
  return out;
}

static bool serverReady() {
  return (WiFi.status() == WL_CONNECTED) && pingServer();
}

// ------------------------------------------------------------
// Parse CSV legacy (se conserva por compatibilidad)
// ------------------------------------------------------------

std::vector<String> parseQuotedCSVLine(const String &line) {
  std::vector<String> cols;
  int i = 0;
  int n = line.length();

  while (i < n) {
    while (i < n && line[i] != '"') i++;
    if (i >= n) break;

    int start = i + 1;
    int end = line.indexOf('"', start);

    if (end == -1) {
      cols.push_back(line.substring(start));
      break;
    }

    cols.push_back(line.substring(start, end));
    i = end + 1;
    if (i < n && line[i] == ',') i++;
  }

  return cols;
}

// ------------------------------------------------------------
// Stubs de transición: ya no escriben nada local
// ------------------------------------------------------------

bool appendLineToFile(const char *path, const String &line) {
  (void)path;
  (void)line;
  Serial.println("WARN: appendLineToFile() deshabilitada. Ya no usa SPIFFS.");
  return false;
}

bool writeAllLines(const char *path, const std::vector<String> &lines) {
  (void)path;
  (void)lines;
  Serial.println("WARN: writeAllLines() deshabilitada. Ya no usa SPIFFS.");
  return false;
}

void initFiles() {
  Serial.println("WARN: initFiles() deshabilitada. Ya no usa SPIFFS.");
}

// ------------------------------------------------------------
// Helpers para filas JSON -> líneas CSV de compatibilidad
// ------------------------------------------------------------

static String alumnoRowToCsv(const JsonObjectConst &o) {
  const char* uidKeys[]     = {"uid", "UID", "rfid_uid", "rfid", "id", "id_usuario", "codigo"};
  const char* nameKeys[]    = {"nombre", "name", "full_name", "nombre_completo", "nombreCompleto"};
  const char* accountKeys[] = {"cuenta", "account", "matricula", "registro", "numero_cuenta"};
  const char* matKeys[]     = {"materia", "subject", "asignatura", "nombre_materia"};
  const char* createdKeys[] = {"created_at", "createdAt", "fecha", "timestamp"};

  String uid     = jsonGetAny(o, uidKeys,     sizeof(uidKeys)     / sizeof(uidKeys[0]));
  String name    = jsonGetAny(o, nameKeys,    sizeof(nameKeys)    / sizeof(nameKeys[0]));
  String account = jsonGetAny(o, accountKeys, sizeof(accountKeys) / sizeof(accountKeys[0]));
  String materia = jsonGetAny(o, matKeys,     sizeof(matKeys)     / sizeof(matKeys[0]));
  String created = jsonGetAny(o, createdKeys, sizeof(createdKeys) / sizeof(createdKeys[0]));

  std::vector<String> cols = {uid, name, account, materia, created};
  return makeCsvLine(cols);
}

static String teacherRowToCsv(const JsonObjectConst &o) {
  const char* uidKeys[]     = {"uid", "UID", "rfid_uid", "rfid", "id", "id_profesor", "codigo"};
  const char* nameKeys[]    = {"nombre", "name", "full_name", "nombre_completo", "nombreCompleto"};
  const char* accountKeys[] = {"cuenta", "account", "matricula", "registro", "numero_cuenta"};
  const char* matKeys[]     = {"materia", "subject", "asignatura", "nombre_materia"};
  const char* createdKeys[] = {"created_at", "createdAt", "fecha", "timestamp"};

  String uid     = jsonGetAny(o, uidKeys,     sizeof(uidKeys)     / sizeof(uidKeys[0]));
  String name    = jsonGetAny(o, nameKeys,    sizeof(nameKeys)    / sizeof(nameKeys[0]));
  String account = jsonGetAny(o, accountKeys, sizeof(accountKeys) / sizeof(accountKeys[0]));
  String materia = jsonGetAny(o, matKeys,     sizeof(matKeys)     / sizeof(matKeys[0]));
  String created = jsonGetAny(o, createdKeys, sizeof(createdKeys) / sizeof(createdKeys[0]));

  std::vector<String> cols = {uid, name, account, materia, created};
  return makeCsvLine(cols);
}

static String notifRowToCsv(const JsonObjectConst &o) {
  const char* idKeys[]      = {"id", "notification_id", "notificacion_id", "notif_id"};
  const char* tsKeys[]      = {"timestamp", "created_at", "createdAt", "fecha"};
  const char* uidKeys[]     = {"uid", "UID", "rfid_uid", "rfid"};
  const char* nameKeys[]    = {"nombre", "name", "full_name"};
  const char* accountKeys[] = {"account", "cuenta", "matricula"};
  const char* noteKeys[]    = {"note", "nota", "mensaje", "message", "descripcion"};

  String id      = jsonGetAny(o, idKeys,      sizeof(idKeys)      / sizeof(idKeys[0]));
  String ts      = jsonGetAny(o, tsKeys,      sizeof(tsKeys)      / sizeof(tsKeys[0]));
  String uid     = jsonGetAny(o, uidKeys,     sizeof(uidKeys)     / sizeof(uidKeys[0]));
  String name    = jsonGetAny(o, nameKeys,    sizeof(nameKeys)    / sizeof(nameKeys[0]));
  String account = jsonGetAny(o, accountKeys, sizeof(accountKeys) / sizeof(accountKeys[0]));
  String note    = jsonGetAny(o, noteKeys,    sizeof(noteKeys)    / sizeof(noteKeys[0]));

  std::vector<String> cols = {id, ts, uid, name, account, note};
  return makeCsvLine(cols);
}

// ------------------------------------------------------------
// Schedules
// ------------------------------------------------------------

static ScheduleEntry scheduleFromJson(const JsonObjectConst &o) {
  ScheduleEntry e;

  const char* matKeys[]   = {"materia", "subject", "asignatura", "nombre_materia", "nombreMateria"};
  const char* dayKeys[]   = {"day", "dia", "weekday", "day_name", "nombre_dia", "nombreDia"};
  const char* startKeys[] = {"start", "inicio", "hora_inicio", "start_time", "horaInicio"};
  const char* endKeys[]   = {"end", "fin", "hora_fin", "end_time", "horaFin"};
  const char* profKeys[]  = {"profesor", "teacher", "docente", "nombre_profesor", "nombreProfesor"};

  String materia  = jsonGetAny(o, matKeys,   sizeof(matKeys)   / sizeof(matKeys[0]));
  String profesor = jsonGetAny(o, profKeys,  sizeof(profKeys)  / sizeof(profKeys[0]));
  String day      = jsonGetAny(o, dayKeys,   sizeof(dayKeys)   / sizeof(dayKeys[0]));
  String start    = jsonGetAny(o, startKeys, sizeof(startKeys) / sizeof(startKeys[0]));
  String end      = jsonGetAny(o, endKeys,   sizeof(endKeys)   / sizeof(endKeys[0]));

  if (materia.indexOf("||") < 0 && profesor.length()) {
    e.materia = materia + "||" + profesor;
  } else {
    e.materia = materia;
  }

  e.day   = day;
  e.start = start;
  e.end   = end;
  return e;
}

std::vector<ScheduleEntry> loadSchedules() {
  std::vector<ScheduleEntry> res;
  if (!serverReady()) return res;

  // listHorarios(materia="") → trae todos
  String payload = listHorarios(String());
  if (payload.length() == 0) return res;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: loadSchedules JSON invalido: %s\n", de.c_str());
    return res;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    res.push_back(scheduleFromJson(item.as<JsonObjectConst>()));
  });

  return res;
}

bool slotOccupied(const String &day, const String &start, const String &materiaFilter) {
  auto v = loadSchedules();
  String d  = trimCopy(day);
  String s  = trimCopy(start);
  String mf = trimCopy(materiaFilter);

  for (auto &e : v) {
    if (trimCopy(e.day)   != d) continue;
    if (trimCopy(e.start) != s) continue;

    if (mf.length()) {
      String em = trimCopy(e.materia);
      int p = em.indexOf("||");
      if (p >= 0) em = em.substring(0, p);
      em.trim();
      if (em != mf) continue;
    }

    return true;
  }
  return false;
}

// FIX: sendHorarioRegistro requiere 6 args: materia, profesor, dia, hora_inicio, hora_fin, created_at
// Se extrae el profesor del string "materia||profesor" si viene así, sino se deja vacío.
void addScheduleSlot(const String &materia, const String &day,
                     const String &start,   const String &end) {
  if (!serverReady()) {
    Serial.println("WARN: addScheduleSlot() sin servidor disponible.");
    return;
  }

  // Si materia viene como "NombreMateria||NombreProfesor", separar
  String mat  = materia;
  String prof = "";
  int sep = materia.indexOf("||");
  if (sep >= 0) {
    mat  = materia.substring(0, sep);
    prof = materia.substring(sep + 2);
    mat.trim();
    prof.trim();
  }

  // firma: sendHorarioRegistro(materia, profesor, dia, hora_inicio, hora_fin, created_at="")
  sendHorarioRegistro(mat, prof, day, start, end, String());
}

// ------------------------------------------------------------
// Courses / Materias
// ------------------------------------------------------------

static Course courseFromJson(const JsonObjectConst &o) {
  Course c;

  const char* matKeys[]     = {"materia", "subject", "asignatura", "nombre_materia", "nombreMateria"};
  const char* profKeys[]    = {"profesor", "teacher", "docente", "nombre_profesor", "nombreProfesor"};
  const char* createdKeys[] = {"created_at", "createdAt", "fecha", "timestamp"};

  c.materia    = jsonGetAny(o, matKeys,     sizeof(matKeys)     / sizeof(matKeys[0]));
  c.profesor   = jsonGetAny(o, profKeys,    sizeof(profKeys)    / sizeof(profKeys[0]));
  c.created_at = jsonGetAny(o, createdKeys, sizeof(createdKeys) / sizeof(createdKeys[0]));

  return c;
}

// ------------------------------------------------------------
// Usuarios / alumnos
// ------------------------------------------------------------

static bool alumnoMatches(JsonObjectConst o, const String &uid,
                          const String &materia, const String &account,
                          bool checkAccount) {
  const char* uidKeys[]     = {"uid", "UID", "rfid_uid", "rfid", "id", "id_usuario", "codigo"};
  const char* accountKeys[] = {"cuenta", "account", "matricula", "registro", "numero_cuenta"};
  const char* matKeys[]     = {"materia", "subject", "asignatura", "nombre_materia"};

  String rowUid = trimCopy(jsonGetAny(o, uidKeys,     sizeof(uidKeys)     / sizeof(uidKeys[0])));
  String rowAcc = trimCopy(jsonGetAny(o, accountKeys, sizeof(accountKeys) / sizeof(accountKeys[0])));
  String rowMat = trimCopy(jsonGetAny(o, matKeys,     sizeof(matKeys)     / sizeof(matKeys[0])));

  if (rowUid != trimCopy(uid)) return false;
  if (materia.length() && rowMat != trimCopy(materia)) return false;
  if (checkAccount && rowAcc != trimCopy(account)) return false;
  return true;
}

static bool scanAlumnos(std::function<bool(JsonObjectConst)> predicate,
                        String *outCsv = nullptr) {
  if (!serverReady()) return false;

  // listAlumnos(materia="") → trae todos
  String payload = listAlumnos(String());
  if (payload.length() == 0) return false;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listAlumnos JSON invalido: %s\n", de.c_str());
    return false;
  }

  bool found = false;
  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (found) return;
    if (!item.is<JsonObjectConst>()) return;
    JsonObjectConst o = item.as<JsonObjectConst>();
    if (predicate(o)) {
      found = true;
      if (outCsv) *outCsv = alumnoRowToCsv(o);
    }
  });

  return found;
}

String findAnyUserByUID(const String &uid) {
  String out;
  bool found = scanAlumnos([&](JsonObjectConst o) {
    return alumnoMatches(o, uid, String(), String(), false);
  }, &out);
  (void)found;
  return out;
}

bool existsUserUidMateria(const String &uid, const String &materia) {
  return scanAlumnos([&](JsonObjectConst o) {
    return alumnoMatches(o, uid, materia, String(), false);
  });
}

bool existsUserAccountMateria(const String &account, const String &materia) {
  if (!serverReady()) return false;

  // Filtrar por materia directo en el servidor para reducir payload
  String payload = listAlumnos(materia);
  if (payload.length() == 0) return false;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listAlumnos JSON invalido: %s\n", de.c_str());
    return false;
  }

  bool found = false;
  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (found) return;
    if (!item.is<JsonObjectConst>()) return;
    JsonObjectConst o = item.as<JsonObjectConst>();

    const char* accountKeys[] = {"cuenta", "account", "matricula", "registro", "numero_cuenta"};
    const char* matKeys[]     = {"materia", "subject", "asignatura", "nombre_materia"};

    String rowAcc = trimCopy(jsonGetAny(o, accountKeys, sizeof(accountKeys) / sizeof(accountKeys[0])));
    String rowMat = trimCopy(jsonGetAny(o, matKeys,     sizeof(matKeys)     / sizeof(matKeys[0])));

    if (rowAcc == trimCopy(account) && rowMat == trimCopy(materia)) {
      found = true;
    }
  });

  return found;
}

std::vector<String> usersForMateria(const String &materia) {
  std::vector<String> res;
  if (!serverReady()) return res;

  // Filtrar por materia en el servidor directamente
  String payload = listAlumnos(materia);
  if (payload.length() == 0) return res;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listAlumnos JSON invalido: %s\n", de.c_str());
    return res;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    res.push_back(alumnoRowToCsv(item.as<JsonObjectConst>()));
  });

  return res;
}

// ------------------------------------------------------------
// Notificaciones
// ------------------------------------------------------------

void addNotification(const String &uid, const String &name,
                     const String &account, const String &note) {
  if (!serverReady()) {
    Serial.println("WARN: addNotification() sin servidor disponible.");
    return;
  }
  sendNotificacionRegistro(nowISO(), uid, name, account, note);
}

std::vector<String> readNotifications(int limit) {
  std::vector<String> res;
  if (limit <= 0) return res;
  if (!serverReady()) return res;

  // FIX: listNotificaciones(bool solo_no_leidas) — false para traer todas
  String payload = listNotificaciones(false);
  if (payload.length() == 0) return res;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listNotificaciones JSON invalido: %s\n", de.c_str());
    return res;
  }

  std::vector<String> all;
  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    all.push_back(notifRowToCsv(item.as<JsonObjectConst>()));
  });

  // FIX: evitar ambigüedad de tipos en max()
  int startIdx = ((int)all.size() > limit) ? (int)all.size() - limit : 0;
  for (int i = startIdx; i < (int)all.size(); ++i) res.push_back(all[i]);
  return res;
}

int notifCount() {
  if (!serverReady()) return 0;

  String payload = listNotificaciones(false);
  if (payload.length() == 0) return 0;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listNotificaciones JSON invalido: %s\n", de.c_str());
    return 0;
  }

  int count = 0;
  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (item.is<JsonObjectConst>()) count++;
  });

  return count;
}

void clearNotifications() {
  if (!serverReady()) {
    Serial.println("WARN: clearNotifications() sin servidor disponible.");
    return;
  }

  String payload = listNotificaciones(false);
  if (payload.length() == 0) return;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listNotificaciones JSON invalido: %s\n", de.c_str());
    return;
  }

  // FIX: deleteNotificacionById(int) — guardar IDs como int, no String
  std::vector<int> ids;

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    JsonObjectConst o = item.as<JsonObjectConst>();

    const char* idKeys[] = {"id", "notification_id", "notificacion_id", "notif_id"};
    String idStr = jsonGetAny(o, idKeys, sizeof(idKeys) / sizeof(idKeys[0]));
    idStr.trim();

    if (idStr.length()) {
      int idInt = idStr.toInt();
      bool exists = false;
      for (int x : ids) {
        if (x == idInt) { exists = true; break; }
      }
      if (!exists) ids.push_back(idInt);
    }
  });

  for (int id : ids) {
    deleteNotificacionById(id);
  }
}

// ------------------------------------------------------------
// Teachers
// ------------------------------------------------------------

static bool teacherMatchesName(JsonObjectConst o, const String &name) {
  const char* nameKeys[] = {"nombre", "name", "full_name", "nombre_completo", "nombreCompleto"};
  String rowName = jsonGetAny(o, nameKeys, sizeof(nameKeys) / sizeof(nameKeys[0]));
  return trimCopy(rowName) == trimCopy(name);
}

String findTeacherByUID(const String &uid) {
  if (!serverReady()) return "";

  String payload = getProfesorByUid(uid);
  if (payload.length() == 0) return "";

  DynamicJsonDocument doc(16 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: getProfesorByUid JSON invalido: %s\n", de.c_str());
    return "";
  }

  String out;
  bool found = false;

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (found) return;
    if (!item.is<JsonObjectConst>()) return;
    out = teacherRowToCsv(item.as<JsonObjectConst>());
    found = true;
  });

  return out;
}

bool teacherNameExists(const String &name) {
  if (!serverReady()) return false;

  String payload = listProfesores();
  if (payload.length() == 0) return false;

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    Serial.printf("WARN: listProfesores JSON invalido: %s\n", de.c_str());
    return false;
  }

  bool found = false;
  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (found) return;
    if (!item.is<JsonObjectConst>()) return;
    if (teacherMatchesName(item.as<JsonObjectConst>(), name)) found = true;
  });

  return found;
}

std::vector<String> teachersForMateriaFile(const String &materia) {
  std::vector<String> out;
  if (!serverReady()) return out;

  // FIX: listProfesorMateria() no acepta argumentos — traer todo y filtrar local
  String payload = listProfesorMateria();
  if (payload.length() != 0) {
    DynamicJsonDocument doc(32 * 1024);
    DeserializationError de = deserializeJson(doc, payload);
    if (!de) {
      visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
        if (!item.is<JsonObjectConst>()) return;
        JsonObjectConst o = item.as<JsonObjectConst>();

        const char* matKeys[] = {"materia", "subject", "asignatura", "nombre_materia"};
        String rowMat = trimCopy(jsonGetAny(o, matKeys, sizeof(matKeys) / sizeof(matKeys[0])));

        // Incluir si coincide la materia o si el row no tiene materia (relación pura uid↔materia)
        if (rowMat == trimCopy(materia) || rowMat.length() == 0) {
          out.push_back(teacherRowToCsv(o));
        }
      });

      if (!out.empty()) return out;
    }
  }

  // Fallback: lista completa de profesores, filtrar por materia
  payload = listProfesores();
  if (payload.length() == 0) return out;

  DynamicJsonDocument doc2(32 * 1024);
  DeserializationError de2 = deserializeJson(doc2, payload);
  if (de2) {
    Serial.printf("WARN: listProfesores JSON invalido: %s\n", de2.c_str());
    return out;
  }

  visitJsonItems(doc2.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (!item.is<JsonObjectConst>()) return;
    JsonObjectConst o = item.as<JsonObjectConst>();

    const char* matKeys[] = {"materia", "subject", "asignatura", "nombre_materia"};
    String rowMat = trimCopy(jsonGetAny(o, matKeys, sizeof(matKeys) / sizeof(matKeys[0])));

    if (rowMat == trimCopy(materia)) {
      out.push_back(teacherRowToCsv(o));
    }
  });

  return out;
}