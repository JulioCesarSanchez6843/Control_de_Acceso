#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <MFRC522.h>
#include "globals.h"
#include "files_utils.h"
#include "display.h" // si tienes funciones ledOff/ledRedOn/ledGreenOn aquí

// Nota: nowISO() está declarado en globals.h e implementado en src/time_utils.cpp
// Asegúrate de NO definir nowISO() aquí (eso causó la duplicación).

void rfidLoopHandler() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;
  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  if (captureMode) {
    if (captureUID.length() == 0 || (now - captureDetectedAt) > CAPTURE_DEBOUNCE_MS) {
      captureUID = uid;
      String found = findAnyUserByUID(uid);
      captureName = "";
      captureAccount = "";
      if (found.length()>0) {
        auto c = parseQuotedCSVLine(found);
        captureName = (c.size()>1?c[1]:"");
        captureAccount = (c.size()>2?c[2]:"");
      }
      captureDetectedAt = now;
      Serial.println("Capture detected UID: " + captureUID + " name: " + captureName + " acc: " + captureAccount);
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // buscar todos los registros del UID en users
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<std::vector<String>> userRows;
  if (f) {
    String header = f.readStringUntil('\n');
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size()>0 && c[0]==uid) userRows.push_back(c);
    }
    f.close();
  }

  if (userRows.size()==0) {
    // No registrado -> denegar inmediatamente
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"NO REGISTRADO\"";
    appendLineToFile(DENIED_FILE, rec);
    tft.fillScreen(ST77XX_BLACK); tft.setCursor(0,6); tft.setTextColor(ST77XX_RED); tft.setTextSize(1); tft.println("ACCESO DENEGADO"); tft.setTextColor(ST77XX_WHITE); tft.println(uid);
    ledRedOn();
    delay(DISPLAY_MS);
    ledOff();
    mfrc522.PICC_HaltA(); mfrc522.PCD_StopCrypto1();
    return;
  }

  String currentMat = currentScheduledMateria();
  bool allowed=false; String allowedMateria=""; String name="", account="";
  if (currentMat.length()>0) {
    for (auto &ur : userRows) {
      String m = (ur.size()>3?ur[3]:"");
      name = (ur.size()>1?ur[1]:"");
      account = (ur.size()>2?ur[2]:"");
      if (m == currentMat) { allowed=true; allowedMateria = m; break; }
    }
  } else {
    allowed = true;
    allowedMateria = (userRows.size()>0 && userRows[0].size()>3 ? userRows[0][3] : "");
    name = (userRows.size()>0 && userRows[0].size()>1 ? userRows[0][1] : "");
    account = (userRows.size()>0 && userRows[0].size()>2 ? userRows[0][2] : "");
  }

  if (allowed) {
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + allowedMateria + "\"," + "\"entrada\"";
    appendLineToFile(ATT_FILE, rec);
    tft.fillScreen(ST77XX_BLACK); tft.setCursor(0,6); tft.setTextColor(ST77XX_GREEN); tft.setTextSize(1); tft.println("ACCESO CONCEDIDO"); tft.setTextColor(ST77XX_WHITE); tft.println(name); tft.println(allowedMateria); tft.println(uid);
    ledGreenOn();
    puerta.write(90); delay(DISPLAY_MS); puerta.write(0);
    ledOff();

    if (currentMat.length() == 0) {
      String note = "Entrada permitida cuando no hay clase en curso. Materia permitida: " + allowedMateria;
      addNotification(uid, name, account, note);
    }
  } else {
    String matList = "";
    for (auto &row : userRows) { if (row.size()>3) { if (matList.length()) matList += "; "; matList += row[3]; } }
    String note = "Intento fuera de horario o materia no autorizada. Materias del usuario: " + matList;
    addNotification(uid, (userRows.size()>0?userRows[0][1]:""), (userRows.size()>0?userRows[0][2]:""), note);
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + note + "\"";
    appendLineToFile(DENIED_FILE, rec);
    tft.fillScreen(ST77XX_BLACK); tft.setCursor(0,6); tft.setTextColor(ST77XX_RED); tft.setTextSize(1); tft.println("ACCESO DENEGADO - NO AUTORIZADO"); tft.setTextColor(ST77XX_WHITE); tft.println(uid);
    ledRedOn();
    delay(DISPLAY_MS);
    ledOff();
  }

  mfrc522.PICC_HaltA(); mfrc522.PCD_StopCrypto1();
}
