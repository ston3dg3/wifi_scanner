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

// Renders the current screen from a fresh data snapshot. Call
// periodically, e.g. alongside sendStateOverSerial(). Neither backend has
// touch/button input (the TFT panel's touch controller turned out to be
// unpopulated - see docs/wiring.md), so the TFT backend also rotates
// between screens on a timer as part of this call; the OLED backend
// rotates between its pages the same way it always has.
void screenRender(const NetworkInfo networks[], int networkCount,
                   const DeviceAssociation associations[], int associationCount,
                   const AlertEvent alerts[], int alertCount,
                   const PacketEvent packets[], int packetCount,
                   unsigned long totalAlertCount, unsigned long uptimeMs,
                   const BatteryStatus &battery);
