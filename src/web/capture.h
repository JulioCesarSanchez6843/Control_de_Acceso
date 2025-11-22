// src/web/capture.h
#pragma once
#include <Arduino.h>

void handleCapturePage();
void handleCaptureIndividualPage();
void handleCaptureBatchPage();

void handleCapturePoll();
void handleCaptureConfirm();

void handleCaptureStartPOST();
void handleCaptureBatchStopPOST();
void handleCaptureStopGET();

void handleCaptureBatchPollGET();
// *** ELIMINAR estas líneas ***
// void handleCaptureBatchClearPOST();
// void handleCaptureCancelPOST();
void handleCaptureBatchPausePOST();
void handleCaptureRemoveLastPOST();
void handleCaptureGenerateLinksPOST();

void handleCaptureEditPage();
void handleCaptureEditPost();

// NUEVO: terminar y guardar batch
void handleCaptureFinishPOST();