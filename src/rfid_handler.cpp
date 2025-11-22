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
  // leer y comprobar
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
  if (!exists) {
    appendLineToFile(QFILE, uid);
  }
}

// Handler principal para eventos RFID:
void rfidLoopHandler() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  // Imprimir evento con hora local para debug
  Serial.printf("---- RFID event (%s) ----\n", nowISO().c_str());
  Serial.printf("Tarjeta detectada UID=%s\n", uid.c_str());

  // --- Si estamos en Batch y hay un self-register en curso -> bloquear lecturas ---
  if (captureBatchMode && awaitingSelfRegister) {
    Serial.println("Lectura bloqueada: hay un auto-registro en curso. Ignorando tarjeta.");
    
    // CORRECCIÓN CRÍTICA: ACTUALIZAR captureUID PARA QUE handleCaptureBatchPollGET LO DETECTE
    captureUID = uid;
    captureName = "";
    captureAccount = "";
    captureDetectedAt = now;
    
    // CORRECCIÓN: Forzar que wrong_card se detecte inmediatamente
    // Esto hará que el display y la web se sincronicen
    Serial.println("DEBUG: Activando wrong_card inmediatamente");
    
    // Mostrar banner en pantalla para informar (sin UID para evitar sobreimpresiones)
    showSelfRegisterBanner(String());
    
    // Mostrar mensaje rojo temporal indicando que espere su turno (2s para sincronizar con web)
    showTemporaryRedMessage("Espere su turno: registro en curso", 2000UL);

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // --- MODO CAPTURA (registro manual) ---
  if (captureMode) {
    // --- Si estamos en modo BATCH, añadir a la cola y crear session+QR si no existe bloqueo ---
    if (captureBatchMode) {
      // Añadir a cola (si no duplicada)
      appendUidToQueueAvoidDup(uid);
      captureDetectedAt = now;
      Serial.printf("Batch capture: UID %s añadida a la cola.\n", uid.c_str());

      // Si ya hay espera activa en display, no crear nueva sesión (ya está bloqueando)
      if (awaitingSelfRegister) {
        Serial.println("Ya hay un self-register en espera en el display; no generar otro QR.");
      } else {
        // Si UID no registrado, crear una sesión y mostrar QR en display
        if (findAnyUserByUID(uid).length() == 0) {
          // crear session
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

          // CORRECCIÓN CRÍTICA: Establecer currentSelfRegUID con la UID esperada
          awaitingSelfRegister = true;
          currentSelfRegToken = s.token;
          currentSelfRegUID = uid;  // ← ESTA ES LA CORRECCIÓN CLAVE
          awaitingSinceMs = millis();

          // Crear URL completa (usar IP)
          String url = String("http://") + WiFi.localIP().toString() + String("/self_register?token=") + currentSelfRegToken;
          Serial.printf("Mostrando QR con URL: %s\n", url.c_str());
          Serial.printf("SelfRegister UID esperada: %s\n", currentSelfRegUID.c_str());

          // Mostrar QR en display (bloquea nuevas capturas hasta que alumno complete)
          int boxSize = min(tft.width(), tft.height()) - 24;
          showQRCodeOnDisplay(url, boxSize);
          // Mostrar banner (sin UID para no sobreimprimir)
          showSelfRegisterBanner(String());
        } else {
          Serial.println("UID ya registrado, no se genera QR (solo agregar a cola).");
        }
      }

      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }

    // --- Individual capture (comportamiento original) ---
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

      // CORRECCIÓN: Mostrar mensaje "Espere su turno" también en modo individual si hay self-register
      if (awaitingSelfRegister && currentSelfRegUID.length() > 0 && captureUID != currentSelfRegUID) {
        showTemporaryRedMessage("Espere su turno: registro en curso", 2000UL);
        // Limpiar la UID para evitar procesamiento incorrecto
        captureUID = "";
        captureName = "";
        captureAccount = "";
        captureDetectedAt = 0;
      } else {
        // actualizar display de captura individual si no hay conflicto
        showCaptureInProgress(false, captureUID);
      }
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // --- No estamos en captureMode: procesar acceso normal ---

  // --- Leer todos los registros del UID en USERS_FILE (puede tener varias materias) ---
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

  // --- Si no existe el usuario: denegar, loggear en denied.csv y CREAR notificación ---
  if (userRows.size() == 0) {
    Serial.printf("UID %s no registrado -> DENEGADO\n", uid.c_str());

    // Registrar en denied.csv
    String recDenied = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"NO REGISTRADO\"";
    appendLineToFile(DENIED_FILE, recDenied);

    // Crear notificación (para que aparezca en la web)
    String note = "Tarjeta no registrada (UID: " + uid + ")";
    addNotification(uid, String(""), String(""), note);

    // UI: pantalla denegada
    showAccessDenied("Tarjeta no registrada", uid);
    ledOff();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // --- Materia actual según horario (owner tal cual: puede ser 'Materia' o 'Materia||Profesor') ---
  String scheduleOwner = currentScheduledMateria(); // devuelve la parte materia (time_utils lo normaliza)
  String scheduleBaseMat = baseMateriaFromOwner(scheduleOwner);
  scheduleBaseMat.trim();
  Serial.printf("Schedule base materia detectada: '%s'\n", scheduleBaseMat.c_str());

  // Info principal del usuario (primer registro para name/account)
  String name = (userRows.size() > 0 && userRows[0].size() > 1 ? userRows[0][1] : "");
  String account = (userRows.size() > 0 && userRows[0].size() > 2 ? userRows[0][2] : "");

  // Construir lista única de materias del usuario (mantener original y lista lower para comparación)
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

  // --- Si hay clase en curso: permitir solo si usuario tiene esa materia ---
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

      puerta.write(90); // abrir
      showAccessGranted(name, wantMat, uid);
      puerta.write(0); // cerrar
      ledOff();
    } else {
      // Usuario no tiene la materia en curso -> denegar, notificar y loggear
      String mmstr = joinMats(userMats);
      String note = "Intento fuera de materia en curso. Usuario: " + name + " (" + account + "). Materias del usuario: " + mmstr + ". Materia en curso: " + wantMat;
      addNotification(uid, name, account, note);

      String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + note + "\"";
      appendLineToFile(DENIED_FILE, rec);

      // Mostrar pantalla denegada indicando la materia requerida
      showAccessDenied(String("No pertenece a: ") + wantMat, uid);
      ledOff();

      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }
  } else {
    // No hay clase en curso — permitir si tiene al menos una materia (registro de entrada libre) o denegar según tu política.
    // Aquí asumimos que si lista de materias no está vacía se permite, sino se deniega.
    if (!userMats.empty()) {
      String chosenMat = userMats[0];
      String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + chosenMat + "\"," + "\"entrada\"";
      appendLineToFile(ATT_FILE, rec);
      puerta.write(90);
      showAccessGranted(name, chosenMat, uid);
      puerta.write(0);
      ledOff();
    } else {
      String note = "Intento de acceso sin materia asignada. UID: " + uid + " Nombre: " + name;
      addNotification(uid, name, account, note);
      appendLineToFile(DENIED_FILE, String("\"") + nowISO() + String("\",\"") + uid + String("\",\"NO MATERIA\""));
      showAccessDenied("Sin materia asignada", uid);
      ledOff();
      mfrc522.PICC_HaltA();
      mfrc522.PCD_StopCrypto1();
      return;
    }
  }

  // finalizar interacción con tarjeta
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}