// src/web/teachers.h
#pragma once
#include <Arduino.h>

// Handlers web para maestros
void handleTeachersForMateria();   // GET /teachers?materia=...
void handleTeachersAll();          // GET /teachers_all
void handleTeacherRemoveCourse();  // POST /teacher_remove_course
void handleTeacherDelete();        // POST /teacher_delete
