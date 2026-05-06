#include "files_utils.h"
#include "db_sync.h"

#include <ArduinoJson.h>

// ------------------------------------------------------------
// Helpers JSON
// ------------------------------------------------------------
static bool parseResponseDoc(const String &payload, DynamicJsonDocument &doc) {
  if (payload.length() == 0) return false;

  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.print("WARN: no se pudo parsear JSON: ");
    Serial.println(err.c_str());
    return false;
  }
  return true;
}

static JsonArrayConst extractArray(const JsonDocument &doc) {
  if (doc.is<JsonArrayConst>()) {
    return doc.as<JsonArrayConst>();
  }

  if (doc.is<JsonObjectConst>()) {
    JsonObjectConst obj = doc.as<JsonObjectConst>();

    JsonArrayConst arr = obj["data"].as<JsonArrayConst>();
    if (!arr.isNull()) return arr;

    arr = obj["items"].as<JsonArrayConst>();
    if (!arr.isNull()) return arr;

    arr = obj["rows"].as<JsonArrayConst>();
    if (!arr.isNull()) return arr;

    arr = obj["result"].as<JsonArrayConst>();
    if (!arr.isNull()) return arr;
  }

  return JsonArrayConst();
}

static String pickString(JsonObjectConst obj, std::initializer_list<const char*> keys) {
  for (const char* k : keys) {
    if (obj.containsKey(k)) {
      JsonVariantConst v = obj[k];
      if (!v.isNull()) {
        const char* s = v.as<const char*>();
        if (s) return String(s);
      }
    }
  }
  return String();
}

static String serializeObj(JsonObjectConst obj) {
  String out;
  serializeJson(obj, out);
  return out;
}

static bool containsIgnoreCase(const String &src, const String &needle) {
  String a = src;
  String b = needle;
  a.toLowerCase();
  b.toLowerCase();
  return a.indexOf(b) >= 0;
}

static String makeMateriaKey(const String &materia, const String &profesor) {
  if (profesor.length()) return materia + "||" + profesor;
  return materia;
}

// ------------------------------------------------------------
// Schedules
// ------------------------------------------------------------
std::vector<ScheduleEntry> loadSchedules() {
  std::vector<ScheduleEntry> res;

  String body = listHorarios(String());
  if (!body.length()) return res;

  DynamicJsonDocument doc(20000);
  if (!parseResponseDoc(body, doc)) return res;

  JsonArrayConst arr = extractArray(doc);

  auto pushEntry = [&](JsonObjectConst obj) {
    ScheduleEntry e;
    String materia = pickString(obj, {"materia"});
    String profesor = pickString(obj, {"profesor"});
    String day     = pickString(obj, {"day", "dia"});
    String start   = pickString(obj, {"start", "hora_inicio", "inicio"});
    String end     = pickString(obj, {"end", "hora_fin", "fin"});

    (void)profesor;
    e.materia = materia;
    e.day = day;
    e.start = start;
    e.end = end;

    if (e.materia.length() && e.day.length() && e.start.length() && e.end.length()) {
      res.push_back(e);
    }
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) {
      pushEntry(obj);
    }
    return res;
  }

  if (doc.is<JsonObjectConst>()) {
    pushEntry(doc.as<JsonObjectConst>());
  }

  return res;
}

bool slotOccupied(const String &day, const String &start, const String &materiaFilter) {
  auto v = loadSchedules();

  for (auto &e : v) {
    if (materiaFilter.length() && e.materia != materiaFilter) {
      continue;
    }

    if (e.day == day && e.start == start) {
      return true;
    }
  }

  return false;
}

void addScheduleSlot(const String &materia, const String &day, const String &start, const String &end) {
  bool ok = sendHorarioRegistro(materia, String(), day, start, end);
  if (!ok) {
    Serial.printf("WARN: no se pudo crear horario online (%s %s %s-%s)\n",
                  materia.c_str(), day.c_str(), start.c_str(), end.c_str());
  }
}

// ------------------------------------------------------------
// Courses
// ------------------------------------------------------------
std::vector<Course> loadCourses() {
  std::vector<Course> res;

  String body = listMaterias();
  if (!body.length()) return res;

  DynamicJsonDocument doc(20000);
  if (!parseResponseDoc(body, doc)) return res;

  JsonArrayConst arr = extractArray(doc);

  auto pushCourse = [&](JsonObjectConst obj) {
    Course c;
    c.materia = pickString(obj, {"materia", "name"});
    c.profesor = pickString(obj, {"profesor", "teacher"});
    c.created_at = pickString(obj, {"created_at", "createdAt"});

    if (c.materia.length()) {
      res.push_back(c);
    }
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) {
      pushCourse(obj);
    }
    return res;
  }

  if (doc.is<JsonObjectConst>()) {
    pushCourse(doc.as<JsonObjectConst>());
  }

  return res;
}

bool courseExists(const String &materia) {
  if (!materia.length()) return false;

  auto v = loadCourses();
  for (auto &c : v) {
    if (c.materia == materia) return true;
  }
  return false;
}

void addCourse(const String &materia, const String &prof) {
  bool ok = sendMateriaRegistro(materia, prof);
  if (!ok) {
    Serial.printf("WARN: no se pudo crear materia online (%s -> %s)\n",
                  materia.c_str(), prof.c_str());
  }
}

void writeCourses(const std::vector<Course> &list) {
  for (const auto &c : list) {
    if (!sendMateriaRegistro(c.materia, c.profesor, c.created_at)) {
      Serial.printf("WARN: no se pudo sincronizar materia '%s'\n", c.materia.c_str());
    }
  }
}

// ------------------------------------------------------------
// Users
// ------------------------------------------------------------
static std::vector<JsonObject> loadAllStudentObjects(JsonDocument &storage) {
  std::vector<JsonObject> out;

  String body = listAlumnos(String());
  if (!body.length()) return out;

  if (!parseResponseDoc(body, static_cast<DynamicJsonDocument&>(storage))) return out;

  JsonArrayConst arr = extractArray(storage);

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) {
      JsonObject copy = storage.createNestedObject();
      for (JsonPairConst p : obj) {
        copy[p.key().c_str()] = p.value();
      }
      out.push_back(copy);
    }
    return out;
  }

  if (storage.is<JsonObjectConst>()) {
    JsonObjectConst obj = storage.as<JsonObjectConst>();
    JsonObject copy = storage.createNestedObject();
    for (JsonPairConst p : obj) {
      copy[p.key().c_str()] = p.value();
    }
    out.push_back(copy);
  }

  return out;
}

static std::vector<String> loadAllStudentRows() {
  std::vector<String> out;

  String body = listAlumnos(String());
  if (!body.length()) return out;

  DynamicJsonDocument doc(25000);
  if (!parseResponseDoc(body, doc)) return out;

  JsonArrayConst arr = extractArray(doc);

  auto pushRow = [&](JsonObjectConst obj) {
    String uid      = pickString(obj, {"rfid_uid", "uid"});
    String name     = pickString(obj, {"name", "nombre"});
    String account  = pickString(obj, {"account", "cuenta"});
    String materia  = pickString(obj, {"materia"});
    String created  = pickString(obj, {"created_at", "createdAt"});
    (void)created;

    if (uid.length()) {
      out.push_back(serializeObj(obj));
    }
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) pushRow(obj);
    return out;
  }

  if (doc.is<JsonObjectConst>()) pushRow(doc.as<JsonObjectConst>());

  return out;
}

String findAnyUserByUID(const String &uid) {
  String body = listAlumnos(String());
  if (!body.length()) return String();

  DynamicJsonDocument doc(25000);
  if (!parseResponseDoc(body, doc)) return String();

  JsonArrayConst arr = extractArray(doc);

  auto matchObj = [&](JsonObjectConst obj) -> String {
    String a = pickString(obj, {"rfid_uid", "uid"});
    if (a == uid) return serializeObj(obj);
    return String();
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) {
      String found = matchObj(obj);
      if (found.length()) return found;
    }
    return String();
  }

  if (doc.is<JsonObjectConst>()) {
    return matchObj(doc.as<JsonObjectConst>());
  }

  return String();
}

bool existsUserUidMateria(const String &uid, const String &materia) {
  String body = listAlumnos(String());
  if (!body.length()) return false;

  DynamicJsonDocument doc(25000);
  if (!parseResponseDoc(body, doc)) return false;

  JsonArrayConst arr = extractArray(doc);

  auto checkObj = [&](JsonObjectConst obj) -> bool {
    String a = pickString(obj, {"rfid_uid", "uid"});
    String m = pickString(obj, {"materia"});
    return (a == uid && m == materia);
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) {
      if (checkObj(obj)) return true;
    }
    return false;
  }

  if (doc.is<JsonObjectConst>()) return checkObj(doc.as<JsonObjectConst>());

  return false;
}

bool existsUserAccountMateria(const String &account, const String &materia) {
  String body = listAlumnos(String());
  if (!body.length()) return false;

  DynamicJsonDocument doc(25000);
  if (!parseResponseDoc(body, doc)) return false;

  JsonArrayConst arr = extractArray(doc);

  auto checkObj = [&](JsonObjectConst obj) -> bool {
    String a = pickString(obj, {"account", "cuenta"});
    String m = pickString(obj, {"materia"});
    return (a == account && m == materia);
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) {
      if (checkObj(obj)) return true;
    }
    return false;
  }

  if (doc.is<JsonObjectConst>()) return checkObj(doc.as<JsonObjectConst>());

  return false;
}

std::vector<String> usersForMateria(const String &materia) {
  std::vector<String> res;

  String body = listAlumnos(String());
  if (!body.length()) return res;

  DynamicJsonDocument doc(25000);
  if (!parseResponseDoc(body, doc)) return res;

  JsonArrayConst arr = extractArray(doc);

  auto pushIfMatch = [&](JsonObjectConst obj) {
    String m = pickString(obj, {"materia"});
    if (m == materia) {
      res.push_back(serializeObj(obj));
    }
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) pushIfMatch(obj);
  } else if (doc.is<JsonObjectConst>()) {
    pushIfMatch(doc.as<JsonObjectConst>());
  }

  return res;
}

// ------------------------------------------------------------
// Notifications / logs
// ------------------------------------------------------------
void addNotification(const String &uid, const String &name, const String &account, const String &note) {
  if (!sendNotificacionRegistro(nowISO(), uid, name, account, note)) {
    Serial.printf("WARN: no se pudo enviar notificación online UID=%s\n", uid.c_str());
  }
}

std::vector<String> readNotifications(int limit) {
  std::vector<String> res;

  String body = listNotificaciones(false);
  if (!body.length()) return res;

  DynamicJsonDocument doc(30000);
  if (!parseResponseDoc(body, doc)) return res;

  JsonArrayConst arr = extractArray(doc);

  auto pushRow = [&](JsonObjectConst obj) {
    res.push_back(serializeObj(obj));
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) pushRow(obj);
  } else if (doc.is<JsonObjectConst>()) {
    pushRow(doc.as<JsonObjectConst>());
  }

  if (limit > 0 && (int)res.size() > limit) {
    std::vector<String> trimmed;
    int start = (int)res.size() - limit;
    for (int i = start; i < (int)res.size(); ++i) {
      trimmed.push_back(res[i]);
    }
    return trimmed;
  }

  return res;
}

int notifCount() {
  String body = listNotificaciones(false);
  if (!body.length()) return 0;

  DynamicJsonDocument doc(30000);
  if (!parseResponseDoc(body, doc)) return 0;

  JsonArrayConst arr = extractArray(doc);
  if (!arr.isNull()) return (int)arr.size();

  if (doc.is<JsonObjectConst>()) return 1;
  return 0;
}

void clearNotifications() {
  String body = listNotificaciones(false);
  if (!body.length()) return;

  DynamicJsonDocument doc(30000);
  if (!parseResponseDoc(body, doc)) return;

  JsonArrayConst arr = extractArray(doc);

  auto deleteFromObj = [&](JsonObjectConst obj) {
    int id = -1;
    if (obj.containsKey("id")) id = obj["id"].as<int>();
    else if (obj.containsKey("notif_id")) id = obj["notif_id"].as<int>();

    if (id >= 0) {
      deleteNotificacionById(id);
    }
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) deleteFromObj(obj);
  } else if (doc.is<JsonObjectConst>()) {
    deleteFromObj(doc.as<JsonObjectConst>());
  }
}

// ------------------------------------------------------------
// Teachers helpers
// ------------------------------------------------------------
static std::vector<String> loadAllTeacherRows() {
  std::vector<String> out;

  String body = listProfesores();
  if (!body.length()) return out;

  DynamicJsonDocument doc(25000);
  if (!parseResponseDoc(body, doc)) return out;

  JsonArrayConst arr = extractArray(doc);

  auto pushRow = [&](JsonObjectConst obj) {
    String uid      = pickString(obj, {"rfid_uid", "uid"});
    String name     = pickString(obj, {"name", "nombre"});
    String account  = pickString(obj, {"account", "cuenta"});
    String materia  = pickString(obj, {"materia"});
    String created  = pickString(obj, {"created_at", "createdAt"});
    (void)created;

    if (uid.length()) {
      out.push_back(serializeObj(obj));
    }
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) pushRow(obj);
    return out;
  }

  if (doc.is<JsonObjectConst>()) pushRow(doc.as<JsonObjectConst>());

  return out;
}

String findTeacherByUID(const String &uid) {
  String body = listProfesores();
  if (!body.length()) return String();

  DynamicJsonDocument doc(25000);
  if (!parseResponseDoc(body, doc)) return String();

  JsonArrayConst arr = extractArray(doc);

  auto matchObj = [&](JsonObjectConst obj) -> String {
    String a = pickString(obj, {"rfid_uid", "uid"});
    if (a == uid) return serializeObj(obj);
    return String();
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) {
      String found = matchObj(obj);
      if (found.length()) return found;
    }
    return String();
  }

  if (doc.is<JsonObjectConst>()) {
    return matchObj(doc.as<JsonObjectConst>());
  }

  return String();
}

bool teacherNameExists(const String &name) {
  String body = listProfesores();
  if (!body.length()) return false;

  DynamicJsonDocument doc(25000);
  if (!parseResponseDoc(body, doc)) return false;

  JsonArrayConst arr = extractArray(doc);

  auto checkObj = [&](JsonObjectConst obj) -> bool {
    String n = pickString(obj, {"name", "nombre"});
    return n == name;
  };

  if (!arr.isNull()) {
    for (JsonObjectConst obj : arr) {
      if (checkObj(obj)) return true;
    }
    return false;
  }

  if (doc.is<JsonObjectConst>()) return checkObj(doc.as<JsonObjectConst>());

  return false;
}

std::vector<String> teachersForMateria(const String &materia) {
  std::vector<String> out;

  auto teacherRows = loadAllTeacherRows();
  auto courses = loadCourses();

  for (auto &row : teacherRows) {
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, row)) continue;

    if (!doc.is<JsonObjectConst>()) continue;
    JsonObjectConst obj = doc.as<JsonObjectConst>();

    String uid     = pickString(obj, {"rfid_uid", "uid"});
    String name    = pickString(obj, {"name", "nombre"});
    String acc     = pickString(obj, {"account", "cuenta"});
    String created  = pickString(obj, {"created_at", "createdAt"});
    String rowMateria = pickString(obj, {"materia"});

    bool match = false;

    if (rowMateria.length() && rowMateria == materia) {
      match = true;
    } else {
      for (auto &co : courses) {
        if (co.materia == materia && co.profesor == name) {
          match = true;
          break;
        }
      }
    }

    if (match) {
      DynamicJsonDocument outDoc(512);
      outDoc["rfid_uid"] = uid;
      outDoc["name"] = name;
      outDoc["account"] = acc;
      outDoc["materia"] = materia;
      outDoc["created_at"] = created.length() ? created : nowISO();

      String serialized;
      serializeJson(outDoc, serialized);
      out.push_back(serialized);
    }
  }

  return out;
}