#pragma once
/* display.h
   Prototipos para manejo de TFT, LEDs y servo (presentación al usuario).
   Implementación en src/display.cpp
*/

#include <Arduino.h>

// Inicializa pantalla, servo, LED pins (llamar en setup después de pinMode si aplica)
void displayInit();

// LEDs RGB
void ledOff();
void ledRedOn();
void ledGreenOn();

// Mensajes genéricos en pantalla
void showMessage(const char *title, const String &line1, const String &line2 = "", uint16_t color = 0xFFFF, unsigned long ms = 3000);

// Acceso concedido / denegado (manejan servo y leds internamente)
void showGranted(const String &name, const String &materia, const String &uid, unsigned long ms = 2000);
void showDenied(const String &uid, const String &note = "", unsigned long ms = 2000);
