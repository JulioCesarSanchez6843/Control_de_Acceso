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

// Registra todas las rutas (equivalente a server.on(...) en setup original)
void registerRoutes() {
  server.on("/", handleRoot);

  // materias
  server.on("/materias", handleMaterias);
  server.on("/materias/new", handleMateriasNew);
  server.on("/materias_add", HTTP_POST, handleMateriasAddPOST);
  server.on("/materias/edit", handleMateriasEditGET);
  server.on("/materias_edit", HTTP_POST, handleMateriasEditPOST);
  server.on("/materias_delete", HTTP_POST, handleMateriasDeletePOST);

  // **nuevas rutas de asignación de horarios**
  server.on("/materias_new_schedule", handleMateriasNewScheduleGET);
  server.on("/materias_new_schedule_add", HTTP_POST, handleMateriasNewScheduleAddPOST);
  server.on("/materias_new_schedule_del", HTTP_POST, handleMateriasNewScheduleDelPOST);

  // students
  server.on("/students", handleStudentsForMateria);
  server.on("/students_all", handleStudentsAll);
  server.on("/student_remove_course", HTTP_POST, handleStudentRemoveCourse);
  server.on("/student_delete", HTTP_POST, handleStudentDelete);

  // capture
  server.on("/capture", HTTP_GET, handleCapturePage);
  server.on("/capture_confirm", HTTP_POST, handleCaptureConfirm);
  server.on("/capture_poll", HTTP_GET, handleCapturePoll);
  server.on("/capture_stop", HTTP_GET, handleCaptureStopGET);

  // NUEVAS RUTAS: edición vía capture
  server.on("/capture_edit", HTTP_GET, handleCaptureEditPage);        // abre página de edición
  server.on("/capture_edit_post", HTTP_POST, handleCaptureEditPost);  // procesa guardado

  // status
  server.on("/status", handleStatus);

  // schedules
  server.on("/schedules", HTTP_GET, handleSchedulesGrid);
  server.on("/schedules/edit", HTTP_GET, handleSchedulesEditGrid);
  server.on("/schedules_add_slot", HTTP_POST, handleSchedulesAddSlot);
  server.on("/schedules_del", HTTP_POST, handleSchedulesDel);

  server.on("/schedules_for", HTTP_GET, handleSchedulesForMateriaGET);
  server.on("/schedules_for_add", HTTP_POST, handleSchedulesForMateriaAddPOST);
  server.on("/schedules_for_del", HTTP_POST, handleSchedulesForMateriaDelPOST);

  // notifications
  server.on("/notifications", handleNotificationsPage);
  server.on("/notifications_clear", HTTP_POST, handleNotificationsClearPOST);

  // edit user
  server.on("/edit", handleEditGet);
  server.on("/edit_post", HTTP_POST, handleEditPost);

  // CSV endpoints
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

  // history
  server.on("/history", handleHistoryPage);
  server.on("/history.csv", handleHistoryCSV);
  server.on("/history_clear", HTTP_POST, handleHistoryClearPOST);
  server.on("/materia_history", handleMateriaHistoryGET);
}
