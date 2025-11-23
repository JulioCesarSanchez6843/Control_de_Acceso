// capture.h
#pragma once
#include <Arduino.h>

// Rutas públicas (mantienen la API original)
void handleCapturePage();
void handleCaptureIndividualPage();
void handleCaptureBatchPage();

void handleCapturePoll();
void handleCaptureConfirm();

void handleCaptureStartPOST();
void handleCaptureBatchStopPOST();
void handleCaptureStopGET();

void handleCaptureBatchPollGET();
void handleCaptureBatchPausePOST();
void handleCaptureRemoveLastPOST();
void handleCaptureGenerateLinksPOST();
void handleCaptureFinishPOST();

void handleCaptureEditPage();
void handleCaptureEditPost();

// Inicializador opcional
void handleCaptureInit();
