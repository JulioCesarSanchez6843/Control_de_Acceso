// src/display.h
#pragma once
#include <Arduino.h>

// Inicialización y pantallas comunes
void displayInit();
void showWaitingMessage();
void showAccessGranted(const String &name, const String &materia, const String &uid);
void showAccessDenied(const String &reason, const String &uid);

// Mostrar un QR en pantalla con la URL. pixelBoxSize es el tamaño total (en px) sugerido.
void showQRCodeOnDisplay(const String &url, int pixelBoxSize);

// Mostrar pequeño banner indicando que hay un self-register en curso (bloquea lecturas)
void showSelfRegisterBanner(const String &uid);

// Mostrar indicador de modo captura (overlay): batch = true -> "Batch", false -> "Individual".
// paused = true mostrará que está en pausa (útil para batch).
void showCaptureMode(bool batch, bool paused);

// LEDs
void ledOff();
void ledRedOn();
void ledGreenOn();
