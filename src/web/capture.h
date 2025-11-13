#pragma once
// Handlers para captura de nuevas tarjetas (capture page, poll, confirm, stop)
#include <Arduino.h>

void handleCapturePage();
void handleCapturePoll();
void handleCaptureConfirm();
void handleCaptureStopGET();
