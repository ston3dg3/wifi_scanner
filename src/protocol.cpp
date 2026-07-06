#include "protocol.h"

// Fields are pipe-delimited; SSIDs get any '|' or newline scrubbed out
// before sending so a stray character in an SSID can't desync the parser.
static String sanitize(const String &in) {
  String out = in;
  out.replace('|', '_');
  out.replace('\n', ' ');
  out.replace('\r', ' ');
  return out;
}

void sendStateOverSerial(const NetworkInfo networks[], int networkCount,
                          const DeviceAssociation associations[], int associationCount,
                          const AlertEvent alerts[], int alertCount,
                          unsigned long totalAlertCount, unsigned long uptimeMs,
                          const BatteryStatus &battery) {
  Serial.println("BEGIN");

  for (int i = 0; i < networkCount; i++) {
    Serial.printf("NET|%u|%s|%ld|%d|%s\n",
                  networks[i].channel, sanitize(networks[i].ssid).c_str(),
                  (long)networks[i].rssi, networks[i].secured ? 1 : 0,
                  networks[i].bssid.c_str());
  }

  for (int i = 0; i < associationCount; i++) {
    Serial.printf("ASSOC|%s|%s|%s|%lu\n",
                  associations[i].deviceMac.c_str(), sanitize(associations[i].ssid).c_str(),
                  associations[i].bssid.c_str(), (uptimeMs - associations[i].lastSeenMs) / 1000);
  }

  for (int i = 0; i < alertCount; i++) {
    Serial.printf("ALERT|%s|%s|%s|%lu\n",
                  alerts[i].kind.c_str(), alerts[i].mac1.c_str(), alerts[i].mac2.c_str(),
                  (uptimeMs - alerts[i].timestampMs) / 1000);
  }

  Serial.printf("BATT|%.2f|%.1f|%d|%d\n", battery.voltageV, battery.currentMA,
                battery.percent, battery.sensorReady ? 1 : 0);

  Serial.printf("END|%lu|%lu\n", uptimeMs / 1000, totalAlertCount);
}
