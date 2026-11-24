#pragma once
#include <stdint.h>
#include "dashmodel.h"

// The screen: three portrait pages (DRIVE / LOCATION / SYSTEM) drawn from a DashInput. Drawing only; what
// each field says and which color it gets is decided in lib/dashmodel (tested on the PC).

void displayInit();                                          // screen and touch chip; call once from setup()
void displayNextPage();
void displayTick(const DashInput& in, uint32_t nowMs);       // call often; redraws at most 10 times a second
