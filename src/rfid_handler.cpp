#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <vector>
#include <ctype.h>
#include <ArduinoJson.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <ESP32Servo.h>
#else
  #include <Servo.h>
#endif

#include "globals.h"
#include "display.h"
#include "time_utils.h"
#include "web/self_register.h"
#include "db_sync.h"

// ------------------------------------------------------------
// Helpers de formato
// ------------------------------------------------------------

// Extrae la parte "materia" si owner viene como "Materia||Profesor"
static String baseMateriaFromOwner(const String &owner) {
  int idx = owner.indexOf("||");
  if (idx < 0) {
    String o = owner;
    o.trim();
    return o;
  }
  String b = owner.substring(0, idx);
  b.trim();
  return b;
}

// Normaliza una materia
static String normMat(const String &s) {
  String t = s;
  t.trim();
  return t;
}

// Devuelve string con materias separadas por "; "
static String joinMats(const std::vector<String> &mats) {
  String out;
  for (size_t i = 0; i < mats.size(); ++i) {
    if (i) out += "; ";
    out += mats[i];
  }
  return out;
}

// Helper: devuelve lowercase copy
static String lowerCopy(const String &s) {
  String t = s;
  t.toLowerCase();
  return t;
}

// ------------------------------------------------------------
// Helpers JSON / servidor
// ------------------------------------------------------------

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
    const char* keys[] = {"data", "alumnos", "profesores", "rows", "result", "items", "materias", "response"};
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

static void appendUnique(std::vector<String> &vec, const String &value) {
  String v = value;
  v.trim();
  if (!v.length()) return;
  for (const auto &x : vec) {
    if (x == v) return;
  }
  vec.push_back(v);
}

static void splitAndAppendMaterias(const String &src, std::vector<String> &out) {
  String work = src;
  work.trim();
  if (!work.length()) return;

  // Soporta delimitadores comunes
  int start = 0;
  while (start < (int)work.length()) {
    int p1 = work.indexOf(';', start);
    int p2 = work.indexOf(',', start);
    int end = -1;

    if (p1 < 0) end = p2;
    else if (p2 < 0) end = p1;
    else end = min(p1, p2);

    String part;
    if (end < 0) {
      part = work.substring(start);
      start = work.length();
    } else {
      part = work.substring(start, end);
      start = end + 1;
    }

    part.trim();
    if (part.length()) appendUnique(out, part);
  }
}

static bool serverSeemsReady() {
  return (WiFi.status() == WL_CONNECTED) && pingServer();
}

// ------------------------------------------------------------
// Modelos en memoria
// ------------------------------------------------------------

struct AlumnoRow {
  String uid;
  String nombre;
  String cuenta;
  String materia;
};

struct ProfesorRow {
  String uid;
  String nombre;
  String cuenta;
};

static bool parseAlumnoRow(JsonVariantConst item, AlumnoRow &out) {
  if (!item.is<JsonObjectConst>()) return false;

  JsonObjectConst o = item.as<JsonObjectConst>();
  const char* uidKeys[]     = {"uid", "UID", "id", "id_usuario", "codigo", "rfid"};
  const char* nameKeys[]    = {"nombre", "name", "full_name", "nombre_completo", "nombreCompleto"};
  const char* accountKeys[] = {"cuenta", "account", "matricula", "registro", "numero_cuenta"};
  const char* matKeys[]     = {"materia", "subject", "asignatura"};

  out.uid     = jsonGetAny(o, uidKeys, sizeof(uidKeys) / sizeof(uidKeys[0]));
  out.nombre  = jsonGetAny(o, nameKeys, sizeof(nameKeys) / sizeof(nameKeys[0]));
  out.cuenta  = jsonGetAny(o, accountKeys, sizeof(accountKeys) / sizeof(accountKeys[0]));
  out.materia = jsonGetAny(o, matKeys, sizeof(matKeys) / sizeof(matKeys[0]));

  return out.uid.length() > 0;
}

static bool parseProfesorRow(JsonVariantConst item, ProfesorRow &out) {
  if (!item.is<JsonObjectConst>()) return false;

  JsonObjectConst o = item.as<JsonObjectConst>();
  const char* uidKeys[]     = {"uid", "UID", "id", "id_profesor", "codigo", "rfid"};
  const char* nameKeys[]    = {"nombre", "name", "full_name", "nombre_completo", "nombreCompleto"};
  const char* accountKeys[] = {"cuenta", "account", "matricula", "registro", "numero_cuenta"};

  out.uid    = jsonGetAny(o, uidKeys, sizeof(uidKeys) / sizeof(uidKeys[0]));
  out.nombre = jsonGetAny(o, nameKeys, sizeof(nameKeys) / sizeof(nameKeys[0]));
  out.cuenta = jsonGetAny(o, accountKeys, sizeof(accountKeys) / sizeof(accountKeys[0]));

  return out.uid.length() > 0 || out.nombre.length() > 0 || out.cuenta.length() > 0;
}

static bool fetchAlumnosByUid(const String &uid, std::vector<AlumnoRow> &rows, String &err) {
  rows.clear();
  err = "";

  if (!serverSeemsReady()) {
    err = "Servidor no disponible";
    return false;
  }

  String payload = listAlumnos();
  if (payload.length() == 0) {
    err = "Respuesta vacia de listAlumnos()";
    return false;
  }

  DynamicJsonDocument doc(32 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    err = String("JSON invalido en listAlumnos(): ") + de.c_str();
    return false;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    AlumnoRow row;
    if (parseAlumnoRow(item, row)) {
      if (row.uid == uid) {
        rows.push_back(row);
      }
    }
  });

  return true;
}

static bool fetchProfesorByUid(const String &uid, ProfesorRow &row, String &err) {
  err = "";
  row = ProfesorRow();

  if (!serverSeemsReady()) {
    err = "Servidor no disponible";
    return false;
  }

  String payload = getProfesorByUid(uid);
  if (payload.length() == 0) {
    // No encontrado no es error de sistema; solo no existe como profesor
    return false;
  }

  DynamicJsonDocument doc(8 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    err = String("JSON invalido en getProfesorByUid(): ") + de.c_str();
    return false;
  }

  bool found = false;
  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (found) return;
    ProfesorRow tmp;
    if (parseProfesorRow(item, tmp)) {
      if (tmp.uid.length() == 0) tmp.uid = uid;

      // Aceptamos el registro si corresponde al UID consultado o si el backend ya filtró por UID
      if (tmp.uid == uid || (tmp.nombre.length() > 0 || tmp.cuenta.length() > 0)) {
        row = tmp;
        if (row.uid.length() == 0) row.uid = uid;
        found = true;
      }
    }
  });

  return found;
}

// CORRECCIÓN: listProfesorMateria() no acepta parámetros según db_sync.h.
// Se llama sin argumentos y se filtra por UID del profesor en memoria.
static bool fetchMateriasForProfesorUid(const String &uid, std::vector<String> &mats, String &err) {
  mats.clear();
  err = "";

  if (!serverSeemsReady()) {
    err = "Servidor no disponible";
    return false;
  }

  // Llamada SIN argumentos — la firma es listProfesorMateria()
  String payload = listProfesorMateria();
  if (payload.length() == 0) {
    // Sin materias no es error fatal; solo retorna vacío
    return true;
  }

  DynamicJsonDocument doc(8 * 1024);
  DeserializationError de = deserializeJson(doc, payload);
  if (de) {
    err = String("JSON invalido en listProfesorMateria(): ") + de.c_str();
    return false;
  }

  visitJsonItems(doc.as<JsonVariantConst>(), [&](JsonVariantConst item) {
    if (item.is<JsonObjectConst>()) {
      JsonObjectConst o = item.as<JsonObjectConst>();

      // Filtrar por UID del profesor antes de extraer la materia
      const char* uidKeys[] = {"uid", "UID", "id_profesor", "profesor_uid", "rfid"};
      String rowUid = jsonGetAny(o, uidKeys, sizeof(uidKeys) / sizeof(uidKeys[0]));
      if (rowUid.length() > 0 && rowUid != uid) {
        return; // No pertenece a este profesor, saltar
      }

      const char* matKeys[] = {"materia", "subject", "asignatura", "nombre", "nombre_materia"};
      String mat = jsonGetAny(o, matKeys, sizeof(matKeys) / sizeof(matKeys[0]));
      if (mat.length()) {
        appendUnique(mats, mat);
        return;
      }

      // Si trae un objeto de materias anidado
      if (o.containsKey("materias") && o["materias"].is<JsonArrayConst>()) {
        JsonArrayConst arr = o["materias"].as<JsonArrayConst>();
        for (JsonVariantConst m : arr) {
          String ms = jsonVariantToString(m);
          if (ms.length()) appendUnique(mats, ms);
        }
        return;
      }
    }

    // Si viene como string simple
    String s = jsonVariantToString(item);
    if (s.length()) {
      splitAndAppendMaterias(s, mats);
    }
  });

  return true;
}

static void alertServerDown() {
  Serial.println("ERROR: servidor no disponible. Se deniega lectura RFID.");
#ifdef USE_DISPLAY
  showTemporaryRedMessage("Servidor no disponible", 2500UL);
#endif
  ledOff();
}

// ------------------------------------------------------------
// Envío online בלבד
// ------------------------------------------------------------

static bool saveAttendanceOnline(
    const String &timestamp,
    const String &uid,
    const String &name,
    const String &account,
    const String &materia,
    const String &mode
) {
  if (!serverSeemsReady()) return false;

  bool sent = sendAsistencia(timestamp, uid, name, account, materia, mode);
  if (sent) {
    Serial.printf("OK: asistencia enviada a servidor UID=%s\n", uid.c_str());
  } else {
    Serial.printf("ERROR: no se pudo enviar asistencia UID=%s\n", uid.c_str());
  }
  return sent;
}

static bool saveDeniedOnline(
    const String &timestamp,
    const String &uid,
    const String &note
) {
  if (!serverSeemsReady()) return false;

  bool sent = sendAccesoDenegadoRegistro(timestamp, uid, note);
  if (sent) {
    Serial.printf("OK: acceso denegado enviado a servidor UID=%s\n", uid.c_str());
  } else {
    Serial.printf("ERROR: no se pudo enviar acceso denegado UID=%s\n", uid.c_str());
  }
  return sent;
}

static bool saveNotificationOnline(
    const String &timestamp,
    const String &uid,
    const String &name,
    const String &account,
    const String &note
) {
  if (!serverSeemsReady()) return false;

  bool sent = sendNotificacionRegistro(timestamp, uid, name, account, note);
  if (sent) {
    Serial.printf("OK: notificacion enviada a servidor UID=%s\n", uid.c_str());
  } else {
    Serial.printf("ERROR: no se pudo enviar notificacion UID=%s\n", uid.c_str());
  }
  return sent;
}

// ------------------------------------------------------------
// Handler principal para eventos RFID
// ------------------------------------------------------------
void rfidLoopHandler() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  Serial.printf("---- RFID event (%s) ----\n", nowISO().c_str());
  Serial.printf("Tarjeta detectada UID=%s\n", uid.c_str());

  // Si el servidor no está disponible, se bloquea todo
  if (!serverSeemsReady()) {
    alertServerDown();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Bloqueo si hay self-register en batch
  if (captureBatchMode && awaitingSelfRegister) {
    Serial.println("Lectura bloqueada: hay un auto-registro en curso. Ignorando tarjeta.");

    captureUID = uid;
    captureName = "";
    captureAccount = "";
    captureDetectedAt = now;

    showSelfRegisterBanner(String());
    showTemporaryRedMessage("Espere su turno: registro en curso", 2000UL);

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // MODO CAPTURA
  if (captureMode) {
    if (captureBatchMode) {
      // Sin cola local. Solo se evalúa en RAM.
      captureUID = uid;
      captureDetectedAt = now;
      Serial.printf("Batch capture: UID %s detectada.\n", uid.c_str());

      if (!awaitingSelfRegister) {
        ProfesorRow teacher;
        String errTeacher;
        bool teacherFound = fetchProfesorByUid(uid, teacher, errTeacher);
        if (!errTeacher.length() && teacherFound) {
          Serial.printf("UID %s detectada como PROFESOR durante batch -> bloquear y notificar\n", uid.c_str());

          String teacherName = teacher.nombre;
          String notificationMsg = "Tarjeta de profesor detectada en captura por lote y bloqueada: " +
                                   (teacherName.length() ? teacherName : String("Sin nombre")) +
                                   " (UID: " + uid + ")";

          saveDeniedOnline(nowISO(), uid, "PROFESOR_BLOQUEADO_LOTE");
          saveNotificationOnline(nowISO(), uid, teacherName, teacher.cuenta, notificationMsg);

          captureUID = uid;
          captureName = "";
          captureAccount = "";
          captureDetectedAt = now;

#ifdef USE_DISPLAY
          showTemporaryRedMessage("PROFESOR - NO PERMITIDO EN LOTE", 4000UL);
#endif

          mfrc522.PICC_HaltA();
          mfrc522.PCD_StopCrypto1();
          return;
        }

        // Si no es profesor y no existe como usuario, crear sesión de self-register
        std::vector<AlumnoRow> studentRows;
        String errStudents;
        bool studentsOk = fetchAlumnosByUid(uid, studentRows, errStudents);

        if (!studentsOk) {
          alertServerDown();
          mfrc522.PICC_HaltA();
          mfrc522.PCD_StopCrypto1();
          return;
        }

        if (studentRows.empty()) {
          SelfRegSession s;
          {
            uint32_t r = (uint32_t)esp_random();
            uint32_t m = (uint32_t)millis();
            char buf[32];
            snprintf(buf, sizeof(buf), "%08X%08X", r, m);
            s.token = String(buf);
          }
          s.uid = uid;
          s.createdAtMs = millis();
          s.ttlMs = SELF_REG_TIMEOUT_MS;
          s.materia = String();
          selfRegSessions.push_back(s);

          awaitingSelfRegister = true;
          currentSelfRegToken = s.token;
          currentSelfRegUID = uid;
          awaitingSinceMs = millis();

          String url = String("http://") + WiFi.localIP().toString() + String("/self_register?token=") + currentSelfRegToken;
          int boxSize = min(tft.width(), tft.height()) - 24;
          showQRCodeOnDisplay(url, boxSize);
          showSelfRegisterBanner(String());
        }
      }

      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }

    // capture individual
    if (captureUID.length() == 0 || (now - captureDetectedAt) > CAPTURE_DEBOUNCE_MS) {
      captureUID = uid;

      std::vector<AlumnoRow> studentRows;
      String errStudents;
      bool studentsOk = fetchAlumnosByUid(uid, studentRows, errStudents);

      if (!studentsOk) {
        alertServerDown();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
      }

      captureName = "";
      captureAccount = "";
      if (!studentRows.empty()) {
        captureName = studentRows[0].nombre;
        captureAccount = studentRows[0].cuenta;
      }

      captureDetectedAt = now;
      Serial.printf("Capture mode: UID=%s -> name='%s' acc='%s'\n",
                    captureUID.c_str(),
                    captureName.c_str(),
                    captureAccount.c_str());

      if (awaitingSelfRegister && currentSelfRegUID.length() > 0 && captureUID != currentSelfRegUID) {
        showTemporaryRedMessage("Espere su turno: registro en curso", 2000UL);
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      } else {
        showCaptureInProgress(false, captureUID);
      }
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // ------------------------------------------------------------
  // PROCESO NORMAL DE ACCESO
  // ------------------------------------------------------------

  // Leer registros del UID en base de datos
  std::vector<AlumnoRow> userRows;
  String errStudents;
  bool studentsOk = fetchAlumnosByUid(uid, userRows, errStudents);

  if (!studentsOk) {
    alertServerDown();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Revisar profesor
  ProfesorRow teacher;
  String errTeacher;
  bool isTeacher = fetchProfesorByUid(uid, teacher, errTeacher);

  if (errTeacher.length()) {
    alertServerDown();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Si no existe ni user ni teacher -> denegar
  if (userRows.size() == 0 && !isTeacher) {
    Serial.printf("UID %s no registrado -> DENEGADO\n", uid.c_str());
    saveDeniedOnline(nowISO(), uid, "NO_REGISTRADO");
    String note = "Tarjeta no registrada (UID: " + uid + ")";
    saveNotificationOnline(nowISO(), uid, "", "", note);
    showAccessDenied("Tarjeta no registrada", uid);
    ledOff();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Materia en horario actual
  String scheduleOwner = currentScheduledMateria(); // puede ser "Materia" o "Materia||Profesor"
  String scheduleBaseMat = baseMateriaFromOwner(scheduleOwner);
  scheduleBaseMat.trim();
  Serial.printf("Schedule base materia detectada: '%s'\n", scheduleBaseMat.c_str());

  // Detectar si owner incluye profesor (clave compuesta)
  String scheduleOwnerProf = "";
  bool scheduleOwnerHasProf = false;
  int soidx = scheduleOwner.indexOf("||");
  if (soidx >= 0) {
    scheduleOwnerProf = scheduleOwner.substring(soidx + 2);
    scheduleOwnerProf.trim();
    scheduleOwnerHasProf = true;
  }

  // Lógica para ALUMNOS
  if (userRows.size() > 0) {
    String name = userRows[0].nombre;
    String account = userRows[0].cuenta;

    std::vector<String> userMats;
    std::vector<String> userMatsLower;

    for (auto &r : userRows) {
      if (r.materia.length()) {
        String mm = normMat(r.materia);
        String mmLower = lowerCopy(mm);
        bool found = false;
        for (auto &ul : userMatsLower) {
          if (ul == mmLower) { found = true; break; }
        }
        if (!found) {
          userMats.push_back(mm);
          userMatsLower.push_back(mmLower);
        }
      }
    }

    if (scheduleBaseMat.length() > 0) {
      String wantMat = normMat(scheduleBaseMat);
      String wantMatLower = lowerCopy(wantMat);
      bool hasCurrent = false;

      for (auto &mmLower : userMatsLower) {
        if (mmLower == wantMatLower) { hasCurrent = true; break; }
      }

      if (hasCurrent) {
        String ts = nowISO();
        saveAttendanceOnline(ts, uid, name, account, wantMat, "entrada");
        puerta.write(90);
        showAccessGranted(name, wantMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String mmstr = joinMats(userMats);
        String note = "Intento fuera de materia en curso. Usuario: " + name + " (" + account + "). Materias del usuario: " + mmstr + ". Materia en curso: " + wantMat;
        saveNotificationOnline(nowISO(), uid, name, account, note);
        saveDeniedOnline(nowISO(), uid, "FUERA_DE_MATERIA");
        showAccessDenied(String("No pertenece a: ") + wantMat, uid);
        ledOff();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
      }
    } else {
      // NO HAY CLASE EN ESTE MOMENTO
      if (!userMats.empty()) {
        String chosenMat = userMats[0];
        String ts = nowISO();
        saveAttendanceOnline(ts, uid, name, account, chosenMat, "entrada");
        String note = "Entrada fuera de horario (Alumno). Usuario: " + name + " (" + account + "). Materia asignada: " + chosenMat;
        saveNotificationOnline(nowISO(), uid, name, account, note);
        puerta.write(90);
        showAccessGranted(name, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso sin materia asignada. UID: " + uid + " Nombre: " + name;
        saveNotificationOnline(nowISO(), uid, name, account, note);
        saveDeniedOnline(nowISO(), uid, "SIN_MATERIA");
        showAccessDenied("Sin materia asignada", uid);
        ledOff();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
      }
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Lógica para TEACHERS
  if (isTeacher) {
    String tname = teacher.nombre;
    String tacc = teacher.cuenta;

    // Obtener las materias registradas para este maestro
    std::vector<String> tmats;
    String errMats;
    bool matsOk = fetchMateriasForProfesorUid(uid, tmats, errMats);
    if (!matsOk) {
      alertServerDown();
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }

    if (scheduleBaseMat.length() > 0) {
      String wantMat = normMat(scheduleBaseMat);
      String wantMatLower = lowerCopy(wantMat);

      // Si el horario especifica profesor (clave compuesta "Materia||Profesor"),
      // sólo permitir acceso al profesor exacto que aparece en la clave.
      if (scheduleOwnerHasProf) {
        if (lowerCopy(tname) == lowerCopy(scheduleOwnerProf)) {
          String ts = nowISO();
          saveAttendanceOnline(ts, uid, tname, tacc, wantMat, "entrada-teacher");
          puerta.write(90);
          showAccessGranted(tname, wantMat, uid);
          puerta.write(0);
          ledOff();
        } else {
          String mmstr = joinMats(tmats);
          String note = "Intento fuera de materia en curso (teacher). Maestro: " + tname + " (" + tacc + "). Materias del maestro: " + mmstr + ". Materia en curso: " + scheduleOwner;
          saveNotificationOnline(nowISO(), uid, tname, tacc, note);
          saveDeniedOnline(nowISO(), uid, "NO_MATERIA_TEACHER");
          showAccessDenied(String("No asignado a: ") + wantMat, uid);
          ledOff();
          mfrc522.PICC_HaltA();
          mfrc522.PCD_StopCrypto1();
          return;
        }
      } else {
        bool hasCurrent = false;
        for (auto &m : tmats) {
          if (lowerCopy(m) == wantMatLower) { hasCurrent = true; break; }
        }

        if (hasCurrent) {
          String ts = nowISO();
          saveAttendanceOnline(ts, uid, tname, tacc, wantMat, "entrada-teacher");
          puerta.write(90);
          showAccessGranted(tname, wantMat, uid);
          puerta.write(0);
          ledOff();
        } else {
          String mmstr = joinMats(tmats);
          String note = "Intento fuera de materia en curso (teacher). Maestro: " + tname + " (" + tacc + "). Materias del maestro: " + mmstr + ". Materia en curso: " + wantMat;
          saveNotificationOnline(nowISO(), uid, tname, tacc, note);
          saveDeniedOnline(nowISO(), uid, "NO_MATERIA_TEACHER");
          showAccessDenied(String("No asignado a: ") + wantMat, uid);
          ledOff();
          mfrc522.PICC_HaltA();
          mfrc522.PCD_StopCrypto1();
          return;
        }
      }
    } else {
      // NO HAY CLASE: si maestro tiene materias, permitir y notificar
      if (!tmats.empty()) {
        String chosenMat = tmats[0];
        String ts = nowISO();
        saveAttendanceOnline(ts, uid, tname, tacc, chosenMat, "entrada-teacher");
        String note = "Entrada fuera de horario (Maestro). Maestro: " + tname + " (" + tacc + "). Materia: " + chosenMat;
        saveNotificationOnline(nowISO(), uid, tname, tacc, note);
        puerta.write(90);
        showAccessGranted(tname, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso (teacher) sin materias asignadas. UID: " + uid + " Nombre: " + tname;
        saveNotificationOnline(nowISO(), uid, tname, tacc, note);
        saveDeniedOnline(nowISO(), uid, "SIN_MATERIA_TEACHER");
        showAccessDenied("Sin materia asignada (teacher)", uid);
        ledOff();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
      }
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Fallback
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}