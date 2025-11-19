// src/rfid_handler.cpp
#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <MFRC522.h>
#include <WiFi.h>

#include "globals.h"
#include "files_utils.h"
#include "display.h"
#include "time_utils.h"
#include <ctype.h>

// helpers (baseMateriaFromOwner, normMat, joinMats, lowerCopy) ...
static String baseMateriaFromOwner(const String &owner) {
  int idx = owner.indexOf("||");
  if (idx < 0) { String o = owner; o.trim(); return o; }
  String b = owner.substring(0, idx); b.trim(); return b;
}
static String normMat(const String &s) { String t = s; t.trim(); return t; }
static String joinMats(const std::vector<String> &mats) {
  String out;
  for (size_t i = 0; i < mats.size(); ++i) {
    if (i) out += "; ";
    out += mats[i];
  }
  return out;
}
static String lowerCopy(const String &s) { String t = s; t.toLowerCase(); return t; }

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

void rfidLoopHandler() {
  if (awaitingSelfRegister) {
    unsigned long now = millis();
    if ((long)(now - awaitingSinceMs) > (long)SELF_REG_TIMEOUT_MS) {
      Serial.println("Self-register timeout -> liberando");
      awaitingSelfRegister = false;
      currentSelfRegToken = String();
      currentSelfRegUID = String();
      awaitingSinceMs = 0;
      showWaitingMessage();
    }
  }

  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  Serial.printf("---- RFID event (%s) ----\n", nowISO().c_str());
  Serial.printf("Tarjeta detectada UID=%s\n", uid.c_str());

  // CAPTURE modes
  if (captureMode) {
    if (captureBatchMode) {
      appendUidToQueueAvoidDup(uid);
      captureDetectedAt = now;
      Serial.printf("Batch capture: UID %s añadida a la cola.\n", uid.c_str());

      if (awaitingSelfRegister) {
        Serial.println("Ya hay un self-register en espera en el display; no generar otro QR.");
      } else {
        if (findAnyUserByUID(uid).length() == 0) {
          SelfRegSession s;
          uint32_t r = (uint32_t)esp_random();
          uint32_t m = (uint32_t)millis();
          char buf[32];
          snprintf(buf, sizeof(buf), "%08X%08X", r, m);
          s.token = String(buf);
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
          Serial.printf("Mostrando QR con URL: %s\n", url.c_str());
          int boxSize = min(tft.width(), tft.height()) - 40;
          showQRCodeOnDisplay(url, boxSize);
        } else {
          Serial.println("UID ya registrado, no se genera QR (solo agregar a cola).");
        }
      }

      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }

    // individual capture
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
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // flujo normal de acceso (leer USERS_FILE...)
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

  if (userRows.size() == 0) {
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

  String scheduleOwner = currentScheduledMateria();
  String scheduleBaseMat = baseMateriaFromOwner(scheduleOwner);
  scheduleBaseMat.trim();
  Serial.printf("Schedule base materia detectada: '%s'\n", scheduleBaseMat.c_str());

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
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // no hay clase -> permitir
  {
    String usedMat = (userMats.size() > 0 ? userMats[0] : "");
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + usedMat + "\"," + "\"entrada\"";
    appendLineToFile(ATT_FILE, rec);
    String note = "Entrada fuera de horario (no hay clase). Usuario: " + name + (usedMat.length() ? " Materia: " + usedMat : "");
    addNotification(uid, name, account, note);
    puerta.write(90);
    showAccessGranted(name, usedMat, uid);
    puerta.write(0);
    ledOff();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }
}
