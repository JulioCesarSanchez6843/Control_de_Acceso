// src/web/web_routes.cpp
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

#include "web_routes.h"
#include "globals.h"
#include "display.h"

// incluye las cabeceras de cada módulo web (deben existir en src/web/)
#include "capture.h"
#include "courses.h"
#include "students.h"
#include "schedules.h"
#include "history.h"
#include "notifications.h"
#include "web_common.h"
#include "self_register.h"  // declara handlers para self-registration
#include "teachers.h"       // handlers para maestros

// Declaraciones externas para acceder a las variables de self-register
extern volatile bool awaitingSelfRegister;
extern String currentSelfRegUID;
extern String currentSelfRegToken;
extern volatile bool blockRFIDForSelfReg;
extern std::vector<SelfRegSession> selfRegSessions;

void registerRoutes() {
  server.on("/", handleRoot);

  // Materias / Cursos (mantengo tus rutas explícitas)
  server.on("/materias", handleMaterias);
  server.on("/materias/new", handleMateriasNew);
  server.on("/materias_add", HTTP_POST, handleMateriasAddPOST);
  server.on("/materias/edit", handleMateriasEditGET);
  server.on("/materias_edit", HTTP_POST, handleMateriasEditPOST);
  server.on("/materias_delete", HTTP_POST, handleMateriasDeletePOST);

  server.on("/materias_new_schedule", handleMateriasNewScheduleGET);
  server.on("/materias_new_schedule_add", HTTP_POST, handleMateriasNewScheduleAddPOST);
  server.on("/materias_new_schedule_del", HTTP_POST, handleMateriasNewScheduleDelPOST);

  // Students
  server.on("/students", handleStudentsForMateria);
  server.on("/students_all", handleStudentsAll);
  server.on("/student_remove_course", HTTP_POST, handleStudentRemoveCourse);
  server.on("/student_delete", HTTP_POST, handleStudentDelete);

  // Teachers (nuevo conjunto de rutas)
  server.on("/teachers", handleTeachersForMateria);
  server.on("/teachers_all", handleTeachersAll);
  server.on("/teacher_remove_course", HTTP_POST, handleTeacherRemoveCourse);
  server.on("/teacher_delete", HTTP_POST, handleTeacherDelete);

  // Captura (shim delega en capture_individual / capture_lote)
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

  // Cancel capture & reset display. Ahora respeta return_to si se envía.
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

    // Limpiar archivo de cola en SPIFFS
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

    // LIMPIAR ESTADO DE SELF-REGISTER
    awaitingSelfRegister = false;
    currentSelfRegUID = "";
    currentSelfRegToken = "";
    blockRFIDForSelfReg = false;

    // También limpiar cualquier sesión de self-register activa
    selfRegSessions.clear();

    // Llamar a la función del display para volver a pantalla normal
    cancelCaptureAndReturnToNormal();

    Serial.println("Captura cancelada completamente - display resetado a pantalla de bienvenido");

    server.sendHeader("Location", return_to);
    server.send(303, "text/plain", "Canceled");
  });

  // NUEVO: terminar y guardar batch
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

  server.on("/notifications", handleNotificationsPage);
  server.on("/notifications_clear", HTTP_POST, handleNotificationsClearPOST);

  server.on("/edit", handleEditGet);
  server.on("/edit_post", HTTP_POST, handleEditPost);

  // Self-register
  server.on("/self_register_start", HTTP_POST, handleSelfRegisterStartPOST);
  server.on("/self_register", HTTP_GET, handleSelfRegisterGET);
  server.on("/self_register_submit", HTTP_POST, handleSelfRegisterPost);
  server.on("/self_register_cancel", HTTP_POST, handleSelfRegisterCancelPOST);

  // CSV endpoints (descarga directa)
  server.on("/users.csv", [](){
    if (!SPIFFS.exists(USERS_FILE)) { server.send(404,"text/plain","No users"); return; }
    File f = SPIFFS.open(USERS_FILE, FILE_READ); server.streamFile(f, "text/csv"); f.close();
  });

  server.on("/attendance.csv", [](){
    if (!SPIFFS.exists(ATT_FILE)) { server.send(404,"text/plain","no att"); return; }
    File f = SPIFFS.open(ATT_FILE, FILE_READ); server.streamFile(f,"text/csv"); f.close();
  });

  server.on("/notifications.csv", [](){
    if (!SPIFFS.exists(NOTIF_FILE)) { server.send(404,"text/plain","no"); return; }
    File f = SPIFFS.open(NOTIF_FILE, FILE_READ); server.streamFile(f,"text/csv"); f.close();
  });

  // Teachers CSV
  server.on("/teachers.csv", [](){
    if (!SPIFFS.exists(TEACHERS_FILE)) { server.send(404,"text/plain","No teachers"); return; }
    File f = SPIFFS.open(TEACHERS_FILE, FILE_READ); server.streamFile(f, "text/csv"); f.close();
  });

  // History & materia history
  server.on("/history", handleHistoryPage);
  server.on("/history.csv", handleHistoryCSV);
  server.on("/history_clear", HTTP_POST, handleHistoryClearPOST);
  server.on("/materia_history", handleMateriaHistoryGET);

  // --- pequeño endpoint JSON adicional (por si el frontend lo usa) ---
  // GET /profesores_for?materia=...
  server.on("/profesores_for", HTTP_GET, []() {
    if (!server.hasArg("materia")) {
      server.send(400, "application/json", "{\"error\":\"materia required\"}");
      return;
    }
    String mat = server.arg("materia"); mat.trim();
    std::vector<String> profs = getProfessorsForMateria(mat);
    String j = "{\"profesores\":[";
    for (size_t i = 0; i < profs.size(); ++i) {
      if (i) j += ",";
      // escape minimal
      String p = profs[i];
      p.replace("\\","\\\\");
      p.replace("\"","\\\"");
      j += "\"" + p + "\"";
    }
    j += "]}";
    server.send(200, "application/json", j);
  });
}
