#pragma once

#include <Arduino.h>
#include "globals.h"

void handleSchedulesGrid();                      
void handleSchedulesEditGrid();                  
void handleSchedulesAddSlot();                   
void handleSchedulesDel();                      

void handleSchedulesForMateriaGET();             
void handleSchedulesForMateriaAddPOST();         
void handleSchedulesForMateriaDelPOST();         
