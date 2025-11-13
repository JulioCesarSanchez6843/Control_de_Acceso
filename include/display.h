#pragma once
// Encapsula interacción con la pantalla TFT y LEDs/buzzer.
// Implementa en display.cpp las funciones que usan tft y los pines RGB.

#include <Arduino.h>

// Inicialización de display y estado visual
void displayInit();                 // iniciar tft, rotation, cursor básico
void showWaitingMessage();          // pantalla "Esperando tarjeta..."
void showAccessGranted(const String &name, const String &materia, const String &uid);
void showAccessDenied(const String &reason, const String &uid);

// LEDs / feedback
void ledOff();
void ledRedOn();
void ledGreenOn();

// (Opcional) funciones para mensajes breves
void showInfo(const String &title, const String &line1, unsigned long ms = 2000);
