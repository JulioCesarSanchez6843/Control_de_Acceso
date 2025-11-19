// src/web/capture.h
#pragma once
#include <Arduino.h>

// Páginas / handlers - Captura
void handleCapturePage();
void handleCaptureIndividualPage();
void handleCaptureBatchPage();

// Individual
void handleCapturePoll();
void handleCaptureConfirm();
void handleCaptureStartPOST(); // compat

// General stop
void handleCaptureStopGET();

// Batch endpoints (GET/POST)
void handleCaptureBatchPollGET();
void handleCaptureBatchStopPOST();
void handleCaptureBatchClearPOST();
void handleCaptureBatchPausePOST();
void handleCaptureRemoveLastPOST();
void handleCaptureCancelPOST();
void handleCaptureGenerateLinksPOST();

// Edit / util
void handleCaptureEditPage();
void handleCaptureEditPost();
