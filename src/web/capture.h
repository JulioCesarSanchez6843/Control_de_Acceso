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
void handleCaptureBatchClearPOST();
void handleCaptureBatchPausePOST();
void handleCaptureRemoveLastPOST();
void handleCaptureCancelPOST();
void handleCaptureGenerateLinksPOST();

void handleCaptureEditPage();
void handleCaptureEditPost();

// NUEVO: terminar y guardar batch
void handleCaptureFinishPOST();
