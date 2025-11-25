// src/web/capture_lote.h
#pragma once

// Interfaces públicas usadas por el shim (capture.cpp / web_routes.cpp)
void capture_lote_page();
void capture_lote_batchPollGET();
void capture_lote_pausePOST();
void capture_lote_removeLastPOST();
void capture_lote_generateLinksPOST();
void capture_lote_finishPOST();
