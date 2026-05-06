// src/rfid_handler.cpp
#include <Arduino.h>
#include <SPI.h>
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
#include "display.h"
#include "time_utils.h"
#include "web/self_register.h"
#include "db_sync.h"

// ------------------------------------------------------------
// Helpers de formato
// ------------------------------------------------------------

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

static String normMat(const String &s) {
  String t = s;
  t.trim();
  return t;
}

static String joinMats(const std::vector<String> &mats) {
  String out;
  for (size_t i = 0; i < mats.size(); ++i) {
    if (i) out += "; ";
    out += mats[i];
  }
  return out;
}

static String lowerCopy(const String &s) {
  String t = s;
  t.toLowerCase();
  return t;
}

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

static bool uidAlreadyCaptured(const String &uid) {
  for (const auto &x : capturedUIDs) {
    if (x == uid) return true;
  }
  return false;
}

static void addUidToBatchQueue(const String &uid) {
  if (uid.length() == 0) return;
  if (!uidAlreadyCaptured(uid)) {
    capturedUIDs.push_back(uid);
  }
}

static std::vector<String> teacherMatsForUID(const String &uid) {
  std::vector<String> out;

  // 1) Intentar obtener el registro del maestro directamente desde backend
  String teacherRow = findTeacherByUID(uid);
  if (teacherRow.length()) {
    auto cols = parseQuotedCSVLine(teacherRow);

    // En muchos esquemas el campo de materia está en la columna 3 o 4
    if (cols.size() > 3) {
      String mat = cols[3];
      mat.trim();
      if (mat.length()) {
        bool found = false;
        for (auto &x : out) {
          if (x == mat) {
            found = true;
            break;
          }
        }
        if (!found) out.push_back(mat);
      }
    }

    // También se intenta resolver por nombre del profesor contra la lista de materias
    String teacherName;
    if (cols.size() > 1) {
      teacherName = cols[1];
      teacherName.trim();
    }

    if (teacherName.length()) {
      auto courses = loadCourses();
      for (auto &c : courses) {
        if (c.profesor == teacherName) {
          bool found = false;
          for (auto &x : out) {
            if (x == c.materia) {
              found = true;
              break;
            }
          }
          if (!found) out.push_back(c.materia);
        }
      }
    }
  }

  return out;
}

static bool parseUserRecordForUID(const String &uid, String &name, String &account, String &materia, String &rawRow) {
  rawRow = findAnyUserByUID(uid);
  if (!rawRow.length()) return false;

  auto cols = parseQuotedCSVLine(rawRow);
  if (cols.size() > 1) name = cols[1];
  if (cols.size() > 2) account = cols[2];
  if (cols.size() > 3) materia = cols[3];

  name.trim();
  account.trim();
  materia.trim();
  return true;
}

static bool parseTeacherRecordForUID(const String &uid, String &name, String &account, String &rawRow) {
  rawRow = findTeacherByUID(uid);
  if (!rawRow.length()) return false;

  auto cols = parseQuotedCSVLine(rawRow);
  if (cols.size() > 1) name = cols[1];
  if (cols.size() > 2) account = cols[2];

  name.trim();
  account.trim();
  return true;
}

// ------------------------------------------------------------
// Helpers de persistencia / envío
// ------------------------------------------------------------

static bool saveAttendanceSmart(
    const String &timestamp,
    const String &uid,
    const String &name,
    const String &account,
    const String &materia,
    const String &mode
) {
  bool sent = sendAsistencia(timestamp, uid, name, account, materia, mode);
  if (sent) {
    Serial.printf("OK: asistencia enviada a backend UID=%s\n", uid.c_str());
    return true;
  }

  Serial.printf("ERROR: no se pudo enviar asistencia al backend UID=%s\n", uid.c_str());
  return false;
}

static bool saveDeniedSmart(
    const String &timestamp,
    const String &uid,
    const String &note
) {
  bool sent = sendAccesoDenegadoRegistro(timestamp, uid, note);
  if (sent) {
    Serial.printf("OK: acceso denegado enviado a backend UID=%s\n", uid.c_str());
    return true;
  }

  Serial.printf("ERROR: no se pudo enviar denegado al backend UID=%s\n", uid.c_str());
  return false;
}

static bool saveNotificationSmart(
    const String &timestamp,
    const String &uid,
    const String &name,
    const String &account,
    const String &note
) {
  bool sent = sendNotificacionRegistro(timestamp, uid, name, account, note);
  if (sent) {
    Serial.printf("OK: notificacion enviada a backend UID=%s\n", uid.c_str());
    return true;
  }

  Serial.printf("ERROR: no se pudo enviar notificacion al backend UID=%s\n", uid.c_str());
  return false;
}

// ------------------------------------------------------------
// Handler principal para eventos RFID
// ------------------------------------------------------------
void rfidLoopHandler() {
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  Serial.printf("---- RFID event (%s) ----\n", nowISO().c_str());
  Serial.printf("Tarjeta detectada UID=%s\n", uid.c_str());

  // Bloqueo si hay self-register en curso
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

  // ------------------------------------------------------------
  // MODO CAPTURA
  // ------------------------------------------------------------
  if (captureMode) {
    if (captureBatchMode) {
      addUidToBatchQueue(uid);
      captureDetectedAt = now;
      Serial.printf("Batch capture: UID %s agregada a memoria temporal.\n", uid.c_str());

      if (!awaitingSelfRegister) {
        String teacherRow = findTeacherByUID(uid);
        if (teacherRow.length() > 0) {
          Serial.printf("UID %s detectada como MAESTRO durante batch -> bloquear y notificar\n", uid.c_str());

          String teacherName;
          auto tc = parseQuotedCSVLine(teacherRow);
          if (tc.size() > 1) teacherName = tc[1];

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

    // Capture individual
    if (captureUID.length() == 0 || (now - captureDetectedAt) > CAPTURE_DEBOUNCE_MS) {
      captureUID = uid;

      String name, account, materia, rawRow;
      if (parseUserRecordForUID(uid, name, account, materia, rawRow)) {
        captureName = name;
        captureAccount = account;
      } else {
        captureName = "";
        captureAccount = "";
      }

      captureDetectedAt = now;
      Serial.printf("Capture mode: UID=%s -> name='%s' acc='%s'\n",
                    captureUID.c_str(), captureName.c_str(), captureAccount.c_str());

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

  String userName;
  String userAccount;
  String userMateria;
  String userRow;
  bool isUser = parseUserRecordForUID(uid, userName, userAccount, userMateria, userRow);

  String teacherName;
  String teacherAccount;
  String teacherRow;
  bool isTeacher = parseTeacherRecordForUID(uid, teacherName, teacherAccount, teacherRow);

  if (!isUser && !isTeacher) {
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

  String scheduleOwner = currentScheduledMateria();
  String scheduleBaseMat = baseMateriaFromOwner(scheduleOwner);
  scheduleBaseMat.trim();
  Serial.printf("Schedule base materia detectada: '%s'\n", scheduleBaseMat.c_str());

  String scheduleOwnerProf = "";
  bool scheduleOwnerHasProf = false;
  int soidx = scheduleOwner.indexOf("||");
  if (soidx >= 0) {
    scheduleOwnerProf = scheduleOwner.substring(soidx + 2);
    scheduleOwnerProf.trim();
    scheduleOwnerHasProf = true;
  }

  // ------------------------------------------------------------
  // LÓGICA ALUMNOS
  // ------------------------------------------------------------
  if (isUser) {
    if (scheduleBaseMat.length() > 0) {
      String wantMat = normMat(scheduleBaseMat);

      // Validación directa contra backend
      if (existsUserUidMateria(uid, wantMat)) {
        String ts = nowISO();
        saveAttendanceSmart(ts, uid, userName, userAccount, wantMat, "entrada");
        puerta.write(90);
        showAccessGranted(userName, wantMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento fuera de materia en curso. Usuario: " + userName +
                      " (" + userAccount + "). Materia en curso: " + wantMat;
        saveNotificationSmart(nowISO(), uid, userName, userAccount, note);
        saveDeniedSmart(nowISO(), uid, "FUERA_DE_MATERIA");
        showAccessDenied(String("No pertenece a: ") + wantMat, uid);
        ledOff();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
      }
    } else {
      if (userMateria.length() > 0) {
        String chosenMat = normMat(userMateria);
        String ts = nowISO();
        saveAttendanceSmart(ts, uid, userName, userAccount, chosenMat, "entrada");
        String note = "Entrada fuera de horario (Alumno). Usuario: " + userName +
                      " (" + userAccount + "). Materia asignada: " + chosenMat;
        saveNotificationSmart(nowISO(), uid, userName, userAccount, note);
        puerta.write(90);
        showAccessGranted(userName, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso sin materia asignada. UID: " + uid +
                      " Nombre: " + userName;
        saveNotificationSmart(nowISO(), uid, userName, userAccount, note);
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

  // ------------------------------------------------------------
  // LÓGICA TEACHERS
  // ------------------------------------------------------------
  if (isTeacher) {
    std::vector<String> tmats = teacherMatsForUID(uid);

    if (scheduleBaseMat.length() > 0) {
      String wantMat = normMat(scheduleBaseMat);
      String wantMatLower = lowerCopy(wantMat);

      if (scheduleOwnerHasProf) {
        if (lowerCopy(teacherName) == lowerCopy(scheduleOwnerProf)) {
          String ts = nowISO();
          saveAttendanceSmart(ts, uid, teacherName, teacherAccount, wantMat, "entrada-teacher");
          puerta.write(90);
          showAccessGranted(teacherName, wantMat, uid);
          puerta.write(0);
          ledOff();
        } else {
          String mmstr = joinMats(tmats);
          String note = "Intento fuera de materia en curso (teacher). Maestro: " + teacherName +
                        " (" + teacherAccount + "). Materias del maestro: " + mmstr +
                        ". Materia en curso: " + scheduleOwner;
          saveNotificationSmart(nowISO(), uid, teacherName, teacherAccount, note);
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
          if (lowerCopy(m) == wantMatLower) {
            hasCurrent = true;
            break;
          }
        }

        if (hasCurrent) {
          String ts = nowISO();
          saveAttendanceSmart(ts, uid, teacherName, teacherAccount, wantMat, "entrada-teacher");
          puerta.write(90);
          showAccessGranted(teacherName, wantMat, uid);
          puerta.write(0);
          ledOff();
        } else {
          String mmstr = joinMats(tmats);
          String note = "Intento fuera de materia en curso (teacher). Maestro: " + teacherName +
                        " (" + teacherAccount + "). Materias del maestro: " + mmstr +
                        ". Materia en curso: " + wantMat;
          saveNotificationSmart(nowISO(), uid, teacherName, teacherAccount, note);
          saveDeniedSmart(nowISO(), uid, "NO_MATERIA_TEACHER");
          showAccessDenied(String("No asignado a: ") + wantMat, uid);
          ledOff();
          mfrc522.PICC_HaltA();
          mfrc522.PCD_StopCrypto1();
          return;
        }
      }
    } else {
      if (!tmats.empty()) {
        String chosenMat = tmats[0];
        String ts = nowISO();
        saveAttendanceSmart(ts, uid, teacherName, teacherAccount, chosenMat, "entrada-teacher");
        String note = "Entrada fuera de horario (Maestro). Maestro: " + teacherName +
                      " (" + teacherAccount + "). Materia: " + chosenMat;
        saveNotificationSmart(nowISO(), uid, teacherName, teacherAccount, note);
        puerta.write(90);
        showAccessGranted(teacherName, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso (teacher) sin materias asignadas. UID: " + uid +
                      " Nombre: " + teacherName;
        saveNotificationSmart(nowISO(), uid, teacherName, teacherAccount, note);
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