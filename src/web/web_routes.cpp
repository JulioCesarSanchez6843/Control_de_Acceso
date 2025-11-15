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

// Registra todas las rutas del servidor web.
// Cada entry es equivalente a llamar server.on(...) en setup().
void registerRoutes() {
  // Página principal
  server.on("/", handleRoot);

  // --- Materias: CRUD y pantallas relacionadas ---
  server.on("/materias", handleMaterias);                          // lista materias
  server.on("/materias/new", handleMateriasNew);                   // formulario nueva materia
  server.on("/materias_add", HTTP_POST, handleMateriasAddPOST);    // crear materia (POST)
  server.on("/materias/edit", handleMateriasEditGET);              // editar materia (GET)
  server.on("/materias_edit", HTTP_POST, handleMateriasEditPOST);  // guardar edición (POST)
  server.on("/materias_delete", HTTP_POST, handleMateriasDeletePOST); // borrar materia (POST)

  // Rutas para asignación de horarios por materia (nuevo flujo)
  server.on("/materias_new_schedule", handleMateriasNewScheduleGET);         // ver/editar grilla de horarios para materia
  server.on("/materias_new_schedule_add", HTTP_POST, handleMateriasNewScheduleAddPOST); // agregar horario (POST)
  server.on("/materias_new_schedule_del", HTTP_POST, handleMateriasNewScheduleDelPOST); // eliminar horario (POST)

  // --- Students: ver por materia / ver todos / acciones ---
  server.on("/students", handleStudentsForMateria);               // ver alumnos de una materia
  server.on("/students_all", handleStudentsAll);                  // ver todos los alumnos
  server.on("/student_remove_course", HTTP_POST, handleStudentRemoveCourse); // remover alumno de una materia (POST)
  server.on("/student_delete", HTTP_POST, handleStudentDelete);   // eliminar alumno por completo (POST)

  // --- Captura (modo manual de registrar tarjetas) ---
  server.on("/capture", HTTP_GET, handleCapturePage);             // página captura
  server.on("/capture_confirm", HTTP_POST, handleCaptureConfirm); // confirmar captura (POST)
  server.on("/capture_poll", HTTP_GET, handleCapturePoll);        // polling para UID detectado (ajax)
  server.on("/capture_stop", HTTP_GET, handleCaptureStopGET);     // detener modo captura

  // Edición vía capture (edición de usuario reutilizando UI de captura)
  server.on("/capture_edit", HTTP_GET, handleCaptureEditPage);        // abrir edición
  server.on("/capture_edit_post", HTTP_POST, handleCaptureEditPost);  // procesar edición (POST)

  // Estado / diagnóstico
  server.on("/status", handleStatus);

  // --- Schedules: grilla y edición ---
  server.on("/schedules", HTTP_GET, handleSchedulesGrid);         // ver grilla (solo lectura)
  server.on("/schedules/edit", HTTP_GET, handleSchedulesEditGrid);// editor global de horarios
  server.on("/schedules_add_slot", HTTP_POST, handleSchedulesAddSlot); // agregar slot (POST)
  server.on("/schedules_del", HTTP_POST, handleSchedulesDel);         // eliminar slot (POST)

  // Editor restringido por materia
  server.on("/schedules_for", HTTP_GET, handleSchedulesForMateriaGET);     // ver horarios por materia
  server.on("/schedules_for_add", HTTP_POST, handleSchedulesForMateriaAddPOST); // agregar por materia (POST)
  server.on("/schedules_for_del", HTTP_POST, handleSchedulesForMateriaDelPOST); // eliminar por materia (POST)

  // --- Notificaciones ---
  server.on("/notifications", handleNotificationsPage);         // ver notificaciones
  server.on("/notifications_clear", HTTP_POST, handleNotificationsClearPOST); // borrar todas (POST)

  // --- Edit user (formulario rápido) ---
  server.on("/edit", handleEditGet);                            // abrir formulario edición de usuario
  server.on("/edit_post", HTTP_POST, handleEditPost);           // guardar cambios (POST)

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
  server.on("/history", handleHistoryPage);                     // página historial
  server.on("/history.csv", handleHistoryCSV);                  // descargar CSV (opcional filtros)
  server.on("/history_clear", HTTP_POST, handleHistoryClearPOST);// borrar historial (POST)
  server.on("/materia_history", handleMateriaHistoryGET);       // historial por materia (lista días)
}
