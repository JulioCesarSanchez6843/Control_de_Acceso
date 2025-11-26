#pragma once

#include <vector>
#include <WString.h>

// Declaraciones públicas de los handlers de "materias" / "courses"

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

// Export: obtener profesores para una materia (necesario para otros módulos)
std::vector<String> getProfessorsForMateria(const String &materia);

// (opcional) lista de nombres de profesores registrados en TEACHERS_FILE
std::vector<String> loadRegisteredTeachersNames();
