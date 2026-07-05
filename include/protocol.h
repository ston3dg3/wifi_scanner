#pragma once

#include <Arduino.h>
#include "network.h"

// Emits the full sniffer state as a compact, pipe-delimited, line-based
// protocol over Serial for the companion Python GUI (gui.py) to parse.
// Far lighter than redrawing an ASCII table every cycle, which matters at
// slow baud rates like 9600. Format per line:
//   BEGIN
//   NET|channel|ssid|rssi|secured(0/1)|bssid
//   ASSOC|deviceMac|ssid|bssid|lastSeenSecondsAgo
//   ALERT|kind|mac1|mac2|ageSecondsAgo
//   END|uptimeSeconds|totalAlertCount
// Any other line (boot messages, "beacon" command feedback, etc.) is plain
// text and the GUI just logs it rather than parsing it.
void sendStateOverSerial(const NetworkInfo networks[], int networkCount,
                          const DeviceAssociation associations[], int associationCount,
                          const AlertEvent alerts[], int alertCount,
                          unsigned long totalAlertCount, unsigned long uptimeMs);
