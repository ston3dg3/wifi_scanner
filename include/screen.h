#pragma once

#include <Arduino.h>
#include "network.h"
#include "battery.h"

// Common interface implemented by exactly one of oled.cpp / tft.cpp,
// chosen via USE_TFT_DISPLAY in display_config.h. Callers don't need to
// know which physical screen is attached.

// Initializes the active display. Call once from setup(). Safe to call
// even if the screen isn't wired up - drawing calls become no-ops rather
// than blocking on missing hardware.
void screenInit();

// Renders one page of a rotating summary (overview, top networks, recent
// alerts, recent device joins) and advances to the next page for next
// time. Call periodically, e.g. alongside sendStateOverSerial().
void screenRenderNextPage(const NetworkInfo networks[], int networkCount,
                           const DeviceAssociation associations[], int associationCount,
                           const AlertEvent alerts[], int alertCount,
                           unsigned long totalAlertCount, unsigned long uptimeMs,
                           const BatteryStatus &battery);
