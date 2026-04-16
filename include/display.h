#pragma once
#include <Arduino.h>

// Inicialización y pantallas comunes
void displayInit();
void showWaitingMessage();
void showAccessGranted(const String &name, const String &materia, const String &uid);
void showAccessDenied(const String &reason, const String &uid);

// QR
void showQRCodeOnDisplay(const String &url, int pixelBoxSize);

// Self-register
// ⚠️ El parámetro realmente NO se usa, pero lo dejamos consistente
void showSelfRegisterBanner(const String &uid);

// Captura
void showCaptureMode(bool batch, bool paused);
void showCaptureInProgress(bool batch, const String &uid);
void cancelCaptureAndReturnToNormal();

// Mensajes temporales
void showTemporaryRedMessage(const String &msg, unsigned long durationMs);

// Actualización no bloqueante
void updateDisplay();

// LEDs
void ledOff();
void ledRedOn();
void ledGreenOn();