// src/web/web_routes.cpp
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

#include "web/web_routes.h"
#include "globals.h"
#include "display.h"

// incluye las cabeceras de cada módulo web (deben existir en src/web/)
#include "capture.h"
#include "courses.h"
#include "students.h"
#include "schedules.h"
#include "history.h"
#include "notifications.h"
#include "web/web_common.h"
#include "self_register.h"  // declara handlers para self-registration
#include "teachers.h"       // handlers para maestros
#include "edit.h"

// CAPTURE_QUEUE_FILE se define en capture_common.h (cola temporal en SPIFFS)
#include "capture_common.h"

// Declaraciones externas para acceder a las variables de self-register
extern volatile bool awaitingSelfRegister;
extern String currentSelfRegUID;
extern String currentSelfRegToken;
extern volatile bool blockRFIDForSelfReg;
extern std::vector<SelfRegSession> selfRegSessions;

// Forward declarations
void handleNotificationsPage();
void handleNotificationsReadPage();
void handleNotificationsClearPOST();
void handleNotificationsDeletePOST();
void handleNotificationsMarkPOST();

void handleProfesoresForMateriaGET();
// ──────────────────────────────────────────────────────────────────────────────

void registerRoutes() {
  server.on("/", handleRoot);

  // Materias / Cursos
  server.on("/materias", handleMaterias);
  server.on("/materias/new", handleMateriasNew);
  server.on("/materias_add", HTTP_POST, handleMateriasAddPOST);
  server.on("/materias/edit", handleMateriasEditGET);
  server.on("/materias_edit", HTTP_POST, handleMateriasEditPOST);
  server.on("/materias_delete", HTTP_POST, handleMateriasDeletePOST);

  server.on("/materias_new_schedule", handleMateriasNewScheduleGET);
  server.on("/materias_new_schedule_add", HTTP_POST, handleMateriasNewScheduleAddPOST);
  server.on("/materias_new_schedule_del", HTTP_POST, handleMateriasNewScheduleDelPOST);

  // Endpoint usado por la UI para obtener profesores de una materia (AJAX)
  // GET /profesores_for?materia=...
  server.on("/profesores_for", HTTP_GET, handleProfesoresForMateriaGET);

  // Students
  server.on("/students", handleStudentsForMateria);
  server.on("/students_all", handleStudentsAll);
  server.on("/student_remove_course", HTTP_POST, handleStudentRemoveCourse);
  server.on("/student_delete", HTTP_POST, handleStudentDelete);

  // Teachers
  server.on("/teachers", handleTeachersForMateria);
  server.on("/teachers_all", handleTeachersAll);
  server.on("/teacher_remove_course", HTTP_POST, handleTeacherRemoveCourse);
  server.on("/teacher_delete", HTTP_POST, handleTeacherDelete);

  // Captura (shim delega en capture_individual / capture_batch)
  server.on("/capture", HTTP_GET, handleCapturePage);
  server.on("/capture_individual", HTTP_GET, handleCaptureIndividualPage);
  server.on("/capture_batch", HTTP_GET, handleCaptureBatchPage);
  server.on("/capture_start", HTTP_POST, handleCaptureStartPOST);
  server.on("/capture_confirm", HTTP_POST, handleCaptureConfirm);
  server.on("/capture_poll", HTTP_GET, handleCapturePoll);
  server.on("/capture_stop", HTTP_GET, handleCaptureStopGET);

  // Batch endpoints
  server.on("/capture_batch_poll", HTTP_GET, handleCaptureBatchPollGET);
  server.on("/capture_batch_stop", HTTP_POST, handleCaptureBatchStopPOST);
  server.on("/capture_batch_pause", HTTP_POST, handleCaptureBatchPausePOST);
  server.on("/capture_remove_last", HTTP_POST, handleCaptureRemoveLastPOST);
  server.on("/capture_generate_links", HTTP_POST, handleCaptureGenerateLinksPOST);

  // Cancel capture & reset display. Respeta return_to si se envía.
  server.on("/cancel_capture", HTTP_POST, []() {
    Serial.println("Cancelando captura y limpiando cola desde /cancel_capture...");

    // Leer return_to si fue enviado en el POST
    String return_to = "/";
    if (server.hasArg("return_to")) {
      String rt = server.arg("return_to"); rt.trim();
      if (rt.length() && rt[0] == '/') return_to = rt;
    }

    // Limpiar la cola de UIDs en memoria
    capturedUIDs.clear();

    // Limpiar archivo de cola temporal en SPIFFS
    if (SPIFFS.exists(CAPTURE_QUEUE_FILE)) {
      SPIFFS.remove(CAPTURE_QUEUE_FILE);
      Serial.println("Archivo de cola eliminado: " + String(CAPTURE_QUEUE_FILE));
    }

    // Limpiar estados de captura globales
    isCapturing = false;
    isBatchCapture = false;

    // Limpiar estados de captura del módulo RFID
    captureMode = false;
    captureBatchMode = false;
    captureUID = "";
    captureName = "";
    captureAccount = "";
    captureDetectedAt = 0;

    // Limpiar estado de self-register
    awaitingSelfRegister = false;
    currentSelfRegUID = "";
    currentSelfRegToken = "";
    blockRFIDForSelfReg = false;
    selfRegSessions.clear();

    // Volver a pantalla normal
    cancelCaptureAndReturnToNormal();

    Serial.println("Captura cancelada completamente - display resetado a pantalla de bienvenido");

    server.sendHeader("Location", return_to);
    server.send(303, "text/plain", "Canceled");
  });

  // Terminar y guardar batch
  server.on("/capture_finish", HTTP_POST, handleCaptureFinishPOST);

  server.on("/capture_edit", HTTP_GET, handleCaptureEditPage);
  server.on("/capture_edit_post", HTTP_POST, handleCaptureEditPost);

  server.on("/status", handleStatus);

  server.on("/schedules", HTTP_GET, handleSchedulesGrid);
  server.on("/schedules/edit", HTTP_GET, handleSchedulesEditGrid);
  server.on("/schedules_add_slot", HTTP_POST, handleSchedulesAddSlot);
  server.on("/schedules_del", HTTP_POST, handleSchedulesDel);

  server.on("/schedules_for", HTTP_GET, handleSchedulesForMateriaGET);
  server.on("/schedules_for_add", HTTP_POST, handleSchedulesForMateriaAddPOST);
  server.on("/schedules_for_del", HTTP_POST, handleSchedulesForMateriaDelPOST);

  // Notifications: lista + acciones
  server.on("/notifications", HTTP_GET, handleNotificationsPage);
  server.on("/notifications_read", HTTP_GET, handleNotificationsReadPage);
  server.on("/notifications_clear", HTTP_POST, handleNotificationsClearPOST);
  server.on("/notifications_delete", HTTP_POST, handleNotificationsDeletePOST);
  server.on("/notifications_mark", HTTP_POST, handleNotificationsMarkPOST);

  server.on("/edit", handleEditGet);
  server.on("/edit_post", HTTP_POST, handleEditPost);

  // Self-register
  server.on("/self_register_start", HTTP_POST, handleSelfRegisterStartPOST);
  server.on("/self_register", HTTP_GET, handleSelfRegisterGET);
  server.on("/self_register_submit", HTTP_POST, handleSelfRegisterPost);
  server.on("/self_register_cancel", HTTP_POST, handleSelfRegisterCancelPOST);

  server.on("/history", handleHistoryPage);
  server.on("/history.csv", handleHistoryCSV);
  server.on("/history_clear", HTTP_POST, handleHistoryClearPOST);
  server.on("/materia_history", handleMateriaHistoryGET);

  // Página de configuración si existe
  #ifdef HAS_CONFIG_PAGE
  server.on("/config", handleConfigPage);
  server.on("/config_save", HTTP_POST, handleConfigSavePOST);
  #endif

  // Página de logs si existe
  #ifdef HAS_LOGS_PAGE
  server.on("/logs", handleLogsPage);
  #endif

  // Favicon (evita errores 404 en navegadores)
  server.on("/favicon.ico", []() {
    if (SPIFFS.exists("/favicon.ico")) {
      File f = SPIFFS.open("/favicon.ico", FILE_READ);
      server.streamFile(f, "image/x-icon");
      f.close();
    } else {
      server.send(404, "text/plain", "No favicon");
    }
  });

  // Logo
  server.on("/logo.png", []() {
    if (SPIFFS.exists("/logo.png")) {
      File f = SPIFFS.open("/logo.png", FILE_READ);
      server.streamFile(f, "image/png");
      f.close();
    } else {
      server.send(404, "text/plain", "No logo");
    }
  });

  // CSS externo
  server.on("/style.css", []() {
    if (SPIFFS.exists("/style.css")) {
      File f = SPIFFS.open("/style.css", FILE_READ);
      server.streamFile(f, "text/css");
      f.close();
    } else {
      server.send(404, "text/plain", "No CSS file");
    }
  });

  // Manejo de error 404
  server.onNotFound([]() {
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    server.send(404, "text/plain", message);
  });
}