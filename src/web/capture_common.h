// capture_common.h
#pragma once
#include <Arduino.h>
#include <vector>
#include <FS.h>
#include <SPIFFS.h>

#include "globals.h"
#include "files_utils.h"
#include "self_register.h"

// Nota: header-only helpers como static inline para evitar multiple definition.

// Ruta por defecto si no fue definida externamente
#ifndef CAPTURE_QUEUE_FILE
static const char *CAPTURE_QUEUE_FILE_LOCAL = "/capture_queue.csv";
#define CAPTURE_QUEUE_FILE CAPTURE_QUEUE_FILE_LOCAL
#endif

static inline String jsonEscape(const String &s) {
  String o = s;
  o.replace("\\", "\\\\");
  o.replace("\"", "\\\"");
  return o;
}

static inline std::vector<String> readCaptureQueue() {
  std::vector<String> out;
  if (!SPIFFS.exists(CAPTURE_QUEUE_FILE)) return out;
  File f = SPIFFS.open(CAPTURE_QUEUE_FILE, FILE_READ);
  if (!f) return out;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length()) out.push_back(l);
  }
  f.close();
  return out;
}

static inline bool appendUidToCaptureQueue(const String &uid) {
  if (uid.length() == 0) return false;

  // Bloqueo por self-register activo
  if (awaitingSelfRegister) {
    #ifdef USE_DISPLAY
    showTemporaryRedMessage("Espere su turno: registro en curso", 2000);
    #endif
    return false;
  }

  auto q = readCaptureQueue();
  for (auto &u : q) if (u == uid) return false;
  return appendLineToFile(CAPTURE_QUEUE_FILE, uid);
}

static inline bool clearCaptureQueueFile() {
  if (SPIFFS.exists(CAPTURE_QUEUE_FILE)) return SPIFFS.remove(CAPTURE_QUEUE_FILE);
  return true;
}

static inline bool writeCaptureQueue(const std::vector<String> &q) {
  if (SPIFFS.exists(CAPTURE_QUEUE_FILE)) SPIFFS.remove(CAPTURE_QUEUE_FILE);
  if (q.size() == 0) return true;
  for (auto &u : q) {
    if (!appendLineToFile(CAPTURE_QUEUE_FILE, u)) return false;
  }
  return true;
}

static inline String sanitizeReturnTo(const String &rt) {
  if (rt.length() > 0 && rt[0] == '/') return rt;
  return String("/students_all");
}
