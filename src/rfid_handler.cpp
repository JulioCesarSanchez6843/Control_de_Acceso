#include <Arduino.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <MFRC522.h>
#include "rfid_handler.h"
#include "files_utils.h"
#include "display.h"
#include "globals.h"

// Si la instancia global de MFRC522 la creaste en main.cpp:
// (asegúrate que en main.cpp tienes: MFRC522 mfrc522(SS_PIN, RST_PIN); )
extern MFRC522 mfrc522;

// capture globals (definidas en main.cpp)
extern volatile bool captureMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// Constante local de debounce (evita dependencia con macros externas)
static const unsigned long CAPTURE_DEBOUNCE_MS_LOCAL = 3000UL;

void rfidInit() {
  SPI.begin();
  mfrc522.PCD_Init();
}

// devuelve la materia programada actualmente (o "" si ninguna)
String currentScheduledMateria() {
  struct tm tm_now;
  if (!getLocalTime(&tm_now)) return "";
  int wday = tm_now.tm_wday;
  int dayIndex = -1;
  if (wday >= 1 && wday <= 6) dayIndex = wday - 1; // LUN..SAB
  if (dayIndex < 0) return "";
  int nowMin = tm_now.tm_hour * 60 + tm_now.tm_min;
  auto schedules = loadSchedules();
  const String DAYS[6] = {"LUN","MAR","MIE","JUE","VIE","SAB"};
  for (auto &s : schedules) {
    if (s.day == DAYS[dayIndex]) {
      if (s.start.length() >= 5 && s.end.length() >= 5) {
        int sh = s.start.substring(0,2).toInt();
        int sm = s.start.substring(3,5).toInt();
        int eh = s.end.substring(0,2).toInt();
        int em = s.end.substring(3,5).toInt();
        int smin = sh*60 + sm;
        int emin = eh*60 + em;
        if (smin <= nowMin && nowMin <= emin) return s.materia;
      }
    }
  }
  return "";
}

void rfidLoopHandler() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;
  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  // Modo captura (UI web) -> rellenar captureUID/name/account
  if (captureMode) {
    if (captureUID.length() == 0 || (now - captureDetectedAt) > CAPTURE_DEBOUNCE_MS_LOCAL) {
      captureUID = uid;
      String found = findAnyUserByUID(uid);
      captureName = "";
      captureAccount = "";
      if (found.length() > 0) {
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

  // Modo normal: buscar usuario(s) por UID
  File f = SPIFFS.open(USERS_FILE, FILE_READ);
  std::vector<std::vector<String>> userRows;
  if (f) {
    String header = f.readStringUntil('\n');
    while (f.available()) {
      String l = f.readStringUntil('\n'); l.trim();
      if (!l.length()) continue;
      auto c = parseQuotedCSVLine(l);
      if (c.size()>0 && c[0] == uid) userRows.push_back(c);
    }
    f.close();
  }

  if (userRows.size() == 0) {
    // No registrado -> denegar y guardar en archivo denied
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"NO REGISTRADO\"";
    appendLineToFile(DENIED_FILE, rec);
    showDenied(uid);
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Determinar materia actual (si hay)
  String currentMat = currentScheduledMateria();
  bool allowed = false;
  String allowedMateria = "";
  String name = "";
  String account = "";

  if (currentMat.length() > 0) {
    for (auto &ur : userRows) {
      String m = (ur.size()>3?ur[3]:"");
      name = (ur.size()>1?ur[1]:"");
      account = (ur.size()>2?ur[2]:"");
      if (m == currentMat) { allowed = true; allowedMateria = m; break; }
    }
  } else {
    // Si no hay clase, permitimos (pero anotamos materia si el usuario tiene asignada)
    allowed = true;
    allowedMateria = (userRows.size()>0 && userRows[0].size()>3 ? userRows[0][3] : "");
    name = (userRows.size()>0 && userRows[0].size()>1 ? userRows[0][1] : "");
    account = (userRows.size()>0 && userRows[0].size()>2 ? userRows[0][2] : "");
  }

  if (allowed) {
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + allowedMateria + "\"," + "\"entrada\"";
    appendLineToFile(ATT_FILE, rec);
    showGranted(name, allowedMateria, uid);
    if (currentMat.length() == 0) {
      String note = "Entrada permitida cuando no hay clase en curso. Materia permitida: " + allowedMateria;
      addNotification(uid, name, account, note);
    }
  } else {
    String matList = "";
    for (auto &row : userRows) {
      if (row.size()>3) {
        if (matList.length()) matList += "; ";
        matList += row[3];
      }
    }
    String note = "Intento fuera de horario o materia no autorizada. Materias del usuario: " + matList;
    addNotification(uid, (userRows.size()>0?userRows[0][1]:""), (userRows.size()>0?userRows[0][2]:""), note);
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + note + "\"";
    appendLineToFile(DENIED_FILE, rec);
    showDenied(uid, note);
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}
