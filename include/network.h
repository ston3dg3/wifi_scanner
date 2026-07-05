#pragma once

#include <Arduino.h>

struct NetworkInfo {
  String ssid;
  int32_t rssi;
  uint8_t channel;
  bool secured;
  String bssid;
  unsigned long lastSeenMs;
};

struct DeviceAssociation {
  String deviceMac;
  String bssid;
  String ssid; // resolved from the sniffed network table when known, else "?"
  unsigned long lastSeenMs;
};

struct AlertEvent {
  String kind; // "DEAUTH", "DISASSOC", "REAUTH"
  String mac1;
  String mac2;
  unsigned long timestampMs;
};

#define MAX_NETWORKS 32
#define MAX_ASSOCIATIONS 16
#define MAX_ALERTS 8

// Puts the radio into station mode and starts the always-on passive frame
// sniffer (channel-hopping across 1-13). Call once from setup().
void networkInit();

// Snapshots of state the sniffer has passively collected off the air.
// Each returns the number of entries written into `out`.
int getNetworks(NetworkInfo out[], int maxCount);
int getAssociations(DeviceAssociation out[], int maxCount);
int getRecentAlerts(AlertEvent out[], int maxCount); // most recent first
unsigned long getTotalAlertCount();
bool isFrameSnifferActive();

// --- Beacon frame crafting ---
// Broadcasts a fake AP advertising `ssid` on `channel` for `durationMs`.
// Only advertises a network; never targets or disconnects real devices.
void broadcastFakeNetwork(const char *ssid, uint8_t channel, unsigned long durationMs);
