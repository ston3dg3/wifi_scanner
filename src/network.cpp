#include "network.h"
#include <ESP8266WiFi.h>
#include <Ticker.h>

extern "C" {
  #include <user_interface.h>
}

// ---------------------------------------------------------------------
// Beacon frame crafting - broadcasts a fake "AP" so its SSID shows up in
// nearby WiFi scans. This only advertises a network; it never targets or
// disconnects anyone, so it's safe to demo without affecting other people's
// devices (still, keep it brief and on hardware/space you control).
// ---------------------------------------------------------------------

static uint8_t beaconTemplate[38] = {
  0x80, 0x00,                          // Frame Control: Management / Beacon
  0x00, 0x00,                          // Duration
  0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,  // Destination: broadcast
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // Source MAC (patched per call)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // BSSID (patched per call, = source)
  0x00, 0x00,                          // Sequence/fragment number (hw fills)
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // Timestamp (hw fills)
  0x64, 0x00,                          // Beacon interval: 100 TU (~102ms)
  0x01, 0x04,                          // Capability info: ESS, open
  0x00, 0x00                           // Tag 0 (SSID), length placeholder
};

static const uint8_t beaconTail[] = {
  0x01, 0x08, 0x82, 0x84, 0x8b, 0x96, 0x24, 0x30, 0x48, 0x6c, // Supported rates
  0x03, 0x01, 0x01                     // Tag 3: DS Parameter Set, channel placeholder
};

static void sendFakeBeacon(const char *ssid, uint8_t channel, const uint8_t mac[6]) {
  uint8_t packet[128];
  uint8_t ssidLen = strlen(ssid);
  if (ssidLen > 32) ssidLen = 32; // 802.11 SSID max length

  memcpy(packet, beaconTemplate, sizeof(beaconTemplate));
  memcpy(&packet[10], mac, 6); // source address
  memcpy(&packet[16], mac, 6); // BSSID
  packet[37] = ssidLen;        // SSID tag length
  memcpy(&packet[38], ssid, ssidLen);

  uint8_t *tail = &packet[38 + ssidLen];
  memcpy(tail, beaconTail, sizeof(beaconTail));
  tail[sizeof(beaconTail) - 1] = channel; // patch DS Parameter Set channel byte

  int totalLen = 38 + ssidLen + sizeof(beaconTail);

  wifi_set_channel(channel);
  wifi_send_pkt_freedom(packet, totalLen, false);
}

// ---------------------------------------------------------------------
// Frame sniffer - passive promiscuous-mode listening only, never transmits.
// Silently builds up in-memory state (networks seen, devices that joined
// them, and deauth/disassoc/reauth alerts); the display layer renders
// snapshots of this state periodically instead of the callback printing
// directly (the callback fires from SDK/interrupt context, so it should
// stay cheap and never touch Serial).
// ---------------------------------------------------------------------

struct RxControl {
  signed rssi : 8;
  unsigned rate : 4;
  unsigned is_group : 1;
  unsigned : 1;
  unsigned sig_mode : 2;
  unsigned legacy_length : 12;
  unsigned damatch0 : 1;
  unsigned damatch1 : 1;
  unsigned bssidmatch0 : 1;
  unsigned bssidmatch1 : 1;
  unsigned MCS : 7;
  unsigned CWB : 1;
  unsigned HT_length : 16;
  unsigned Smoothing : 1;
  unsigned Not_Sounding : 1;
  unsigned : 1;
  unsigned Aggregation : 1;
  unsigned STBC : 2;
  unsigned FEC_CODING : 1;
  unsigned SGI : 1;
  unsigned rxend_state : 8;
  unsigned ampdu_cnt : 8;
  unsigned channel : 4;
  unsigned : 12;
};

// SDK sniffer buffer size - real-world beacons with many tagged params (HT
// caps, vendor IEs, RSN, ...) routinely exceed this, so pkt->len can report
// a frame length longer than what's actually sitting in pkt->data. Anything
// walking tagged parameters must clamp to this, not trust pkt->len alone,
// or it reads past the buffer.
#define SNIFFER_CAPTURE_LEN 112

struct SnifferPacket {
  struct RxControl rx_ctrl;
  uint8_t data[SNIFFER_CAPTURE_LEN];
  uint16_t cnt;
  uint16_t len;
};

static bool sniffing = false;
static uint8_t hopChannel = 1;
static Ticker channelHopTicker;
static unsigned long totalAlertCount = 0;

static void hopToNextChannel() {
  hopChannel++;
  if (hopChannel > 13) hopChannel = 1;
  wifi_set_channel(hopChannel);
}

static void macToStr(const uint8_t *mac, char *out) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

// Reads the SSID tag (tag number 0) out of a beacon frame body. `tagStart`
// is where tagged parameters begin: 36 for beacons (24-byte MAC header +
// 12-byte fixed fields: timestamp + beacon interval + capability info).
static void extractSsid(const uint8_t *frame, uint16_t frameLen, uint16_t tagStart,
                         char *outSsid, size_t outSize) {
  outSsid[0] = '\0';
  if (frameLen <= (uint16_t)(tagStart + 2)) return;
  if (frame[tagStart] != 0x00) return; // tag 0 = SSID
  uint8_t tagLen = frame[tagStart + 1];
  if (tagLen == 0) {
    strncpy(outSsid, "<hidden>", outSize);
    return;
  }
  if (tagLen > outSize - 1) tagLen = outSize - 1;
  if ((uint16_t)(tagStart + 2 + tagLen) > frameLen) return; // capture got truncated
  memcpy(outSsid, &frame[tagStart + 2], tagLen);
  outSsid[tagLen] = '\0';
}

// --- Packet log (filtered feed of interesting frames, see network.h) ---

static PacketEvent packetLog[MAX_PACKET_LOG];
static int packetLogCount = 0; // entries filled so far, capped at MAX_PACKET_LOG
static int packetLogNext = 0;  // ring buffer write cursor

static void addPacketEvent(PacketKind kind, const String &mac, const String &detail,
                            int32_t rssi, uint8_t channel) {
  packetLog[packetLogNext].kind = kind;
  packetLog[packetLogNext].mac = mac;
  packetLog[packetLogNext].detail = detail;
  packetLog[packetLogNext].rssi = rssi;
  packetLog[packetLogNext].channel = channel;
  packetLog[packetLogNext].timestampMs = millis();
  packetLogNext = (packetLogNext + 1) % MAX_PACKET_LOG;
  if (packetLogCount < MAX_PACKET_LOG) packetLogCount++;
}

const char *packetKindLabel(PacketKind k) {
  switch (k) {
    case PKT_NEW_NETWORK: return "NEW AP";
    case PKT_PROBE: return "PROBE";
    case PKT_AUTH: return "AUTH";
    case PKT_ASSOC: return "ASSOC";
    case PKT_REASSOC: return "REASSOC";
    case PKT_DEAUTH: return "DEAUTH";
    case PKT_DISASSOC: return "DISASSOC";
    case PKT_REAUTH: return "REAUTH";
  }
  return "?";
}

// --- Sniffed network table (built from beacon frames) ---

struct NetworkSlot {
  bool used;
  uint8_t bssidBytes[6];
  NetworkInfo info;
};

static NetworkSlot networkTable[MAX_NETWORKS];

// `outIsNew` tells the caller whether this BSSID was just allocated (first
// beacon ever seen from it) vs. an existing entry being refreshed - used to
// log a PKT_NEW_NETWORK event only once per network instead of every beacon.
static NetworkSlot *findOrAllocNetwork(const uint8_t bssidBytes[6], bool *outIsNew) {
  int freeSlot = -1;
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (networkTable[i].used && memcmp(networkTable[i].bssidBytes, bssidBytes, 6) == 0) {
      *outIsNew = false;
      return &networkTable[i];
    }
    if (!networkTable[i].used && freeSlot == -1) freeSlot = i;
  }
  if (freeSlot == -1) { *outIsNew = false; return nullptr; } // table full, drop silently
  networkTable[freeSlot].used = true;
  memcpy(networkTable[freeSlot].bssidBytes, bssidBytes, 6);
  char bssidStr[18];
  macToStr(bssidBytes, bssidStr);
  networkTable[freeSlot].info.bssid = bssidStr;
  *outIsNew = true;
  return &networkTable[freeSlot];
}

static String lookupSsidForBssid(const uint8_t bssidBytes[6]) {
  for (int i = 0; i < MAX_NETWORKS; i++) {
    if (networkTable[i].used && memcmp(networkTable[i].bssidBytes, bssidBytes, 6) == 0) {
      return networkTable[i].info.ssid;
    }
  }
  return "?";
}

const char *securityLabel(SecurityType s) {
  switch (s) {
    case SEC_OPEN: return "Open";
    case SEC_WEP: return "WEP";
    case SEC_WPA: return "WPA";
    case SEC_WPA2: return "WPA2";
    case SEC_WPA3: return "WPA3";
    case SEC_WPA2_WPA3: return "WPA2/3";
    case SEC_SECURED: return "Secured";
  }
  return "?";
}

// Walks a beacon's tagged parameters looking for an RSN IE (tag 48, WPA2/3)
// or a vendor WPA1 IE (tag 221, OUI 00:50:F2 type 1) to tell WEP/WPA/WPA2/
// WPA3 apart instead of just "some encryption is on". `frameLen` here must
// already be clamped to SNIFFER_CAPTURE_LEN by the caller; `truncated`
// says whether the *real* frame was longer than that clamp, so a "no RSN/
// WPA IE found" result can be told apart from "we never got to look".
static SecurityType parseSecurity(const uint8_t *frame, uint16_t frameLen, uint16_t tagStart,
                                   bool privacy, bool truncated) {
  if (!privacy) return SEC_OPEN;

  bool hasRSN = false, hasWPA1 = false;
  bool akmPsk = false, akmSae = false;

  uint16_t pos = tagStart;
  while (pos + 2 <= frameLen) {
    uint8_t tagNum = frame[pos];
    uint8_t tagLen = frame[pos + 1];
    uint16_t dataStart = pos + 2;
    if (dataStart + tagLen > frameLen) break; // truncated capture, stop walking

    if (tagNum == 48 && tagLen >= 8) { // RSN Information
      hasRSN = true;
      uint16_t tagEnd = dataStart + tagLen;
      uint16_t p = dataStart + 2 + 4; // skip version(2) + group cipher suite(4)
      if (p + 2 <= tagEnd) {
        uint16_t pairwiseCount = frame[p] | (frame[p + 1] << 8);
        p += 2 + pairwiseCount * 4;
        if (p + 2 <= tagEnd) {
          uint16_t akmCount = frame[p] | (frame[p + 1] << 8);
          p += 2;
          for (uint16_t k = 0; k < akmCount && p + 4 <= tagEnd; k++) {
            uint8_t akmType = frame[p + 3]; // last byte of the 00-0F-AC:x AKM suite
            if (akmType == 1 || akmType == 2 || akmType == 3 || akmType == 4 ||
                akmType == 5 || akmType == 6) akmPsk = true; // PSK/802.1X family -> WPA2-tier
            if (akmType == 8 || akmType == 9 || akmType == 18) akmSae = true; // SAE/OWE -> WPA3-tier
            p += 4;
          }
        }
      }
    } else if (tagNum == 221 && tagLen >= 4 &&
               frame[dataStart] == 0x00 && frame[dataStart + 1] == 0x50 &&
               frame[dataStart + 2] == 0xF2 && frame[dataStart + 3] == 0x01) {
      hasWPA1 = true; // vendor-specific WPA1 IE
    }

    pos = dataStart + tagLen;
  }

  if (hasRSN && akmSae && akmPsk) return SEC_WPA2_WPA3;
  if (hasRSN && akmSae) return SEC_WPA3;
  if (hasRSN) return SEC_WPA2;
  if (hasWPA1) return SEC_WPA;
  if (truncated) return SEC_SECURED; // privacy bit set but we can't say more
  return SEC_WEP; // privacy bit set, no RSN/WPA1 IE anywhere in the full capture
}

static void handleBeacon(const uint8_t *frame, uint16_t frameLen, int8_t rssi) {
  bool truncated = frameLen > SNIFFER_CAPTURE_LEN;
  uint16_t capLen = truncated ? SNIFFER_CAPTURE_LEN : frameLen;

  char ssid[33];
  extractSsid(frame, capLen, 36, ssid, sizeof(ssid));
  bool privacy = (frame[34] & 0x10) != 0; // capability info: Privacy bit

  bool isNew;
  NetworkSlot *slot = findOrAllocNetwork(&frame[16], &isNew); // Address 3: BSSID
  if (!slot) return;
  slot->info.ssid = ssid;
  slot->info.rssi = rssi;
  slot->info.channel = hopChannel;
  slot->info.security = parseSecurity(frame, capLen, 36, privacy, truncated);
  slot->info.lastSeenMs = millis();

  if (isNew) addPacketEvent(PKT_NEW_NETWORK, slot->info.bssid, ssid, rssi, hopChannel);
}

// Probe requests have no fixed fields between the header and tagged params
// (unlike beacons, which have an 12-byte timestamp+interval+capability
// block first) - tags start right at byte 24.
static void handleProbeRequest(const uint8_t *frame, uint16_t frameLen, int32_t rssi) {
  uint16_t capLen = frameLen > SNIFFER_CAPTURE_LEN ? SNIFFER_CAPTURE_LEN : frameLen;

  char ssid[33];
  extractSsid(frame, capLen, 24, ssid, sizeof(ssid));
  // extractSsid() reports a zero-length SSID tag as "<hidden>", which fits
  // beacons (an AP hiding its own name) but not probes, where a zero-length
  // tag means "wildcard" - the device is asking about any network.
  String detail = (strcmp(ssid, "<hidden>") == 0) ? String("<broadcast>") : String(ssid);

  char macStr[18];
  macToStr(&frame[10], macStr); // Address 2: transmitter, the probing device
  addPacketEvent(PKT_PROBE, macStr, detail, rssi, hopChannel);
}

// --- Device association table (built from (re)association requests) ---

struct AssociationSlot {
  bool used;
  uint8_t deviceMacBytes[6];
  DeviceAssociation info;
};

static AssociationSlot associationTable[MAX_ASSOCIATIONS];

static void handleAssociationRequest(const uint8_t *frame, uint8_t subtype, int32_t rssi) {
  const uint8_t *deviceMac = &frame[10]; // Address 2: the client
  const uint8_t *bssid = &frame[4];      // Address 1: the AP it's joining

  AssociationSlot *slot = nullptr;
  int freeSlot = -1;
  for (int i = 0; i < MAX_ASSOCIATIONS; i++) {
    if (associationTable[i].used && memcmp(associationTable[i].deviceMacBytes, deviceMac, 6) == 0) {
      slot = &associationTable[i];
      break;
    }
    if (!associationTable[i].used && freeSlot == -1) freeSlot = i;
  }
  if (!slot) {
    if (freeSlot == -1) return; // table full, drop silently
    slot = &associationTable[freeSlot];
    slot->used = true;
    memcpy(slot->deviceMacBytes, deviceMac, 6);
    char deviceMacStr[18];
    macToStr(deviceMac, deviceMacStr);
    slot->info.deviceMac = deviceMacStr;
  }

  char bssidStr[18];
  macToStr(bssid, bssidStr);
  slot->info.bssid = bssidStr;
  slot->info.ssid = lookupSsidForBssid(bssid);
  slot->info.lastSeenMs = millis();

  addPacketEvent(subtype == 0x00 ? PKT_ASSOC : PKT_REASSOC, slot->info.deviceMac, slot->info.ssid,
                 rssi, hopChannel);
}

// --- Alert log (deauth / disassoc / reauth-after-deauth) ---

static AlertEvent alertLog[MAX_ALERTS];
static int alertLogCount = 0; // entries filled so far, capped at MAX_ALERTS
static int alertLogNext = 0;  // ring buffer write cursor

static void addAlert(const char *kind, const char *mac1, const char *mac2) {
  alertLog[alertLogNext].kind = kind;
  alertLog[alertLogNext].mac1 = mac1;
  alertLog[alertLogNext].mac2 = mac2;
  alertLog[alertLogNext].timestampMs = millis();
  alertLogNext = (alertLogNext + 1) % MAX_ALERTS;
  if (alertLogCount < MAX_ALERTS) alertLogCount++;
  totalAlertCount++;
}

// Recently deauthed/disassoc'd MACs, so a following Authentication frame
// from the same device can be flagged as a reconnect attempt.
#define MAX_RECENT_TARGETS 6
#define REAUTH_WINDOW_MS 3000

struct RecentTarget {
  bool used;
  bool notified;
  uint8_t mac[6];
  unsigned long timestampMs;
};

static RecentTarget recentTargets[MAX_RECENT_TARGETS];
static int recentTargetsNext = 0;

static void rememberDeauthTarget(const uint8_t mac[6]) {
  recentTargets[recentTargetsNext].used = true;
  recentTargets[recentTargetsNext].notified = false;
  memcpy(recentTargets[recentTargetsNext].mac, mac, 6);
  recentTargets[recentTargetsNext].timestampMs = millis();
  recentTargetsNext = (recentTargetsNext + 1) % MAX_RECENT_TARGETS;
}

// Returns true if this mac matched a recent deauth/disassoc target and a
// REAUTH alert/packet event was logged for it - lets the caller skip also
// logging a generic PKT_AUTH for the same frame.
static bool checkForReauth(const uint8_t mac[6], int32_t rssi) {
  unsigned long now = millis();
  for (int i = 0; i < MAX_RECENT_TARGETS; i++) {
    if (!recentTargets[i].used || recentTargets[i].notified) continue;
    if (now - recentTargets[i].timestampMs > REAUTH_WINDOW_MS) continue;
    if (memcmp(recentTargets[i].mac, mac, 6) != 0) continue;

    char macStr[18];
    macToStr(mac, macStr);
    addAlert("REAUTH", macStr, "");
    addPacketEvent(PKT_REAUTH, macStr, "", rssi, hopChannel);
    recentTargets[i].notified = true;
    return true;
  }
  return false;
}

static void handleDeauthOrDisassoc(const uint8_t *frame, uint8_t subtype, int32_t rssi) {
  char mac1[18], mac2[18];
  macToStr(&frame[10], mac1); // Address 2: transmitter
  macToStr(&frame[4], mac2);  // Address 1: receiver

  addAlert(subtype == 0x0C ? "DEAUTH" : "DISASSOC", mac1, mac2);
  addPacketEvent(subtype == 0x0C ? PKT_DEAUTH : PKT_DISASSOC, mac1, mac2, rssi, hopChannel);
  rememberDeauthTarget(&frame[10]);
  rememberDeauthTarget(&frame[4]);
}

static void promiscuousCallback(uint8_t *buffer, uint16_t bufferLen) {
  if (bufferLen < 28) return; // shorter than FC+dur+3 addrs+seq - not what we want

  struct SnifferPacket *pkt = (struct SnifferPacket *)buffer;
  uint8_t *frame = pkt->data;
  uint16_t frameLen = pkt->len; // actual captured 802.11 frame length

  uint8_t frameControl = frame[0];
  uint8_t type = (frameControl >> 2) & 0x03;
  uint8_t subtype = (frameControl >> 4) & 0x0F;

  if (type != 0) return; // only management frames

  int32_t rssi = pkt->rx_ctrl.rssi;

  switch (subtype) {
    case 0x08: // Beacon
      handleBeacon(frame, frameLen, rssi);
      break;

    case 0x04: // Probe request
      handleProbeRequest(frame, frameLen, rssi);
      break;

    case 0x00: // Association request
    case 0x02: // Reassociation request
      handleAssociationRequest(frame, subtype, rssi);
      break;

    case 0x0B: { // Authentication
      // checkForReauth() already logs a more specific REAUTH event when this
      // matches a recent deauth target - only log the generic one otherwise,
      // so a single auth frame doesn't show up twice in the packet feed.
      bool reauth = checkForReauth(&frame[10], rssi); // Address 2: transmitter
      reauth |= checkForReauth(&frame[4], rssi);       // Address 1: receiver
      if (!reauth) {
        char mac1[18], mac2[18];
        macToStr(&frame[10], mac1);
        macToStr(&frame[4], mac2);
        addPacketEvent(PKT_AUTH, mac1, mac2, rssi, hopChannel);
      }
      break;
    }

    case 0x0C: // Deauth
    case 0x0A: // Disassoc
      handleDeauthOrDisassoc(frame, subtype, rssi);
      break;

    default:
      return;
  }
}

static void startFrameSniffer() {
  if (sniffing) return;

  wifi_set_opmode(STATION_MODE);
  wifi_promiscuous_enable(0);
  wifi_set_promiscuous_rx_cb(promiscuousCallback);
  wifi_set_channel(hopChannel);
  wifi_promiscuous_enable(1);

  channelHopTicker.attach_ms(300, hopToNextChannel);
  sniffing = true;
}

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

void networkInit() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  startFrameSniffer();
}

bool isFrameSnifferActive() {
  return sniffing;
}

int getNetworks(NetworkInfo out[], int maxCount) {
  int count = 0;
  for (int i = 0; i < MAX_NETWORKS && count < maxCount; i++) {
    if (networkTable[i].used) out[count++] = networkTable[i].info;
  }
  return count;
}

int getAssociations(DeviceAssociation out[], int maxCount) {
  int count = 0;
  for (int i = 0; i < MAX_ASSOCIATIONS && count < maxCount; i++) {
    if (associationTable[i].used) out[count++] = associationTable[i].info;
  }
  return count;
}

int getRecentAlerts(AlertEvent out[], int maxCount) {
  int available = alertLogCount < MAX_ALERTS ? alertLogCount : MAX_ALERTS;
  int n = available < maxCount ? available : maxCount;
  for (int i = 0; i < n; i++) {
    int idx = (alertLogNext - 1 - i + MAX_ALERTS) % MAX_ALERTS;
    out[i] = alertLog[idx];
  }
  return n;
}

int getRecentPackets(PacketEvent out[], int maxCount) {
  int available = packetLogCount < MAX_PACKET_LOG ? packetLogCount : MAX_PACKET_LOG;
  int n = available < maxCount ? available : maxCount;
  for (int i = 0; i < n; i++) {
    int idx = (packetLogNext - 1 - i + MAX_PACKET_LOG) % MAX_PACKET_LOG;
    out[i] = packetLog[idx];
  }
  return n;
}

unsigned long getTotalAlertCount() {
  return totalAlertCount;
}

void broadcastFakeNetwork(const char *ssid, uint8_t channel, unsigned long durationMs) {
  uint8_t fakeMac[6] = {0x02, 0x12, 0x34, 0x56, 0x78, 0x9A}; // locally-administered MAC

  bool wasHopping = sniffing;
  if (wasHopping) channelHopTicker.detach(); // hold the channel steady while we transmit

  unsigned long start = millis();
  while (millis() - start < durationMs) {
    sendFakeBeacon(ssid, channel, fakeMac);
    delay(100); // real APs beacon roughly every 100ms too
  }

  if (wasHopping) channelHopTicker.attach_ms(300, hopToNextChannel);
}
