// src/self_register.h
#pragma once

#include "globals.h"   // contiene la definición de SelfRegSession y selfRegSessions
#include <Arduino.h>

// Handlers web para self-registration
// Profesor inicia sesión (POST) -> crea una SelfRegSession
void handleSelfRegisterStartPOST(); 

// Alumno accede con token (GET), muestra formulario
void handleSelfRegisterGET();

// Alumno envía datos del formulario (POST)
void handleSelfRegisterPost();

// Opcional: utilidades expuestas (si las usas en otros módulos)
// Buscar sesión por token (devuelve index o -1 si no existe)
int findSelfRegSessionIndexByToken(const String &token);

// Elimina una sesión por index (si quieres exponerlo)
void removeSelfRegSessionByIndex(int idx);
