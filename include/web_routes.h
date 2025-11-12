#pragma once
/* web_routes.h
   Declaración de todos los manejadores HTTP (handlers) expuestos por web_routes.cpp
   Estos prototipos se registran en main.cpp con server.on(...)
*/

#include <Arduino.h>

// ---------- Root ----------
void handleRoot();

// ---------- Materias ----------
void handleMaterias();
void handleMateriasNew();
void handleMateriasAddPOST();
void handleMateriasEditGET();
void handleMateriasEditPOST();
void handleMateriasDeletePOST();

// ---------- Students / Usuarios ----------
void handleStudentsForMateria();
void handleStudentsAll();
void handleStudentRemoveCourse();
void handleStudentDelete();

// ---------- Capture (tarjetas) ----------
void handleCapturePage();
void handleCaptureStopGET();
void handleCapturePoll();
void handleCaptureConfirm();

// ---------- Status ----------
void handleStatus();

// ---------- Horarios (Schedules) ----------
void handleSchedulesGrid();
void handleSchedulesEditGrid();
void handleSchedulesAddSlot();
void handleSchedulesDel();

void handleSchedulesForMateriaGET();
void handleSchedulesForMateriaAddPOST();
void handleSchedulesForMateriaDelPOST();

// ---------- Notificaciones ----------
void handleNotificationsPage();
void handleNotificationsClearPOST();

// ---------- Edición de usuarios ----------
void handleEditGet();
void handleEditPost();

// ---------- CSV / Historial ----------
void handleUsersCSV();
void handleHistoryPage();
void handleHistoryCSV();
void handleHistoryClearPOST();
void handleMateriaHistoryGET();
