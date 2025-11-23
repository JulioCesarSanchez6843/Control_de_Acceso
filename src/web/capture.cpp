// src/web/capture.cpp  (shim - delega en capture_individual / capture_lote)
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

// Landing (legacy) — lo dejamos por compatibilidad, pero ya no lo usamos en los menús
void handleCapturePage() {
  String html = htmlHeader("Capturar Tarjeta");
  html += "<div class='card'><h2>Capturar Tarjeta</h2>";
  html += "<p class='small'>Acerca la tarjeta. Si ya existe en otra materia se autocompletan los campos. Seleccione un modo:</p>";
  html += "<div style='display:flex;gap:12px;justify-content:center;margin-top:18px;'>";
  html += "<a class='btn btn-blue' href='/capture_individual'>Individual</a>";
  html += "<a class='btn btn-blue' href='/capture_batch'>Batch (varias tarjetas)</a>";
  html += "</div></div>" + htmlFooter();
  server.send(200, "text/html", html);
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
