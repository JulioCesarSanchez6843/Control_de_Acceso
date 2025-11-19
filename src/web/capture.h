#ifndef CAPTURE_H
#define CAPTURE_H

#include <Arduino.h>

// Páginas
void handleCapturePage();             // /capture  -> landing con dos botones
void handleCaptureIndividualPage();   // /capture_individual -> formulario individual (arranca modo)
void handleCaptureBatchPage();        // /capture_batch -> UI batch (arranca modo)

// Poll / confirm / stop
void handleCapturePoll();             // /capture_poll
void handleCaptureConfirm();          // /capture_confirm (POST)
void handleCaptureStopGET();          // /capture_stop

// Batch endpoints
void handleCaptureBatchPollGET();     // /capture_batch_poll (GET) -> lista UIDs
void handleCaptureBatchStopPOST();    // /capture_batch_stop (POST)
void handleCaptureBatchClearPOST();   // /capture_clear_queue (POST)
void handleCaptureGenerateLinksPOST();// /capture_generate_links (POST)

// Edición desde Students
void handleCaptureEditPage();
void handleCaptureEditPost();

// Compatibilidad (si hay código antiguo que use /capture_start)
void handleCaptureStartPOST();        // opcional, mantiene compatibilidad

#endif // CAPTURE_H
