// src/rfid_handler.cpp
#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <vector>
#include <ctype.h>

#if defined(ARDUINO_ARCH_ESP32)
  #include <ESP32Servo.h>
#else
  #include <Servo.h>
#endif

#include "globals.h"
#include "files_utils.h"
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

// Normaliza una materia (quita espacios al inicio/fin)
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

// Evita comillas dobles dentro de CSV local
static String csvSafe(String s) {
  s.replace("\"", "'");
  return s;
}

static String csvLine3(const String &a, const String &b, const String &c) {
  return "\"" + csvSafe(a) + "\",\"" + csvSafe(b) + "\",\"" + csvSafe(c) + "\"";
}

static String csvLine5(const String &a, const String &b, const String &c, const String &d, const String &e) {
  return "\"" + csvSafe(a) + "\",\"" + csvSafe(b) + "\",\"" + csvSafe(c) + "\",\"" + csvSafe(d) + "\",\"" + csvSafe(e) + "\"";
}

static String csvLine6(const String &a, const String &b, const String &c, const String &d, const String &e, const String &f) {
  return "\"" + csvSafe(a) + "\",\"" + csvSafe(b) + "\",\"" + csvSafe(c) + "\",\"" + csvSafe(d) + "\",\"" + csvSafe(e) + "\",\"" + csvSafe(f) + "\"";
}

// Helper local: intenta añadir UID a CAPTURE_QUEUE_FILE evitando duplicados simples
static void appendUidToQueueAvoidDup(const String &uid) {
  if (uid.length() == 0) return;
  const char *QFILE = CAPTURE_QUEUE_FILE;
  bool exists = false;

  if (SPIFFS.exists(QFILE)) {
    File f = SPIFFS.open(QFILE, FILE_READ);
    if (f) {
      while (f.available()) {
        String l = f.readStringUntil('\n');
        l.trim();
        if (l.length() > 0 && l == uid) {
          exists = true;
          break;
        }
      }
      f.close();
    }
  }

  if (!exists) appendLineToFile(QFILE, uid);
}

// Helper: obtiene lista única de materias asociadas a un teacher (por uid y por courses)
static std::vector<String> teacherMatsForUID(const String &uid) {
  std::vector<String> out;

  // Desde TEACHERS_FILE por uid (si el registro contiene columna de materia)
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (f) {
    String header = f.readStringUntil('\n');
    (void)header;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() >= 4) {
        String rowUid = c[0];
        String mat = c[3];
        if (rowUid == uid && mat.length()) {
          bool found = false;
          for (auto &x : out) if (x == mat) { found = true; break; }
          if (!found) out.push_back(mat);
        }
      }
    }
    f.close();
  }

  // Además, buscar en courses por nombre de profesor (si existe)
  String teacherName;
  String trec = findTeacherByUID(uid);
  if (trec.length()) {
    auto cc = parseQuotedCSVLine(trec);
    if (cc.size() > 1) teacherName = cc[1];
  }

  if (teacherName.length()) {
    auto courses = loadCourses();
    for (auto &c : courses) {
      if (c.profesor == teacherName) {
        bool found = false;
        for (auto &x : out) if (x == c.materia) { found = true; break; }
        if (!found) out.push_back(c.materia);
      }
    }
  }
  return out;
}

// ------------------------------------------------------------
// Helpers de persistencia / envío
// ------------------------------------------------------------

static bool serverSeemsReady() {
  return (WiFi.status() == WL_CONNECTED) && pingServer();
}

static bool saveAttendanceSmart(
    const String &timestamp,
    const String &uid,
    const String &name,
    const String &account,
    const String &materia,
    const String &mode
) {
  // En modo online intenta Oracle primero.
  // Solo si falla, guarda en SPIFFS.
  if (!modoLocal) {
    bool sent = sendAsistencia(timestamp, uid, name, account, materia, mode);

    if (sent) {
      Serial.printf("OK: asistencia enviada a Oracle UID=%s\n", uid.c_str());
      return true;
    }

    Serial.println("Oracle fallo en runtime, guardando en SPIFFS");
    String rec = csvLine6(timestamp, uid, name, account, materia, mode);
    appendLineToFile(ATT_FILE, rec);
    return false;
  }

  // Modo local: solo SPIFFS
  String rec = csvLine6(timestamp, uid, name, account, materia, mode);
  appendLineToFile(ATT_FILE, rec);
  Serial.printf("LOCAL: asistencia guardada en SPIFFS UID=%s\n", uid.c_str());
  return false;
}

static bool saveDeniedSmart(
    const String &timestamp,
    const String &uid,
    const String &note
) {
  if (!modoLocal) {
    bool sent = sendAccesoDenegadoRegistro(timestamp, uid, note);

    if (sent) {
      Serial.printf("OK: acceso denegado enviado a Oracle UID=%s\n", uid.c_str());
      return true;
    }

    Serial.println("Oracle fallo (denegado), guardando en SPIFFS");
    String rec = csvLine3(timestamp, uid, note);
    appendLineToFile(DENIED_FILE, rec);
    return false;
  }

  String rec = csvLine3(timestamp, uid, note);
  appendLineToFile(DENIED_FILE, rec);
  Serial.printf("LOCAL: acceso denegado guardado en SPIFFS UID=%s\n", uid.c_str());
  return false;
}

static bool saveNotificationSmart(
    const String &timestamp,
    const String &uid,
    const String &name,
    const String &account,
    const String &note
) {
  if (!modoLocal) {
    bool sent = sendNotificacionRegistro(timestamp, uid, name, account, note);

    if (sent) {
      Serial.printf("OK: notificacion enviada a Oracle UID=%s\n", uid.c_str());
      return true;
    }

    Serial.println("Oracle fallo (notif), guardando en SPIFFS");
    String rec = csvLine5(timestamp, uid, name, account, note);
    appendLineToFile(NOTIF_FILE, rec);
    return false;
  }

  String rec = csvLine5(timestamp, uid, name, account, note);
  appendLineToFile(NOTIF_FILE, rec);
  Serial.printf("LOCAL: notificacion guardada en SPIFFS UID=%s\n", uid.c_str());
  return false;
}

static bool csvHasDataRows(const char *path) {
  if (!SPIFFS.exists(path)) return false;

  File f = SPIFFS.open(path, FILE_READ);
  if (!f) return false;

  if (f.available()) {
    String header = f.readStringUntil('\n');
    (void)header;
  }

  while (f.available()) {
    String l = f.readStringUntil('\n');
    l.trim();
    if (l.length() > 0) {
      f.close();
      return true;
    }
  }

  f.close();
  return false;
}

static bool syncPendingAttendanceFile() {
  if (!SPIFFS.exists(ATT_FILE)) return true;

  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  if (!f) return false;

  String header = f.readStringUntil('\n');
  std::vector<String> remaining;
  bool hadRows = false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    hadRows = true;

    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 6) {
      if (!sendAsistencia(c[0], c[1], c[2], c[3], c[4], c[5])) {
        remaining.push_back(line);
      }
    } else {
      remaining.push_back(line);
    }
  }
  f.close();

  if (!hadRows) return true;

  if (remaining.empty()) {
    SPIFFS.remove(ATT_FILE);
    File out = SPIFFS.open(ATT_FILE, FILE_WRITE);
    if (out) {
      out.println(header);
      out.close();
    }
    return true;
  }

  std::vector<String> outLines;
  outLines.push_back(header);
  for (auto &ln : remaining) outLines.push_back(ln);
  return writeAllLines(ATT_FILE, outLines);
}

static bool syncPendingDeniedFile() {
  if (!SPIFFS.exists(DENIED_FILE)) return true;

  File f = SPIFFS.open(DENIED_FILE, FILE_READ);
  if (!f) return false;

  String header = f.readStringUntil('\n');
  std::vector<String> remaining;
  bool hadRows = false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    hadRows = true;

    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 3) {
      if (!sendAccesoDenegadoRegistro(c[0], c[1], c[2])) {
        remaining.push_back(line);
      }
    } else {
      remaining.push_back(line);
    }
  }
  f.close();

  if (!hadRows) return true;

  if (remaining.empty()) {
    SPIFFS.remove(DENIED_FILE);
    File out = SPIFFS.open(DENIED_FILE, FILE_WRITE);
    if (out) {
      out.println(header);
      out.close();
    }
    return true;
  }

  std::vector<String> outLines;
  outLines.push_back(header);
  for (auto &ln : remaining) outLines.push_back(ln);
  return writeAllLines(DENIED_FILE, outLines);
}

static bool syncPendingNotificationsFile() {
  if (!SPIFFS.exists(NOTIF_FILE)) return true;

  File f = SPIFFS.open(NOTIF_FILE, FILE_READ);
  if (!f) return false;

  String header = f.readStringUntil('\n');
  std::vector<String> remaining;
  bool hadRows = false;

  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    hadRows = true;

    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 5) {
      if (!sendNotificacionRegistro(c[0], c[1], c[2], c[3], c[4])) {
        remaining.push_back(line);
      }
    } else {
      remaining.push_back(line);
    }
  }
  f.close();

  if (!hadRows) return true;

  if (remaining.empty()) {
    SPIFFS.remove(NOTIF_FILE);
    File out = SPIFFS.open(NOTIF_FILE, FILE_WRITE);
    if (out) {
      out.println(header);
      out.close();
    }
    return true;
  }

  std::vector<String> outLines;
  outLines.push_back(header);
  for (auto &ln : remaining) outLines.push_back(ln);
  return writeAllLines(NOTIF_FILE, outLines);
}

// ------------------------------------------------------------
// Sincronización pública — llamada desde main.cpp en boot
// y periódicamente desde rfidLoopHandler()
// ------------------------------------------------------------
void syncPendingToServer() {
  static unsigned long lastAttemptMs = 0;
  const unsigned long SYNC_CHECK_INTERVAL_MS = 15000UL;

  if (millis() - lastAttemptMs < SYNC_CHECK_INTERVAL_MS) return;
  lastAttemptMs = millis();

  if (!csvHasDataRows(ATT_FILE) && !csvHasDataRows(DENIED_FILE) && !csvHasDataRows(NOTIF_FILE)) {
    return;
  }

  if (!serverSeemsReady()) {
    Serial.println("SYNC: servidor aun no disponible.");
    return;
  }

  Serial.println("SYNC: servidor disponible, intentando subir pendientes de SPIFFS...");

  syncPendingAttendanceFile();
  syncPendingDeniedFile();
  syncPendingNotificationsFile();

  if (!csvHasDataRows(ATT_FILE) && !csvHasDataRows(DENIED_FILE) && !csvHasDataRows(NOTIF_FILE)) {
    Serial.println("SYNC: todos los pendientes sincronizados correctamente.");
    modoLocal = false; // reconectado y limpio: volver a modo online
  } else {
    Serial.println("SYNC: aun quedan pendientes en SPIFFS.");
  }
}

// ------------------------------------------------------------
// Handler principal para eventos RFID
// ------------------------------------------------------------
void rfidLoopHandler() {
  // Intento de sincronizar pendientes en segundo plano
  syncPendingToServer(); // <-- CORREGIDO: antes llamaba syncPendingSpiffsIfPossible()

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  Serial.printf("---- RFID event (%s) ----\n", nowISO().c_str());
  Serial.printf("Tarjeta detectada UID=%s\n", uid.c_str());

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
      // Añadir a la cola (evita duplicados)
      appendUidToQueueAvoidDup(uid);
      captureDetectedAt = now;
      Serial.printf("Batch capture: UID %s añadida a la cola.\n", uid.c_str());

      if (!awaitingSelfRegister) {
        // Si UID pertenece a un maestro: bloquear visiblemente y notificar
        if (findTeacherByUID(uid).length() > 0) {
          Serial.printf("UID %s detectada como MAESTRO durante batch -> bloquear y notificar al dashboard\n", uid.c_str());

          String teacherRow = findTeacherByUID(uid);
          String teacherName = "";
          if (teacherRow.length()) {
            auto tc = parseQuotedCSVLine(teacherRow);
            if (tc.size() > 1) teacherName = tc[1];
          }

          String notificationMsg = "Tarjeta de maestro detectada en captura por lote y bloqueada: " +
                                   (teacherName.length() ? teacherName : String("Sin nombre")) +
                                   " (UID: " + uid + ")";

          saveDeniedSmart(nowISO(), uid, "MAESTRO_BLOQUEADO_LOTE");
          saveNotificationSmart(nowISO(), uid, teacherName, "", notificationMsg);

          captureUID = uid;
          captureName = "";
          captureAccount = "";
          captureDetectedAt = now;

          #ifdef USE_DISPLAY
          showTemporaryRedMessage("MAESTRO - NO PERMITIDO EN LOTE", 4000UL);
          #endif

          mfrc522.PICC_HaltA();
          mfrc522.PCD_StopCrypto1();
          return;
        }

        // Si NO es maestro y no existe en usuarios, crear sesión de self-register
        if (findAnyUserByUID(uid).length() == 0) {
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
      String found = findAnyUserByUID(uid);
      captureName = "";
      captureAccount = "";
      if (found.length() > 0) {
        auto c = parseQuotedCSVLine(found);
        captureName = (c.size() > 1 ? c[1] : "");
        captureAccount = (c.size() > 2 ? c[2] : "");
      }
      captureDetectedAt = now;
      Serial.printf("Capture mode: UID=%s -> name='%s' acc='%s'\n", captureUID.c_str(), captureName.c_str(), captureAccount.c_str());
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

  // PROCESO NORMAL DE ACCESO

  // Leer registros del UID en USERS_FILE
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<std::vector<String>> userRows;
  if (f) {
    String header = f.readStringUntil('\n');
    (void)header;
    while (f.available()) {
      String l = f.readStringUntil('\n');
      l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size() > 0 && c[0] == uid) userRows.push_back(c);
    }
    f.close();
  } else {
    Serial.println("WARN: no se pudo abrir USERS_FILE (SPIFFS).");
  }

  // Revisar TEACHERS_FILE
  String teacherRow = findTeacherByUID(uid);
  bool isTeacher = (teacherRow.length() > 0);

  // Si no existe ni user ni teacher -> denegar
  if (userRows.size() == 0 && !isTeacher) {
    Serial.printf("UID %s no registrado -> DENEGADO\n", uid.c_str());
    saveDeniedSmart(nowISO(), uid, "NO_REGISTRADO");
    String note = "Tarjeta no registrada (UID: " + uid + ")";
    saveNotificationSmart(nowISO(), uid, "", "", note);
    showAccessDenied("Tarjeta no registrada", uid);
    ledOff();
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Materia en horario actual
  String scheduleOwner = currentScheduledMateria();
  String scheduleBaseMat = baseMateriaFromOwner(scheduleOwner);
  scheduleBaseMat.trim();
  Serial.printf("Schedule base materia detectada: '%s'\n", scheduleBaseMat.c_str());

  // Detectar si owner incluye profesor (clave compuesta "Materia||Profesor")
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
    String name = (userRows.size() > 0 && userRows[0].size() > 1 ? userRows[0][1] : "");
    String account = (userRows.size() > 0 && userRows[0].size() > 2 ? userRows[0][2] : "");

    std::vector<String> userMats;
    std::vector<String> userMatsLower;
    for (auto &r : userRows) {
      if (r.size() > 3) {
        String mm = normMat(r[3]);
        String mmLower = lowerCopy(mm);
        bool found = false;
        for (auto &ul : userMatsLower) if (ul == mmLower) { found = true; break; }
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
        saveAttendanceSmart(ts, uid, name, account, wantMat, "entrada");
        puerta.write(90);
        showAccessGranted(name, wantMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String mmstr = joinMats(userMats);
        String note = "Intento fuera de materia en curso. Usuario: " + name + " (" + account + "). Materias del usuario: " + mmstr + ". Materia en curso: " + wantMat;
        saveNotificationSmart(nowISO(), uid, name, account, note);
        saveDeniedSmart(nowISO(), uid, "FUERA_DE_MATERIA");
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
        saveAttendanceSmart(ts, uid, name, account, chosenMat, "entrada");
        String note = "Entrada fuera de horario (Alumno). Usuario: " + name + " (" + account + "). Materia asignada: " + chosenMat;
        saveNotificationSmart(nowISO(), uid, name, account, note);
        puerta.write(90);
        showAccessGranted(name, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso sin materia asignada. UID: " + uid + " Nombre: " + (userRows.size() ? (userRows[0].size() > 1 ? userRows[0][1] : "") : "");
        saveNotificationSmart(nowISO(), uid, "", "", note);
        saveDeniedSmart(nowISO(), uid, "SIN_MATERIA");
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
    auto cols = parseQuotedCSVLine(teacherRow);
    String tname = (cols.size() > 1 ? cols[1] : "");
    String tacc = (cols.size() > 2 ? cols[2] : "");

    std::vector<String> tmats = teacherMatsForUID(uid);

    if (scheduleBaseMat.length() > 0) {
      String wantMat = normMat(scheduleBaseMat);
      String wantMatLower = lowerCopy(wantMat);

      if (scheduleOwnerHasProf) {
        if (lowerCopy(tname) == lowerCopy(scheduleOwnerProf)) {
          String ts = nowISO();
          saveAttendanceSmart(ts, uid, tname, tacc, wantMat, "entrada-teacher");
          puerta.write(90);
          showAccessGranted(tname, wantMat, uid);
          puerta.write(0);
          ledOff();
        } else {
          String mmstr = joinMats(tmats);
          String note = "Intento fuera de materia en curso (teacher). Maestro: " + tname + " (" + tacc + "). Materias del maestro: " + mmstr + ". Materia en curso: " + scheduleOwner;
          saveNotificationSmart(nowISO(), uid, tname, tacc, note);
          saveDeniedSmart(nowISO(), uid, "NO_MATERIA_TEACHER");
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
          saveAttendanceSmart(ts, uid, tname, tacc, wantMat, "entrada-teacher");
          puerta.write(90);
          showAccessGranted(tname, wantMat, uid);
          puerta.write(0);
          ledOff();
        } else {
          String mmstr = joinMats(tmats);
          String note = "Intento fuera de materia en curso (teacher). Maestro: " + tname + " (" + tacc + "). Materias del maestro: " + mmstr + ". Materia en curso: " + wantMat;
          saveNotificationSmart(nowISO(), uid, tname, tacc, note);
          saveDeniedSmart(nowISO(), uid, "NO_MATERIA_TEACHER");
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
        saveAttendanceSmart(ts, uid, tname, tacc, chosenMat, "entrada-teacher");
        String note = "Entrada fuera de horario (Maestro). Maestro: " + tname + " (" + tacc + "). Materia: " + chosenMat;
        saveNotificationSmart(nowISO(), uid, tname, tacc, note);
        puerta.write(90);
        showAccessGranted(tname, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso (teacher) sin materias asignadas. UID: " + uid + " Nombre: " + tname;
        saveNotificationSmart(nowISO(), uid, tname, tacc, note);
        saveDeniedSmart(nowISO(), uid, "SIN_MATERIA_TEACHER");
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