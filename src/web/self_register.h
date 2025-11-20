// src/web/self_register.h
#pragma once

#include "globals.h"   // contiene la definición de SelfRegSession y selfRegSessions
#include <Arduino.h>

// Handlers web para self-registration
void handleSelfRegisterStartPOST(); 
void handleSelfRegisterGET();
void handleSelfRegisterPost();

// Cancelar/abortar una SelfRegSession (invocado desde el navegador del alumno si pulsa "Cancelar")
void handleSelfRegisterCancelPOST();

// Utilidades (opcionales)
int findSelfRegSessionIndexByToken(const String &token);
void removeSelfRegSessionByIndex(int idx);
