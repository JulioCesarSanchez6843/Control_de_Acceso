#include <Arduino.h>
#include <SPI.h>
#include <FS.h>
#include <SPIFFS.h>
#include <MFRC522.h>
#include "globals.h"
#include "files_utils.h"
#include "display.h" 

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

// Handler principal para eventos RFID:
// - detecta tarjeta, lee UID
// - si está en captureMode almacena datos para registro manual
// - busca filas de usuario en USERS_FILE (puede haber varias materias)
// - decide: denegar (no registrado), permitir según horario, o permitir fuera de horario
// - registra asistencias, denied.csv y notificaciones; controla servo y UI
void rfidLoopHandler() {
  if (!mfrc522.PICC_IsNewCardPresent()) return;
  if (!mfrc522.PICC_ReadCardSerial()) return;

  String uid = uidBytesToString(mfrc522.uid.uidByte, mfrc522.uid.size);
  unsigned long now = millis();

  // Imprimir evento con hora local para debug
  Serial.printf("---- RFID event (%s) ----\n", nowISO().c_str());
  Serial.printf("Tarjeta detectada UID=%s\n", uid.c_str());

  // --- MODO CAPTURA (registro manual) ---
  // Si captureMode activo, guarda UID y info encontrada y no procesa entrada.
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
      Serial.printf("Capture mode: UID=%s -> name='%s' acc='%s'\n", captureUID.c_str(), captureName.c_str(), captureAccount.c_str());
    }
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

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
    bool okDenied = appendLineToFile(DENIED_FILE, recDenied);
    Serial.printf("appendLineToFile(DENIED_FILE) -> %s\n", okDenied ? "OK" : "FAIL");

    // Crear notificación (para que aparezca en la web)
    String note = "Tarjeta no registrada (UID: " + uid + ")";
    addNotification(uid, String(""), String(""), note);
    Serial.println("addNotification() llamada para tarjeta no registrada.");

    // UI: pantalla denegada
    showAccessDenied("Tarjeta no registrada", uid);
    ledOff();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // --- DEBUG: imprimir info de schedules y día actual ---
  {
    struct tm tm_now;
    if (getLocalTime(&tm_now)) {
      int wday = tm_now.tm_wday; // 0=domingo,1=lunes,...6=sábado
      int dayIndex = -1;
      if (wday >= 1 && wday <= 6) dayIndex = wday - 1;
      Serial.printf("DEBUG now local: %04d-%02d-%02d %02d:%02d (tm_wday=%d, dayIndex=%d)\n",
                    tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday, tm_now.tm_hour, tm_now.tm_min, wday, dayIndex);
    } else {
      Serial.println("DEBUG: getLocalTime() falló al intentar imprimir info de horario.");
    }

    auto dbgSchedules = loadSchedules();
    Serial.printf("DEBUG schedules loaded: %d\n", (int)dbgSchedules.size());
    for (auto &s : dbgSchedules) {
      Serial.printf("  sched owner='%s' day='%s' start='%s' end='%s'\n",
                    s.materia.c_str(), s.day.c_str(), s.start.c_str(), s.end.c_str());
    }
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
      // evitar duplicados (comparación case-insensitive para la lista)
      String mmLower = lowerCopy(mm);
      bool found = false;
      for (auto &ul : userMatsLower) if (ul == mmLower) { found = true; break; }
      if (!found) {
        userMats.push_back(mm);
        userMatsLower.push_back(mmLower);
      }
    }
  }

  // LOG materias del usuario
  String mmstr = joinMats(userMats);
  if (mmstr.length() == 0) mmstr = "(sin materias registradas)";
  Serial.printf("Usuario '%s' (uid=%s) materias: %s\n", name.c_str(), uid.c_str(), mmstr.c_str());

  // --- Si hay clase en curso: permitir solo si usuario tiene esa materia ---
  if (scheduleBaseMat.length() > 0) {
    String wantMat = normMat(scheduleBaseMat);
    String wantMatLower = lowerCopy(wantMat);
    bool hasCurrent = false;
    for (auto &mmLower : userMatsLower) {
      if (mmLower == wantMatLower) { hasCurrent = true; break; }
    }

    Serial.printf("Comparando materia en curso '%s' (lower='%s') con materias usuario (lower):", wantMat.c_str(), wantMatLower.c_str());
    for (auto &x : userMatsLower) Serial.printf(" '%s'", x.c_str());
    Serial.println();

    if (hasCurrent) {
      // Permitir entrada: registrar asistencia, abrir puerta, mostrar UI
      Serial.printf("ACCESO PERMITIDO: %s pertenece a '%s'\n", name.c_str(), wantMat.c_str());
      String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + wantMat + "\"," + "\"entrada\"";
      bool ok = appendLineToFile(ATT_FILE, rec);
      Serial.printf("appendLineToFile(ATT_FILE) -> %s\n", ok ? "OK" : "FAIL");

      puerta.write(90); // abrir
      showAccessGranted(name, wantMat, uid);
      puerta.write(0); // cerrar
      ledOff();

    } else {
      // Denegar: notificar admin y guardar en denied
      Serial.printf("ACCESO DENEGADO: %s no pertenece a materia en curso (%s)\n", name.c_str(), wantMat.c_str());

      String matList = mmstr;
      String note = "Intento fuera de materia en curso. Usuario: " + name + " (" + account + "). Materias del usuario: " + matList + ". Materia en curso: " + wantMat;
      addNotification(uid, name, account, note);
      Serial.println("addNotification() llamada para intento fuera de materia.");

      String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + note + "\"";
      bool okDenied = appendLineToFile(DENIED_FILE, rec);
      Serial.printf("appendLineToFile(DENIED_FILE) -> %s\n", okDenied ? "OK" : "FAIL");

      // UI: denegado con motivo corto
      String reason = "No pertenece a: " + wantMat;
      showAccessDenied(reason, uid);
      ledOff();
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  // --- Si NO hay clase en curso: permitir a cualquier usuario registrado ---
  {
    String usedMat = (userMats.size() > 0 ? userMats[0] : "");
    Serial.printf("No hay clase en curso -> permitir entrada a %s (materia informativa: %s)\n", name.c_str(), usedMat.c_str());

    String rec = "\"" + nowISO() + "\"," + "\"" + uid + "\"," + "\"" + name + "\"," + "\"" + account + "\"," + "\"" + usedMat + "\"," + "\"entrada\"";
    bool ok = appendLineToFile(ATT_FILE, rec);
    Serial.printf("appendLineToFile(ATT_FILE) -> %s\n", ok ? "OK" : "FAIL");

    // Notificación informativa para admin
    String note = "Entrada fuera de horario (no hay clase). Usuario: " + name + (usedMat.length() ? " Materia: " + usedMat : "");
    addNotification(uid, name, account, note);
    Serial.println("addNotification() llamada para entrada fuera de horario.");

    puerta.write(90);
    showAccessGranted(name, usedMat, uid);
    puerta.write(0);
    ledOff();

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }
}
