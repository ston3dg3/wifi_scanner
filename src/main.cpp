#include <Arduino.h>
#include "network.h"
#include "protocol.h"
#include "screen.h"
#include "battery.h"

#define LED D4 // Onboard LED, GPIO2 on NodeMCU v3
#define SEND_INTERVAL_MS 3000

// Static rather than loop-local: each holds a handful of Strings per entry,
// so re-allocating them on the stack every 3s would eat into the limited
// task stack for no reason.
static NetworkInfo networks[MAX_NETWORKS];
static DeviceAssociation associations[MAX_ASSOCIATIONS];
static AlertEvent alerts[MAX_ALERTS];

// Serial commands:
//   beacon - broadcast a fake AP for 5s (see network.cpp)
void handleSerialCommands() {
  if (!Serial.available()) return;
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "beacon") {
    Serial.println("Broadcasting fake beacon 'ESP8266-Fake-AP' on channel 6 for 5s...");
    broadcastFakeNetwork("ESP8266-Fake-AP", 6, 5000);
    Serial.println("Done.");
  }
}

void setup() {
  pinMode(LED, OUTPUT);
  Serial.begin(9600);
  networkInit(); // station mode + always-on passive frame sniffer
  screenInit();
  batteryInit();
  Serial.println("WiFi Analyzer booting...");
  Serial.println("Commands: 'beacon' = broadcast fake AP for 5s");
}

void loop() {
  handleSerialCommands();

  static unsigned long lastSend = 0;
  static bool ledOn = false;
  if (millis() - lastSend < SEND_INTERVAL_MS) return;
  lastSend = millis();

  int networkCount = getNetworks(networks, MAX_NETWORKS);
  int associationCount = getAssociations(associations, MAX_ASSOCIATIONS);
  int alertCount = getRecentAlerts(alerts, MAX_ALERTS);
  BatteryStatus battery = batteryRead();

  sendStateOverSerial(networks, networkCount, associations, associationCount,
                       alerts, alertCount, getTotalAlertCount(), millis(), battery);
  screenRenderNextPage(networks, networkCount, associations, associationCount,
                        alerts, alertCount, getTotalAlertCount(), millis(), battery);

  ledOn = !ledOn;
  digitalWrite(LED, ledOn ? LOW : HIGH); // active-LOW heartbeat blink
}
