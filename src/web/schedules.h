#pragma once

#include <Arduino.h>
#include "globals.h"

void handleSchedulesGrid();
void handleSchedulesEditGrid();
void handleSchedulesAddSlot();
void handleSchedulesDel();

void handleSchedulesForMateriaGET();
void handleSchedulesForMateriaAddPOST();
void handleSchedulesForMateriaDelPOST();

// Función de utilidad usada por courses.cpp para eliminar un slot de horario.
// courseKey puede ser "materia||profesor" o simplemente "materia".
// Retorna true si el slot fue encontrado y eliminado (o ya no existía).
bool deleteScheduleSlot(const String &courseKey, const String &day, const String &start);