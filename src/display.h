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

// Banner pequeño indicando que hay un self-register en curso (bloquea lecturas)
void showSelfRegisterBanner(const String &uid);

// Indicador modo captura (banner)
void showCaptureMode(bool batch, bool paused);

// Pantalla/overlay para indicar que la captura está en progreso (individual o batch)
void showCaptureInProgress(bool batch, const String &uid);

// Muestra mensaje rojo temporal (por ejemplo "Espere su turno") que reemplaza texto del QR durante durationMs ms
void showTemporaryRedMessage(const String &msg, unsigned long durationMs);

// Función no bloqueante para actualizar el display - debe llamarse en el loop principal
void updateDisplay();

// Nueva función: Cancelar captura y volver a pantalla normal
void cancelCaptureAndReturnToNormal();

// LEDs
void ledOff();
void ledRedOn();
void ledGreenOn();