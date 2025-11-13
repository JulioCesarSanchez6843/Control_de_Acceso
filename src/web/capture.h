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

#endif // CAPTURE_H
