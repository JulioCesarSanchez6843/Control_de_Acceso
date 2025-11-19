// src/web/web_routes.cpp
#include <Arduino.h>
#include <FS.h>
#include <SPIFFS.h>

#include "web_routes.h"
#include "globals.h"

// incluye las cabeceras de cada módulo web (deben existir en src/web/)
#include "capture.h"
#include "courses.h"
#include "students.h"
#include "schedules.h"
#include "history.h"
#include "notifications.h"
#include "web_common.h"
#include "self_register.h"  // declara handlers para self-registration

// Registra todas las rutas del servidor web.
// Cada entry es equivalente a llamar server.on(...) en setup().
void registerRoutes() {
  // Página principal
  server.on("/", handleRoot);

  // --- Materias: CRUD y pantallas relacionadas ---
  server.on("/materias", handleMaterias);
  server.on("/materias/new", handleMateriasNew);
  server.on("/materias_add", HTTP_POST, handleMateriasAddPOST);
  server.on("/materias/edit", handleMateriasEditGET);
  server.on("/materias_edit", HTTP_POST, handleMateriasEditPOST);
  server.on("/materias_delete", HTTP_POST, handleMateriasDeletePOST);

  // Rutas para asignación de horarios por materia (nuevo flujo)
  server.on("/materias_new_schedule", handleMateriasNewScheduleGET);
  server.on("/materias_new_schedule_add", HTTP_POST, handleMateriasNewScheduleAddPOST);
  server.on("/materias_new_schedule_del", HTTP_POST, handleMateriasNewScheduleDelPOST);

  // --- Students: ver por materia / ver todos / acciones ---
  server.on("/students", handleStudentsForMateria);
  server.on("/students_all", handleStudentsAll);
  server.on("/student_remove_course", HTTP_POST, handleStudentRemoveCourse);
  server.on("/student_delete", HTTP_POST, handleStudentDelete);

  // --- Captura (modo manual) ---
  // Página principal de captura (muestra toggle Individual / Batch)
  server.on("/capture", HTTP_GET, handleCapturePage);

  // Confirmación individual (form submit)
  server.on("/capture_confirm", HTTP_POST, handleCaptureConfirm);

  // Polling para autocompletar UID en formulario individual
  server.on("/capture_poll", HTTP_GET, handleCapturePoll);

  // Stop general de modo captura (sirve para Individual y Batch)
  server.on("/capture_stop", HTTP_GET, handleCaptureStopGET);

  // --- Endpoints de Batch (coinciden con la implementación del capture.cpp que tienes) ---
  // Iniciar batch (GET en tu capture.cpp actual)
  server.on("/capture_batch_start", HTTP_GET, handleCaptureBatchStartGET);
  // Obtener estado / lista de UIDs en cola
  server.on("/capture_batch_poll", HTTP_GET, handleCaptureBatchPollGET);
  // Limpiar cola (POST)
  server.on("/capture_clear_queue", HTTP_POST, handleCaptureBatchClearPOST);

  // Edición vía capture (reutiliza UI de captura para editar usuario)
  server.on("/capture_edit", HTTP_GET, handleCaptureEditPage);
  server.on("/capture_edit_post", HTTP_POST, handleCaptureEditPost);

  // Estado / diagnóstico
  server.on("/status", handleStatus);

  // --- Schedules: grilla y edición ---
  server.on("/schedules", HTTP_GET, handleSchedulesGrid);
  server.on("/schedules/edit", HTTP_GET, handleSchedulesEditGrid);
  server.on("/schedules_add_slot", HTTP_POST, handleSchedulesAddSlot);
  server.on("/schedules_del", HTTP_POST, handleSchedulesDel);

  server.on("/schedules_for", HTTP_GET, handleSchedulesForMateriaGET);
  server.on("/schedules_for_add", HTTP_POST, handleSchedulesForMateriaAddPOST);
  server.on("/schedules_for_del", HTTP_POST, handleSchedulesForMateriaDelPOST);

  // --- Notificaciones ---
  server.on("/notifications", handleNotificationsPage);
  server.on("/notifications_clear", HTTP_POST, handleNotificationsClearPOST);

  // --- Edit user (formulario rápido) ---
  server.on("/edit", handleEditGet);
  server.on("/edit_post", HTTP_POST, handleEditPost);

  // --- Self-registration (nuevo) ---
  server.on("/self_register_start", HTTP_POST, handleSelfRegisterStartPOST); // profesor crea sesión
  server.on("/self_register", HTTP_GET, handleSelfRegisterGET);             // alumno accede con token
  server.on("/self_register_submit", HTTP_POST, handleSelfRegisterPost);    // submit desde móvil/alumno

  // --- Endpoints CSV (descarga directa de archivos) ---
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

  // --- Historial ---
  server.on("/history", handleHistoryPage);
  server.on("/history.csv", handleHistoryCSV);
  server.on("/history_clear", HTTP_POST, handleHistoryClearPOST);
  server.on("/materia_history", handleMateriaHistoryGET);
}
