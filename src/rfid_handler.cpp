// src/rfid_handles.cpp
#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <MFRC522.h>
#include "globals.h"
#include "files_utils.h"
#include "display.h" // showWaitingMessage, showAccessGranted, showAccessDenied, led* functions

// Nota: nowISO() está declarado en globals.h e implementado en src/time_utils.cpp
// No definir nowISO() aquí.

void rfidLoopHandler() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;
  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  // Modo captura (registro manual): solo guardar y salir
  if (captureMode) {
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
      Serial.println("Capture detected UID: " + captureUID + " name: " + captureName + " acc: " + captureAccount);
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Leer todos los registros del usuario con ese UID (puede tener varias materias)
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
  }

  // 1) Tarjeta no registrada
  if (userRows.size() == 0) {
    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"NO REGISTRADO\"";
    appendLineToFile(DENIED_FILE, rec);

    // UI: pantalla denegada (mensaje corto). El display se encarga de la espera 5s y de volver.
    showAccessDenied("Tarjeta no registrada", uid);
    ledOff();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Obtener materia actual según horario (vacío = no hay clase)
  String currentMat = currentScheduledMateria();

  // Recoger info principal del usuario (primer registro para nombre/account)
  String name = (userRows.size() > 0 && userRows[0].size() > 1 ? userRows[0][1] : "");
  String account = (userRows.size() > 0 && userRows[0].size() > 2 ? userRows[0][2] : "");

  // Construir lista de materias del usuario (unique)
  std::vector<String> userMats;
  for (auto &r : userRows) {
    if (r.size() > 3) {
      bool found = false;
      for (auto &mm : userMats) if (mm == r[3]) { found = true; break; }
      if (!found) userMats.push_back(r[3]);
    }
  }

  // Si HAY materia en curso -> permitir solo si el usuario tiene esa materia
  if (currentMat.length() > 0) {
    bool hasCurrent = false;
    for (auto &mm : userMats) if (mm == currentMat) { hasCurrent = true; break; }

    if (hasCurrent) {
      // Permitir entrada: registrar asistencia, abrir puerta, mostrar UI de éxito
      String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + currentMat + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, rec);

      puerta.write(90); // abrir
      showAccessGranted(name, currentMat, uid);
      puerta.write(0); // cerrar
      ledOff();

    } else {
      // Denegar: notificar admin y guardar en denied
      String matList = "";
      for (size_t i = 0; i < userMats.size(); ++i) {
        if (i) matList += "; ";
        matList += userMats[i];
      }
      String note = "Intento fuera de materia en curso. Materias del usuario: " + matList + ". Materia en curso: " + currentMat;
      addNotification(uid, name, account, note);
      String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + note + "\"";
      appendLineToFile(DENIED_FILE, rec);

      // UI: denegado con motivo corto
      String reason = "No pertenece a: " + currentMat;
      showAccessDenied(reason, uid);
      ledOff();
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // Si NO HAY materia en curso -> PERMITIR entrada, registrar attendance y generar notificación informativa
  {
    // Tomamos la primera materia del usuario como materia asociada (si existe), si no, cadena vacía
    String usedMat = (userMats.size() > 0 ? userMats[0] : "");

    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + usedMat + "\"," + "\"entrada\"";
    appendLineToFile(ATT_FILE, rec);

    // Notificar en notifications file que alguien entró cuando no había clase
    String note = "Entrada fuera de horario (no hay clase). Usuario: " + name + (usedMat.length() ? " Materia: " + usedMat : "");
    addNotification(uid, name, account, note);

    // Abrir puerta y mostrar pantalla de éxito
    puerta.write(90);
    showAccessGranted(name, usedMat, uid);
    puerta.write(0);
    ledOff();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }
}
