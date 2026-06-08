// src/rfid_handler.cpp
// ============================================================
// ESTRATEGIA DE DATOS:
//   1. SPIFFS es la fuente primaria (rápida, local, siempre disponible).
//      Todos los registros se escriben PRIMERO aquí.
//   2. Oracle / BD es el respaldo.  Después de escribir en SPIFFS
//      se intenta enviar a la BD en el mismo evento (fire-and-forget).
//      Si falla, el dato queda en SPIFFS marcado como pendiente y
//      syncPendingToServer() lo reintenta cada 5 min en background.
//   3. modoLocal ha sido ELIMINADO — el sistema opera igual
//      con o sin BD disponible.
// ============================================================

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

static std::vector<String> teacherMatsForUID(const String &uid) {
  std::vector<String> out;

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
// Helpers de persistencia — NUEVA ESTRATEGIA DUAL
//
// SIEMPRE:  escribe en SPIFFS (rápido, local)
// LUEGO:    intenta enviar a Oracle (fire-and-forget)
//           Si falla, el dato queda en SPIFFS y
//           syncPendingToServer() lo reintentará.
//
// Retorna true si Oracle confirmó la recepción,
//         false si quedó pendiente en SPIFFS.
// ------------------------------------------------------------

static bool saveAttendanceDual(
    const String &timestamp,
    const String &uid,
    const String &name,
    const String &account,
    const String &materia,
    const String &mode
) {
  // 1) Escribir en SPIFFS PRIMERO (siempre)
  String rec = csvLine6(timestamp, uid, name, account, materia, mode);
  appendLineToFile(ATT_FILE, rec);
  Serial.printf("SPIFFS: asistencia guardada UID=%s\n", uid.c_str());

  // 2) Intentar enviar a Oracle inmediatamente
  if (WiFi.status() == WL_CONNECTED) {
    bool sent = sendAsistencia(timestamp, uid, name, account, materia, mode);
    if (sent) {
      Serial.printf("Oracle: asistencia enviada UID=%s\n", uid.c_str());
      return true;
    }
    Serial.println("Oracle no disponible ahora; se sincronizara en background.");
  } else {
    Serial.println("Sin WiFi; asistencia quedara pendiente en SPIFFS.");
  }
  return false;
}

static bool saveDeniedDual(
    const String &timestamp,
    const String &uid,
    const String &note
) {
  // 1) SPIFFS primero
  String rec = csvLine3(timestamp, uid, note);
  appendLineToFile(DENIED_FILE, rec);
  Serial.printf("SPIFFS: acceso denegado guardado UID=%s\n", uid.c_str());

  // 2) Oracle
  if (WiFi.status() == WL_CONNECTED) {
    bool sent = sendAccesoDenegadoRegistro(timestamp, uid, note);
    if (sent) {
      Serial.printf("Oracle: acceso denegado enviado UID=%s\n", uid.c_str());
      return true;
    }
    Serial.println("Oracle no disponible ahora; se sincronizara en background.");
  } else {
    Serial.println("Sin WiFi; denegado quedara pendiente en SPIFFS.");
  }
  return false;
}

static bool saveNotificationDual(
    const String &timestamp,
    const String &uid,
    const String &name,
    const String &account,
    const String &note
) {
  // 1) SPIFFS primero
  String rec = csvLine5(timestamp, uid, name, account, note);
  appendLineToFile(NOTIF_FILE, rec);
  Serial.printf("SPIFFS: notificacion guardada UID=%s\n", uid.c_str());

  // 2) Oracle
  if (WiFi.status() == WL_CONNECTED) {
    bool sent = sendNotificacionRegistro(timestamp, uid, name, account, note);
    if (sent) {
      Serial.printf("Oracle: notificacion enviada UID=%s\n", uid.c_str());
      return true;
    }
    Serial.println("Oracle no disponible ahora; se sincronizara en background.");
  } else {
    Serial.println("Sin WiFi; notif quedara pendiente en SPIFFS.");
  }
  return false;
}

// ------------------------------------------------------------
// Helpers para sync en background
// ------------------------------------------------------------

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

// Intenta reenviar a Oracle lo que está en SPIFFS y aún no llegó.
// Nota: el dato NO se borra del SPIFFS (SPIFFS mantiene el historial).
// Solo marcamos "sincronizado" con un archivo aparte si fuera necesario;
// en esta versión simplemente reenviamos y, si Oracle responde OK,
// ya quedó duplicado en ambos lados — que es exactamente lo que queremos.
// El archivo SPIFFS NO se trunca para no perder el historial local.
static void trySyncAttendanceFile() {
  if (!SPIFFS.exists(ATT_FILE)) return;

  File f = SPIFFS.open(ATT_FILE, FILE_READ);
  if (!f) return;

  // Leer header
  String header = f.readStringUntil('\n');
  (void)header;

  // Colectar líneas pendientes (no enviadas aún)
  // Estrategia simple: intentar enviar todas; Oracle ignorará duplicados
  // si el servidor tiene manejo de idempotencia, o simplemente se duplican.
  // Para evitar duplicados en Oracle usamos la columna unique del endpoint.
  // En esta versión enviamos solo si el archivo tiene filas.
  bool hadRows = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    hadRows = true;
    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 6) {
      sendAsistencia(c[0], c[1], c[2], c[3], c[4], c[5]);
      // No importa si falla: lo reintentará en el siguiente ciclo
    }
  }
  f.close();
  if (hadRows) Serial.println("SYNC: revisando asistencias SPIFFS -> Oracle");
}

static void trySyncDeniedFile() {
  if (!SPIFFS.exists(DENIED_FILE)) return;
  File f = SPIFFS.open(DENIED_FILE, FILE_READ);
  if (!f) return;
  String header = f.readStringUntil('\n'); (void)header;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 3) {
      sendAccesoDenegadoRegistro(c[0], c[1], c[2]);
    }
  }
  f.close();
}

// Archivo auxiliar que guarda una línea por cada notificación ya enviada
// (usamos timestamp+uid como clave única).
static const char* NOTIF_SENT_FILE = "/notif_sent.txt";

static bool isNotifAlreadySent(const String &key) {
  if (!SPIFFS.exists(NOTIF_SENT_FILE)) return false;
  File f = SPIFFS.open(NOTIF_SENT_FILE, FILE_READ);
  if (!f) return false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line == key) { f.close(); return true; }
  }
  f.close();
  return false;
}

static void markNotifSent(const String &key) {
  File f = SPIFFS.open(NOTIF_SENT_FILE, FILE_APPEND);
  if (f) { f.println(key); f.close(); }
}

static void trySyncNotificationsFile() {
  if (!SPIFFS.exists(NOTIF_FILE)) return;
  File f = SPIFFS.open(NOTIF_FILE, FILE_READ);
  if (!f) return;
  String header = f.readStringUntil('\n'); (void)header;
  int enviadas = 0;
  int omitidas = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (!line.length()) continue;
    auto c = parseQuotedCSVLine(line);
    if (c.size() >= 5) {
      // Clave única: timestamp + UID
      String key = c[0] + "|" + c[1];
      if (isNotifAlreadySent(key)) {
        omitidas++;
        continue; // ya fue enviada exitosamente antes
      }
      bool ok = sendNotificacionRegistro(c[0], c[1], c[2], c[3], c[4]);
      if (ok) {
        markNotifSent(key); // marcar para no reenviar
        enviadas++;
      }
    }
  }
  f.close();
  if (enviadas > 0 || omitidas > 0)
    Serial.printf("SYNC notif: %d enviadas, %d ya existian\n", enviadas, omitidas);
}

// ------------------------------------------------------------
// syncPendingToServer — llamado desde setup() y desde loop
// via rfidLoopHandler().
// Verifica si la BD está disponible y reenvía lo pendiente.
// SPIFFS NO se borra: mantiene el historial permanente.
// ------------------------------------------------------------
void syncPendingToServer() {
  static unsigned long lastAttemptMs = 0;
  const unsigned long SYNC_CHECK_INTERVAL_MS = 5UL * 60UL * 1000UL; // cada 5 minutos

  if (millis() - lastAttemptMs < SYNC_CHECK_INTERVAL_MS) return;
  lastAttemptMs = millis();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("SYNC: sin WiFi, omitiendo.");
    return;
  }

  if (!pingServer()) {
    Serial.println("SYNC: servidor BD no disponible aun.");
    return;
  }

  Serial.println("SYNC: servidor BD disponible, reenviando registros SPIFFS...");
  trySyncAttendanceFile();
  trySyncDeniedFile();
  trySyncNotificationsFile();
  Serial.println("SYNC: ciclo de sincronizacion completado.");
}

// ------------------------------------------------------------
// Handler principal para eventos RFID
// ------------------------------------------------------------
void rfidLoopHandler() {
  // Sincronizar SPIFFS -> Oracle en background
  syncPendingToServer();

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
        if (findTeacherByUID(uid).length() > 0) {
          Serial.printf("UID %s detectada como MAESTRO durante batch -> bloquear y notificar\n", uid.c_str());

          String teacherRow = findTeacherByUID(uid);
          String teacherName = "";
          if (teacherRow.length()) {
            auto tc = parseQuotedCSVLine(teacherRow);
            if (tc.size() > 1) teacherName = tc[1];
          }

          String notificationMsg = "Tarjeta de maestro detectada en captura por lote y bloqueada: " +
                                   (teacherName.length() ? teacherName : String("Sin nombre")) +
                                   " (UID: " + uid + ")";

          saveDeniedDual(nowISO(), uid, "MAESTRO_BLOQUEADO_LOTE");
          saveNotificationDual(nowISO(), uid, teacherName, "", notificationMsg);

          captureUID = uid;
          captureName = "";
          captureAccount = "";
          captureDetectedAt = now;

          showTemporaryRedMessage("MAESTRO - NO PERMITIDO EN LOTE", 4000UL);

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

    // Captura individual
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

  // ──────────────────────────────────────────────────────────
  // PROCESO NORMAL DE ACCESO
  // ──────────────────────────────────────────────────────────

  // Leer registros del UID en USERS_FILE (SPIFFS)
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

  // Revisar TEACHERS_FILE (SPIFFS)
  String teacherRow = findTeacherByUID(uid);
  bool isTeacher = (teacherRow.length() > 0);

  // Si no existe ni user ni teacher -> denegar
  if (userRows.size() == 0 && !isTeacher) {
    Serial.printf("UID %s no registrado -> DENEGADO\n", uid.c_str());
    saveDeniedDual(nowISO(), uid, "NO_REGISTRADO");
    String note = "Tarjeta no registrada (UID: " + uid + ")";
    saveNotificationDual(nowISO(), uid, "", "", note);
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

  String scheduleOwnerProf = "";
  bool scheduleOwnerHasProf = false;
  int soidx = scheduleOwner.indexOf("||");
  if (soidx >= 0) {
    scheduleOwnerProf = scheduleOwner.substring(soidx + 2);
    scheduleOwnerProf.trim();
    scheduleOwnerHasProf = true;
  }

  // ── ALUMNOS ──────────────────────────────────────────────
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
        saveAttendanceDual(ts, uid, name, account, wantMat, "entrada");
        puerta.write(90);
        showAccessGranted(name, wantMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String mmstr = joinMats(userMats);
        String note = "Intento fuera de materia en curso. Usuario: " + name + " (" + account + "). Materias del usuario: " + mmstr + ". Materia en curso: " + wantMat;
        saveNotificationDual(nowISO(), uid, name, account, note);
        saveDeniedDual(nowISO(), uid, "FUERA_DE_MATERIA");
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
        saveAttendanceDual(ts, uid, name, account, chosenMat, "entrada");
        String note = "Entrada fuera de horario (Alumno). Usuario: " + name + " (" + account + "). Materia asignada: " + chosenMat;
        saveNotificationDual(nowISO(), uid, name, account, note);
        puerta.write(90);
        showAccessGranted(name, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso sin materia asignada. UID: " + uid + " Nombre: " + (userRows.size() ? (userRows[0].size() > 1 ? userRows[0][1] : "") : "");
        saveNotificationDual(nowISO(), uid, "", "", note);
        saveDeniedDual(nowISO(), uid, "SIN_MATERIA");
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

  // ── TEACHERS ─────────────────────────────────────────────
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
          saveAttendanceDual(ts, uid, tname, tacc, wantMat, "entrada-teacher");
          puerta.write(90);
          showAccessGranted(tname, wantMat, uid);
          puerta.write(0);
          ledOff();
        } else {
          String mmstr = joinMats(tmats);
          String note = "Intento fuera de materia en curso (teacher). Maestro: " + tname + " (" + tacc + "). Materias del maestro: " + mmstr + ". Materia en curso: " + scheduleOwner;
          saveNotificationDual(nowISO(), uid, tname, tacc, note);
          saveDeniedDual(nowISO(), uid, "NO_MATERIA_TEACHER");
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
          saveAttendanceDual(ts, uid, tname, tacc, wantMat, "entrada-teacher");
          puerta.write(90);
          showAccessGranted(tname, wantMat, uid);
          puerta.write(0);
          ledOff();
        } else {
          String mmstr = joinMats(tmats);
          String note = "Intento fuera de materia en curso (teacher). Maestro: " + tname + " (" + tacc + "). Materias del maestro: " + mmstr + ". Materia en curso: " + wantMat;
          saveNotificationDual(nowISO(), uid, tname, tacc, note);
          saveDeniedDual(nowISO(), uid, "NO_MATERIA_TEACHER");
          showAccessDenied(String("No asignado a: ") + wantMat, uid);
          ledOff();
          mfrc522.PICC_HaltA();
          mfrc522.PCD_StopCrypto1();
          return;
        }
      }
    } else {
      // Sin clase activa: permitir y notificar
      if (!tmats.empty()) {
        String chosenMat = tmats[0];
        String ts = nowISO();
        saveAttendanceDual(ts, uid, tname, tacc, chosenMat, "entrada-teacher");
        String note = "Entrada fuera de horario (Maestro). Maestro: " + tname + " (" + tacc + "). Materia: " + chosenMat;
        saveNotificationDual(nowISO(), uid, tname, tacc, note);
        puerta.write(90);
        showAccessGranted(tname, chosenMat, uid);
        puerta.write(0);
        ledOff();
      } else {
        String note = "Intento de acceso (teacher) sin materias asignadas. UID: " + uid + " Nombre: " + tname;
        saveNotificationDual(nowISO(), uid, tname, tacc, note);
        saveDeniedDual(nowISO(), uid, "SIN_MATERIA_TEACHER");
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