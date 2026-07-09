#pragma once

#include <Arduino.h>

// Derived from the beacon's capability info Privacy bit plus its RSN (WPA2/3)
// and vendor WPA1 tagged parameters - see parseSecurity() in network.cpp.
// SEC_SECURED is a fallback for "Privacy bit set but the capture was
// truncated before an RSN/WPA IE could be found" - the sniffer's fixed-size
// packet buffer can cut off beacons with many tagged params before reaching
// those IEs, so this avoids mislabeling an unread WPA2/3 network as WEP.
enum SecurityType : uint8_t {
  SEC_OPEN = 0,
  SEC_WEP,
  SEC_WPA,
  SEC_WPA2,
  SEC_WPA3,
  SEC_WPA2_WPA3,
  SEC_SECURED,
};

const char *securityLabel(SecurityType s);

struct NetworkInfo {
  String ssid;
  int32_t rssi;
  uint8_t channel;
  SecurityType security;
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

// A filtered feed of "interesting" 802.11 management frames - broader than
// AlertEvent (which is just the deauth family, for the existing serial
// protocol/OLED alerts page), meant for a live-ish scrolling view. Not
// every frame: beacons alone fire ~every 100ms per AP, so only a network's
// *first* beacon is logged (PKT_NEW_NETWORK), not every refresh.
enum PacketKind : uint8_t {
  PKT_NEW_NETWORK, // first beacon seen for a BSSID
  PKT_PROBE,       // probe request - a device looking for a network
  PKT_AUTH,        // authentication frame (start of a connection handshake)
  PKT_ASSOC,       // association request (joining an AP)
  PKT_REASSOC,     // reassociation request (roaming/rejoining)
  PKT_DEAUTH,
  PKT_DISASSOC,
  PKT_REAUTH,      // reconnect attempt shortly after a deauth/disassoc
};

const char *packetKindLabel(PacketKind k);

struct PacketEvent {
  PacketKind kind;
  String mac;    // primary actor: the AP for a new network, the device for everything else
  String detail; // SSID (new network/probe) or peer MAC (auth/assoc/deauth/disassoc/reauth)
  int32_t rssi;
  uint8_t channel;
  unsigned long timestampMs;
};

#define MAX_NETWORKS 32
#define MAX_ASSOCIATIONS 16
#define MAX_ALERTS 8
#define MAX_PACKET_LOG 20

// Puts the radio into station mode and starts the always-on passive frame
// sniffer (channel-hopping across 1-13). Call once from setup().
void networkInit();

// Snapshots of state the sniffer has passively collected off the air.
// Each returns the number of entries written into `out`.
int getNetworks(NetworkInfo out[], int maxCount);
int getAssociations(DeviceAssociation out[], int maxCount);
int getRecentAlerts(AlertEvent out[], int maxCount); // most recent first
int getRecentPackets(PacketEvent out[], int maxCount); // most recent first
unsigned long getTotalAlertCount();
bool isFrameSnifferActive();

// --- Beacon frame crafting ---
// Broadcasts a fake AP advertising `ssid` on `channel` for `durationMs`.
// Only advertises a network; never targets or disconnects real devices.
void broadcastFakeNetwork(const char *ssid, uint8_t channel, unsigned long durationMs);
