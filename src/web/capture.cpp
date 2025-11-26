#include "capture.h"
#include "capture_common.h"

// NECESARIO: incluir web_common para htmlHeader/htmlFooter
#include "web_common.h"

// Declarations implemented in capture_individual.cpp / capture_lote.cpp
void capture_individual_page();
void capture_individual_poll();
void capture_individual_confirm();
void capture_individual_startPOST();
void capture_individual_stopGET();
void capture_individual_editPage();
void capture_individual_editPost();

void capture_lote_page();
void capture_lote_batchPollGET();
void capture_lote_pausePOST();
void capture_lote_removeLastPOST();
void capture_lote_generateLinksPOST();
void capture_lote_finishPOST();

void handleCaptureInit() {
  // placeholder para compatibilidad; no hace nada por ahora
}

// Landing (legacy) — redirigimos a /capture_individual para eliminar el menú,
// pero mantenemos la ruta por compatibilidad con clientes que aún la usen.
void handleCapturePage() {
  // Redirigir al flujo individual. Cambia la URL si quieres otro comportamiento por defecto.
  server.sendHeader("Location", "/capture_individual");
  server.send(303, "text/plain", "redirect");
}

// Delegation handlers (mantienen los nombres originales usados por web_routes.cpp)
void handleCaptureIndividualPage() { capture_individual_page(); }
void handleCaptureBatchPage()      { capture_lote_page(); }

void handleCapturePoll()           { capture_individual_poll(); }
void handleCaptureConfirm()        { capture_individual_confirm(); }

void handleCaptureStartPOST()      { capture_individual_startPOST(); }
void handleCaptureBatchStopPOST()  { capture_individual_stopGET(); } // legacy mapping
void handleCaptureStopGET()        { capture_individual_stopGET(); }

void handleCaptureBatchPollGET()   { capture_lote_batchPollGET(); }
void handleCaptureBatchPausePOST() { capture_lote_pausePOST(); }
void handleCaptureRemoveLastPOST() { capture_lote_removeLastPOST(); }
void handleCaptureGenerateLinksPOST() { capture_lote_generateLinksPOST(); }
void handleCaptureFinishPOST()     { capture_lote_finishPOST(); }

void handleCaptureEditPage() { capture_individual_editPage(); }
void handleCaptureEditPost() { capture_individual_editPost(); }
