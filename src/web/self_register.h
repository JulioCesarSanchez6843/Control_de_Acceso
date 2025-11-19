#pragma once
// self_register.h - endpoints para auto-registro (alumno)

/*
  Provee:
    POST  /self_register_start   -> handleSelfRegisterStartPOST
    GET   /self_register         -> handleSelfRegisterGET
    POST  /self_register_submit  -> handleSelfRegisterPost

  Nota: la estructura SelfRegSession está definida en globals.h para evitar
  redefiniciones en múltiples unidades de traducción.
*/

#include <Arduino.h>
#include "globals.h"

// Handlers (implementados en self_register.cpp)
void handleSelfRegisterStartPOST();
void handleSelfRegisterGET();
void handleSelfRegisterPost();
