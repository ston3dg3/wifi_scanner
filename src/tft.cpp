#include "display_config.h"

#if USE_TFT_DISPLAY // this backend is inactive - compiles to nothing

#include "screen.h"
#include <TFT_eSPI.h>

// This module is sold/labeled as "ILI9341" but is actually a mislabeled
// ST7789 clone - confirmed by bring-up testing (the ILI9341 driver only
// ever addressed part of the panel, ST7789 covers it correctly). See
// docs/wiring.md.
//
// Driver choice, pins, and SPI frequency are all set via build_flags in
// platformio.ini (TFT_eSPI is configured at compile time, not via
// constructor args) - see the comment block there for the pin mapping.
// This panel's touch controller turned out to be unpopulated (confirmed
// by continuity/IRQ probing - see docs/wiring.md), so there's no touch
// input; screens rotate automatically instead.

// --- Dark, modern palette ---------------------------------------------
static uint16_t COLOR_BG, COLOR_SURFACE, COLOR_BORDER, COLOR_TEXT, COLOR_TEXT_DIM,
                 COLOR_ACCENT, COLOR_GREEN, COLOR_TEAL, COLOR_AMBER, COLOR_RED;

static TFT_eSPI display = TFT_eSPI();
static bool tftReady = false;

// --- Layout, computed once in layoutInit() from the panel's actual size ---
static int screenW, screenH;
static int contentY0, contentY1, contentH;

#define STATUS_H 22
#define NAV_H 34

enum ScreenId { SCREEN_GRAPH = 0, SCREEN_LIST = 1, SCREEN_PACKETS = 2, SCREEN_COUNT };
// To add a screen: bump SCREEN_COUNT above, add its label to TAB_LABELS in
// drawNavBar(), add a render function, and add a case to renderContent().
static uint8_t activeScreen = SCREEN_GRAPH;

// Screens rotate automatically - no touch/buttons on this hardware.
#define ROTATE_INTERVAL_MS 10000UL
static unsigned long lastRotateMs = 0;

// Graph screen plot geometry
#define RSSI_MAX -30 // top of the graph (strong signal)
#define RSSI_MIN -100 // bottom of the graph (weak signal)
static int plotLeft, plotRight, plotTop, plotBottom;
static int chanX[14]; // pixel x for channel 1-13 (index 0 unused)
static int rssiRowY[8], rssiRowVal[8], rssiRowCount;

// List screen geometry
#define LIST_ROW_H 18
static int listRowsTop, listPagerY, listPagerH = 18;
static int rowsPerPage;
static int colChX, colRssiX, colSecX;
static int gListPage = 0;
static int gListTotalPages = 1;

// Packets screen geometry
#define PACKET_ROW_H 18

// --- Cached snapshot of the last data screenRender() was given ---
static NetworkInfo gNetworks[MAX_NETWORKS];
static int gNetworkCount = 0;
static PacketEvent gPackets[MAX_PACKET_LOG];
static int gPacketCount = 0;
static unsigned long gUptimeMs = 0;
static BatteryStatus gBattery = {0, 0, 0, false};
static bool gHaveData = false;

static void renderContent();
static void drawStatusBar();
static void drawNavBar();

static String truncated(const String &s, uint8_t maxLen) {
  if (s.length() <= maxLen) return s;
  return s.substring(0, maxLen - 1) + ".";
}

// Last 3 octets, e.g. "12:34:56" - enough to distinguish devices in the
// packet feed's tight row width without eating all the horizontal space.
static String shortMac(const String &mac) {
  return mac.length() >= 17 ? mac.substring(9) : mac;
}

static uint16_t packetColor(PacketKind k) {
  switch (k) {
    case PKT_NEW_NETWORK: return COLOR_ACCENT;
    case PKT_PROBE:
    case PKT_AUTH: return COLOR_TEXT_DIM;
    case PKT_ASSOC:
    case PKT_REASSOC: return COLOR_GREEN;
    case PKT_DEAUTH:
    case PKT_DISASSOC: return COLOR_RED;
    case PKT_REAUTH: return COLOR_AMBER;
  }
  return COLOR_TEXT_DIM;
}

static uint16_t securityColor(SecurityType s) {
  switch (s) {
    case SEC_OPEN: return COLOR_RED;
    case SEC_WEP:
    case SEC_WPA: return COLOR_AMBER;
    case SEC_WPA2:
    case SEC_SECURED: return COLOR_GREEN;
    case SEC_WPA3:
    case SEC_WPA2_WPA3: return COLOR_TEAL;
  }
  return COLOR_TEXT_DIM;
}

static void initPalette() {
  COLOR_BG       = display.color565(13, 17, 23);
  COLOR_SURFACE  = display.color565(22, 27, 34);
  COLOR_BORDER   = display.color565(48, 56, 68);
  COLOR_TEXT     = display.color565(230, 237, 243);
  COLOR_TEXT_DIM = display.color565(139, 148, 158);
  COLOR_ACCENT   = display.color565(56, 189, 189);
  COLOR_GREEN    = display.color565(63, 185, 80);
  COLOR_TEAL     = display.color565(45, 212, 191);
  COLOR_AMBER    = display.color565(230, 160, 50);
  COLOR_RED      = display.color565(230, 80, 80);
}

static void layoutInit() {
  screenW = display.width();
  screenH = display.height();

  contentY0 = STATUS_H;
  contentY1 = screenH - NAV_H;
  contentH = contentY1 - contentY0;

  // Graph plot: left margin for RSSI labels, bottom margin for channel labels.
  plotLeft = 26;
  plotRight = screenW - 6;
  plotTop = contentY0 + 6;
  plotBottom = contentY1 - 14;
  for (int ch = 1; ch <= 13; ch++) {
    float frac = (float)(ch - 1) / 12;
    chanX[ch] = plotLeft + (int)(frac * (plotRight - plotLeft));
  }
  rssiRowCount = 0;
  for (int rssi = RSSI_MAX; rssi >= RSSI_MIN; rssi -= 20) {
    float frac = (float)(RSSI_MAX - rssi) / (RSSI_MAX - RSSI_MIN);
    rssiRowY[rssiRowCount] = plotTop + (int)(frac * (plotBottom - plotTop));
    rssiRowVal[rssiRowCount] = rssi;
    rssiRowCount++;
  }

  // Network list columns, inset from the right edge; SSID takes what's left.
  colSecX = screenW - 60;
  colRssiX = screenW - 98;
  colChX = screenW - 126;
  listRowsTop = contentY0 + 16;
  listPagerY = contentY1 - listPagerH;
  rowsPerPage = (contentH - 16 - listPagerH) / LIST_ROW_H;
  if (rowsPerPage < 1) rowsPerPage = 1;
}

// --- Status bar (top) -----------------------------------------------------
static void drawStatusBar() {
  display.fillRect(0, 0, screenW, STATUS_H, COLOR_SURFACE);
  display.drawFastHLine(0, STATUS_H - 1, screenW, COLOR_BORDER);

  display.setTextSize(1);
  display.setTextColor(COLOR_TEXT_DIM);
  display.setCursor(6, 7);
  display.printf("%d network%s", gNetworkCount, gNetworkCount == 1 ? "" : "s");

  unsigned long sec = gUptimeMs / 1000;
  char timeBuf[9];
  snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu", sec / 3600, (sec / 60) % 60, sec % 60);
  display.setTextDatum(TC_DATUM);
  display.setTextColor(COLOR_TEXT_DIM);
  display.drawString(timeBuf, screenW / 2, 7);
  display.setTextDatum(TL_DATUM);

  if (gBattery.sensorReady) {
    char battBuf[8];
    snprintf(battBuf, sizeof(battBuf), "%d%%", gBattery.percent);
    uint16_t col = gBattery.percent <= 15 ? COLOR_RED : (gBattery.percent <= 40 ? COLOR_AMBER : COLOR_GREEN);
    display.setTextDatum(TR_DATUM);
    display.setTextColor(col);
    display.drawString(battBuf, screenW - 6, 7);
    display.setTextDatum(TL_DATUM);
  }
}

// --- Nav bar (bottom): shows which screen is active, purely informational
// now that there's no touch input to drive it. ---
static void drawNavBar() {
  static const char *TAB_LABELS[SCREEN_COUNT] = {"Graph", "Networks", "Packets"};
  int tabW = screenW / SCREEN_COUNT;

  display.fillRect(0, contentY1, screenW, NAV_H, COLOR_SURFACE);
  display.drawFastHLine(0, contentY1, screenW, COLOR_BORDER);

  display.setTextSize(1);
  display.setTextDatum(MC_DATUM);
  for (int i = 0; i < SCREEN_COUNT; i++) {
    int x = i * tabW;
    bool active = (i == activeScreen);

    if (active) {
      display.fillRect(x, contentY1, tabW, 3, COLOR_ACCENT); // active indicator along the tab's top edge
    }
    if (i > 0) {
      display.drawFastVLine(x, contentY1 + 8, NAV_H - 16, COLOR_BORDER);
    }

    display.setTextColor(active ? COLOR_ACCENT : COLOR_TEXT_DIM);
    display.drawString(TAB_LABELS[i], x + tabW / 2, contentY1 + NAV_H / 2 + 2);
  }
  display.setTextDatum(TL_DATUM);
}

// --- Graph screen: channel (x) vs RSSI (y) scatter -------------------
static void renderGraphScreen() {
  display.setTextSize(1);
  display.setTextColor(COLOR_TEXT_DIM);
  for (int r = 0; r < rssiRowCount; r++) {
    display.drawFastHLine(plotLeft, rssiRowY[r], plotRight - plotLeft, COLOR_BORDER);
    display.setCursor(2, rssiRowY[r] - 3);
    display.print(rssiRowVal[r]);
  }
  for (int ch = 1; ch <= 13; ch++) {
    display.drawFastVLine(chanX[ch], plotTop, plotBottom - plotTop, COLOR_BORDER);
    int lx = chanX[ch] - (ch < 10 ? 3 : 6);
    if (lx < plotLeft) lx = plotLeft;
    display.setCursor(lx, plotBottom + 3);
    display.print(ch);
  }

  for (int i = 0; i < gNetworkCount; i++) {
    const NetworkInfo &n = gNetworks[i];

    int32_t rssiClamped = n.rssi;
    if (rssiClamped > RSSI_MAX) rssiClamped = RSSI_MAX;
    if (rssiClamped < RSSI_MIN) rssiClamped = RSSI_MIN;
    float frac = (float)(RSSI_MAX - rssiClamped) / (RSSI_MAX - RSSI_MIN);
    int x = chanX[n.channel];
    int y = plotTop + (int)(frac * (plotBottom - plotTop));
    y = constrain(y, plotTop + 2, plotBottom - 2);

    uint16_t col = securityColor(n.security);
    display.fillCircle(x, y, 2, col);

    String label = n.ssid.length() > 4 ? n.ssid.substring(0, 4) : n.ssid;
    if (label.length() == 0) label = "?";
    int labelW = label.length() * 6;

    // Prefer the dot's right side; flip to the left if that would run past
    // the plot's right edge, then clamp both sides so it can never spill
    // outside the content area no matter how crowded the channel is.
    int lx = x + 5;
    if (lx + labelW > plotRight) lx = x - 5 - labelW;
    if (lx < 2) lx = 2;
    if (lx + labelW > screenW - 2) lx = screenW - 2 - labelW;

    int ly = y - 7;
    if (ly < contentY0 + 1) ly = contentY0 + 1;
    if (ly + 7 > contentY1 - 1) ly = contentY1 - 8;

    display.setTextColor(COLOR_TEXT_DIM);
    display.setCursor(lx, ly);
    display.print(label);
  }
}

// --- Networks screen: sortable, paged list with security level --------
static void sortNetworksByRssiDesc(int order[], int count) {
  for (int i = 0; i < count; i++) order[i] = i;
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (gNetworks[order[j]].rssi > gNetworks[order[i]].rssi) {
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
  }
}

static void renderListScreen() {
  int headerY = contentY0 + 2;
  display.setTextSize(1);
  display.setTextColor(COLOR_TEXT_DIM);
  display.setCursor(6, headerY);
  display.print("SSID");
  display.setCursor(colChX, headerY);
  display.print("CH");
  display.setCursor(colRssiX, headerY);
  display.print("RSSI");
  display.setCursor(colSecX, headerY);
  display.print("SECURITY");
  display.drawFastHLine(4, headerY + 12, screenW - 8, COLOR_BORDER);

  if (gNetworkCount == 0) {
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_TEXT_DIM);
    display.drawString(gHaveData ? "No networks found yet" : "Waiting for data...",
                        screenW / 2, contentY0 + contentH / 2);
    display.setTextDatum(TL_DATUM);
    gListTotalPages = 1;
    return;
  }

  int order[MAX_NETWORKS];
  sortNetworksByRssiDesc(order, gNetworkCount);

  gListTotalPages = (gNetworkCount + rowsPerPage - 1) / rowsPerPage;
  if (gListPage >= gListTotalPages) gListPage = gListTotalPages - 1;
  if (gListPage < 0) gListPage = 0;

  int startIdx = gListPage * rowsPerPage;
  int endIdx = startIdx + rowsPerPage;
  if (endIdx > gNetworkCount) endIdx = gNetworkCount;

  int rowY = listRowsTop;
  for (int i = startIdx; i < endIdx; i++) {
    const NetworkInfo &n = gNetworks[order[i]];
    if ((i - startIdx) % 2 == 1) {
      display.fillRect(4, rowY - 2, screenW - 8, LIST_ROW_H, COLOR_SURFACE);
    }

    display.setTextColor(COLOR_TEXT);
    display.setCursor(6, rowY);
    display.print(truncated(n.ssid.length() == 0 ? String("<hidden>") : n.ssid, 18));

    display.setCursor(colChX, rowY);
    display.printf("%2u", n.channel);

    display.setCursor(colRssiX, rowY);
    display.printf("%4ld", (long)n.rssi);

    display.setTextColor(securityColor(n.security));
    display.setCursor(colSecX, rowY);
    display.print(securityLabel(n.security));

    rowY += LIST_ROW_H;
  }

  // Pages advance automatically (see updateActiveScreen()) - no on-screen
  // controls needed, just a plain indicator of where we are.
  char pageBuf[16];
  snprintf(pageBuf, sizeof(pageBuf), "Page %d / %d", gListPage + 1, gListTotalPages);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(COLOR_TEXT_DIM);
  display.drawString(pageBuf, screenW / 2, listPagerY + listPagerH / 2);
  display.setTextDatum(TL_DATUM);
}

// --- Packets screen: filtered feed of interesting frames, newest first ---
static void renderPacketsScreen() {
  if (gPacketCount == 0) {
    display.setTextDatum(MC_DATUM);
    display.setTextColor(COLOR_TEXT_DIM);
    display.drawString(gHaveData ? "No activity yet" : "Waiting for data...",
                        screenW / 2, contentY0 + contentH / 2);
    display.setTextDatum(TL_DATUM);
    return;
  }

  int maxRows = contentH / PACKET_ROW_H;
  int shown = gPacketCount < maxRows ? gPacketCount : maxRows;

  display.setTextSize(1);
  int rowY = contentY0 + 4;
  for (int i = 0; i < shown; i++) {
    const PacketEvent &p = gPackets[i]; // already newest-first from getRecentPackets()

    unsigned long ageSec = (gUptimeMs >= p.timestampMs) ? (gUptimeMs - p.timestampMs) / 1000 : 0;
    char ageBuf[6];
    if (ageSec < 60) snprintf(ageBuf, sizeof(ageBuf), "%2lus", ageSec);
    else snprintf(ageBuf, sizeof(ageBuf), "%2lum", ageSec / 60);

    display.setTextColor(COLOR_TEXT_DIM);
    display.setCursor(4, rowY);
    display.print(ageBuf);

    display.setTextColor(packetColor(p.kind));
    display.setCursor(34, rowY);
    display.print(packetKindLabel(p.kind));

    String line = shortMac(p.mac);
    if (p.detail.length() > 0) {
      line += "  ";
      line += p.detail;
    }
    display.setTextColor(COLOR_TEXT);
    display.setCursor(102, rowY);
    display.print(truncated(line, 34));

    rowY += PACKET_ROW_H;
  }
}

static void renderContent() {
  display.fillRect(0, contentY0, screenW, contentH, COLOR_BG);
  switch (activeScreen) {
    case SCREEN_GRAPH: renderGraphScreen(); break;
    case SCREEN_LIST: renderListScreen(); break;
    case SCREEN_PACKETS: renderPacketsScreen(); break;
  }
}

// Advances to the next screen every ROTATE_INTERVAL_MS. Stepping onto the
// list screen also advances its page, so a network list too long for one
// page is still fully visible over time instead of only ever showing page 1.
static void updateActiveScreen() {
  unsigned long now = millis();
  if (now - lastRotateMs < ROTATE_INTERVAL_MS) return;
  lastRotateMs = now;

  activeScreen = (activeScreen + 1) % SCREEN_COUNT;
  if (activeScreen == SCREEN_LIST) {
    gListPage = (gListPage + 1) % (gListTotalPages > 0 ? gListTotalPages : 1);
  }
}

// --- Public API -----------------------------------------------------------
void screenInit() {
  display.init(); // dimensions/pins/SPI frequency come from platformio.ini build_flags
  display.invertDisplay(false); // this clone's init() defaults to inverted colors
  tftReady = true; // this library has no way to report a failed init over SPI

  display.setRotation(1); // landscape, 320x240 (try 3 instead if upside-down on your panel)
  initPalette();
  layoutInit();
  lastRotateMs = millis();

  display.fillScreen(COLOR_BG);
  display.setTextDatum(MC_DATUM);
  display.setTextColor(COLOR_TEXT);
  display.setTextSize(2);
  display.drawString("WiFi Scanner", screenW / 2, screenH / 2 - 20);
  display.setTextSize(1);
  display.setTextColor(COLOR_TEXT_DIM);
  display.drawString("booting...", screenW / 2, screenH / 2 + 12);
  display.setTextDatum(TL_DATUM);

  drawStatusBar();
  drawNavBar();
  renderContent();
}

void screenRender(const NetworkInfo networks[], int networkCount,
                   const DeviceAssociation associations[], int associationCount,
                   const AlertEvent alerts[], int alertCount,
                   const PacketEvent packets[], int packetCount,
                   unsigned long totalAlertCount, unsigned long uptimeMs,
                   const BatteryStatus &battery) {
  if (!tftReady) return;

  // Associations/alerts aren't shown on any screen yet - kept in the
  // shared interface for the OLED backend and for a future alerts screen.
  (void)associations; (void)associationCount; (void)alerts; (void)alertCount; (void)totalAlertCount;

  bool firstData = !gHaveData; // so the initial "Waiting for data..." placeholder doesn't linger until the first rotation

  for (int i = 0; i < networkCount; i++) gNetworks[i] = networks[i];
  gNetworkCount = networkCount;
  for (int i = 0; i < packetCount; i++) gPackets[i] = packets[i];
  gPacketCount = packetCount;
  gUptimeMs = uptimeMs;
  gBattery = battery;
  gHaveData = true;

  uint8_t screenBefore = activeScreen;
  updateActiveScreen();

  drawStatusBar(); // small and cheap - fine to refresh every cycle

  // The content area's full black clear is slow enough over this panel's
  // conservative SPI speed to read as a flicker, and the data doesn't
  // change fast enough to need refreshing every cycle anyway - only
  // redraw it when the screen actually changes.
  if (activeScreen != screenBefore) {
    drawNavBar();
    renderContent();
  } else if (firstData) {
    renderContent();
  }
}

#endif // USE_TFT_DISPLAY
