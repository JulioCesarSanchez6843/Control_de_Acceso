#pragma once
/* rfid_handler.h
   Prototypes para inicialización y loop RFID.
   También declara (extern) las variables de captura que usan la UI web.
*/

#include <Arduino.h>

// Inicialización del hardware RFID (llamar en setup)
void rfidInit();

// Handler a llamar periódicamente desde loop()
void rfidLoopHandler();

// Capture mode globals (se definen en main.cpp)
extern volatile bool captureMode;
extern String captureUID;
extern String captureName;
extern String captureAccount;
extern unsigned long captureDetectedAt;

// NOTA: *No* definir CAPTURE_DEBOUNCE_MS aquí con #define si ya la defines
// como `const unsigned long CAPTURE_DEBOUNCE_MS = 3000UL;` en main.cpp
