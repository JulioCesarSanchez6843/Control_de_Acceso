// src/web/capture.h
#pragma once
#include <Arduino.h>

// Página de selección (landing)
void handleCapturePage();

// Individual
void handleCaptureIndividualPage();
void handleCapturePoll();           // GET /capture_poll (JSON)
void handleCaptureConfirm();        // POST /capture_confirm
void handleCaptureStartPOST();      // POST /capture_start
void handleCaptureStopGET();        // GET /capture_stop

// Batch / Lote
void handleCaptureBatchPage();
void handleCaptureBatchPollGET();   // GET /capture_batch_poll
void handleCaptureBatchStopPOST();  // POST /capture_batch_stop
void handleCaptureBatchPausePOST(); // POST /capture_batch_pause
void handleCaptureRemoveLastPOST(); // POST /capture_remove_last
void handleCaptureGenerateLinksPOST(); // POST /capture_generate_links
void handleCaptureFinishPOST();     // POST /capture_finish

// Edit (used from students / teachers lists)
void handleCaptureEditPage();       // GET /capture_edit
void handleCaptureEditPost();       // POST /capture_edit_post
