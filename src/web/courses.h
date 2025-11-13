#pragma once

// Declaraciones públicas de los handlers de "materias" / "courses"
// (asegúrate de que src/web/courses.cpp implemente todas estas funciones)

void registerCoursesHandlers();

void handleMaterias();
void handleMateriasNew();
void handleMateriasAddPOST();

void handleMateriasNewScheduleGET();
void handleMateriasNewScheduleAddPOST();
void handleMateriasNewScheduleDelPOST();

void handleMateriasEditGET();
void handleMateriasEditPOST();
void handleMateriasDeletePOST();
