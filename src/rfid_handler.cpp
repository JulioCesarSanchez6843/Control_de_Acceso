// src/rfid_handler.cpp
#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <vector>

#include "globals.h"
#include "files_utils.h"
#include "display.h"
#include "time_utils.h"
#include "web/self_register.h"
#include <ctype.h>

// Extrae la parte "materia" si owner viene como "Materia||Profesor"
static String baseMateriaFromOwner(const String &owner) {
  int idx = owner.indexOf("||");
  if (idx < 0) {
    String o = owner; o.trim();
    return o;
  }
  String b = owner.substring(0, idx);
  b.trim();
  return b;
}

// Normaliza una materia (quita espacios al inicio/fin)
static String normMat(const String &s) {
  String t = s; t.trim(); return t;
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

// Helper local: intenta añadir UID a CAPTURE_QUEUE_FILE evitando duplicados simples
static void appendUidToQueueAvoidDup(const String &uid) {
  if (uid.length() == 0) return;
  const char *QFILE = CAPTURE_QUEUE_FILE;
  bool exists = false;
  if (SPIFFS.exists(QFILE)) {
    File f = SPIFFS.open(QFILE, FILE_READ);
    if (f) {
      while (f.available()) {
        String l = f.readStringUntil('\n'); l.trim();
        if (l.length() > 0 && l == uid) { exists = true; break; }
      }
      f.close();
    }
  }
  if (!exists) appendLineToFile(QFILE, uid);
}

// Helper: obtiene lista única de materias asociadas a un teacher (por uid y por courses)
static std::vector<String> teacherMatsForUID(const String &uid) {
  std::vector<String> out;
  // Desde TEACHERS_FILE por uid
  File f = SPIFFS.open(TEACHERS_FILE, FILE_READ);
  if (f) {
    String header = f.readStringUntil('\n'); (void)header;
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
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

// Handler principal para eventos RFID:
void rfidLoopHandler() {
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
      appendUidToQueueAvoidDup(uid);
      captureDetectedAt = now;
      Serial.printf("Batch capture: UID %s añadida a la cola.\n", uid.c_str());

      if (!awaitingSelfRegister) {
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
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
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
    String recDenied = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"NO REGISTRADO\"";
    appendLineToFile(DENIED_FILE, recDenied);
    String note = "Tarjeta no registrada (UID: " + uid + ")";
    addNotification(uid, String(""), String(""), note);
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
      // Hay clase en curso; verificar pertenencia
      String wantMat = normMat(scheduleBaseMat);
      String wantMatLower = lowerCopy(wantMat);
      bool hasCurrent = false;
      for (auto &mmLower : userMatsLower) {
        if (mmLower == wantMatLower) { hasCurrent = true; break; }
      }

      if (hasCurrent) {
        String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + wantMat + "\"," + "\"entrada\"";
        appendLineToFile(ATT_FILE, rec);
        puerta.write(90);
        showAccessGranted(name, wantMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String mmstr = joinMats(userMats);
        String note = "Intento fuera de materia en curso. Usuario: " + name + " (" + account + "). Materias del usuario: " + mmstr + ". Materia en curso: " + wantMat;
        addNotification(uid, name, account, note);
        String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + note + "\"";
        appendLineToFile(DENIED_FILE, rec);
        showAccessDenied(String("No pertenece a: ") + wantMat, uid);
        ledOff();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
      }
    } else {
      // NO hay clase en el horario actual -> permitimos entrada pero GENERAMOS notificación
      if (!userMats.empty()) {
        String chosenMat = userMats[0];
        String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMat + "\"," + "\"entrada\"";
        appendLineToFile(ATT_FILE, rec);
        // NOTIFICACIÓN: entrada fuera de horario (alumno)
        String note = "Entrada fuera de horario: Usuario: " + name + " (" + account + "). Materia registrada: " + chosenMat;
        addNotification(uid, name, account, note);
        puerta.write(90);
        showAccessGranted(name, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        // Sin materia asignada -> denegar y notificar
        String note = "Intento de acceso sin materia asignada. UID: " + uid + " Nombre: " + (userRows.size() ? (userRows[0].size()>1?userRows[0][1]:"") : "");
        addNotification(uid, String(""), String(""), note);
        appendLineToFile(DENIED_FILE, String("\"") + nowISO() + String("\",\"") + uid + String("\",\"NO MATERIA\""));
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
      // Hay una materia en curso en schedule -> verificar si el teacher corresponde
      String wantMat = normMat(scheduleBaseMat);
      String wantMatLower = lowerCopy(wantMat);
      bool hasCurrent = false;
      for (auto &m : tmats) {
        if (lowerCopy(m) == wantMatLower) { hasCurrent = true; break; }
      }
      if (hasCurrent) {
        String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + tname + "\"," + "\"" + tacc + "\"," + "\"" + wantMat + "\"," + "\"entrada-teacher\"";
        appendLineToFile(ATT_FILE, rec);
        puerta.write(90);
        showAccessGranted(tname, wantMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String mmstr = joinMats(tmats);
        String note = "Intento fuera de materia en curso (teacher). Maestro: " + tname + " (" + tacc + "). Materias del maestro: " + mmstr + ". Materia en curso: " + wantMat;
        addNotification(uid, tname, tacc, note);
        appendLineToFile(DENIED_FILE, String("\"") + nowISO() + String("\",\"") + uid + String("\",\"NO MATERIA TEACHER\""));
        showAccessDenied(String("No asignado a: ") + wantMat, uid);
        ledOff();
        mfrc522.PICC_HaltA();
        mfrc522.PCD_StopCrypto1();
        return;
      }
    } else {
      // NO hay clase en horario: permitir entrada PERO notificar (maestro entró fuera de horario)
      if (!tmats.empty()) {
        String chosenMat = tmats[0];
        String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + tname + "\"," + "\"" + tacc + "\"," + "\"" + chosenMat + "\"," + "\"entrada-teacher\"";
        appendLineToFile(ATT_FILE, rec);
        // Notificación: maestro entrando fuera de horario
        String note = "Entrada fuera de horario (maestro): " + tname + " (" + tacc + "). Materia asociada: " + chosenMat;
        addNotification(uid, tname, tacc, note);
        puerta.write(90);
        showAccessGranted(tname, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso (teacher) sin materias asignadas. UID: " + uid + " Nombre: " + tname;
        addNotification(uid, tname, tacc, note);
        appendLineToFile(DENIED_FILE, String("\"") + nowISO() + String("\",\"") + uid + String("\",\"NO MATERIA TEACHER\""));
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
