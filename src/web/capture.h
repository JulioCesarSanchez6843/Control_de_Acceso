#ifndef CAPTURE_H
#define CAPTURE_H

#include <Arduino.h>

void handleCapturePage();
void handleCapturePoll();
void handleCaptureConfirm();
void handleCaptureStopGET();

// Nuevas funciones para edición desde Students
void handleCaptureEditPage();
void handleCaptureEditPost();

// Compatibilidad / rutas batch (nombres que usa web_routes.cpp)
void handleCaptureStartPOST();
void handleCaptureBatchStartGET();
void handleCaptureBatchPollGET();
void handleCaptureBatchClearPOST();
void handleCaptureGenerateLinksPOST();

#endif // CAPTURE_H
