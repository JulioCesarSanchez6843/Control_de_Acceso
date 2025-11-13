#pragma once
// schedules.h - prototipos para los manejadores de /schedules
// Asegúrate de colocarlo en src/web/ y que web_routes.cpp haga #include "schedules.h"

#include <Arduino.h>
#include "globals.h"

// Rutas / manejadores de horarios
void handleSchedulesGrid();                      // GET /schedules
void handleSchedulesEditGrid();                  // GET /schedules/edit
void handleSchedulesAddSlot();                   // POST /schedules_add_slot
void handleSchedulesDel();                       // POST /schedules_del

void handleSchedulesForMateriaGET();             // GET /schedules_for?materia=...
void handleSchedulesForMateriaAddPOST();         // POST /schedules_for_add
void handleSchedulesForMateriaDelPOST();         // POST /schedules_for_del
