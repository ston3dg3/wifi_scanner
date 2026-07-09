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

#define COLOR_DARKGREY 0x7BEF // matches TFT_DARKGREY, spelled out for clarity
#define COLOR_NAVY 0x000C     // dim fill for the per-channel occupancy bars

// RSSI range for the channel/signal graph, in dBm.
#define RSSI_MAX -30 // top of the graph (strong signal)
#define RSSI_MIN -100 // bottom of the graph (weak signal)

// A join toast / alert banner stays on screen this long after the event,
// so it survives roughly two SEND_INTERVAL_MS (3s) redraw cycles before
// fading back out on its own on a later redraw.
#define TOAST_WINDOW_MS 6000UL

// A channel showing this many networks or more fills its occupancy bar
// all the way - keeps one very crowded channel from dwarfing the rest.
#define CHANNEL_BAR_CAP 6

// Persistent-screen layout. The screen is never fully wiped after boot -
// the grid/labels below are drawn once and each redraw only touches the
// small region a piece of dynamic content actually lives in, so nothing
// ever flashes black before repainting (see screenRenderNextPage()).
#define STATUS_BAR_H 13
#define BANNER_H 18
#define MARGIN_LEFT 26
#define MARGIN_RIGHT 4
#define MARGIN_TOP (STATUS_BAR_H + 4)
#define MARGIN_BOTTOM 12
#define COLUMN_HALF_W 5 // each channel's dynamic content lives in a x-5..x+5 strip

static TFT_eSPI display = TFT_eSPI();
static bool tftReady = false;

// Layout, computed once in screenInit() from the panel's actual dimensions.
static int gPlotW, gPlotH, gBaseline, gGraphBottom;
static int gChanX[14]; // pixel x for channel 1-13 (index 0 unused)
static int gRssiRowY[8], gRssiRowVal[8], gRssiRowCount;
static int gToastX, gToastY, gToastW, gToastH;

static bool bannerWasVisible = false;
static bool toastWasVisible = false;

static String truncated(const String &s, uint8_t maxLen) {
  if (s.length() <= maxLen) return s;
  return s.substring(0, maxLen - 1) + ".";
}

// Last 3 octets, e.g. "12:34:56" - enough to distinguish devices in the
// small toast/banner strips without eating all the horizontal space.
static String shortMac(const String &mac) {
  return mac.length() >= 17 ? mac.substring(9) : mac;
}

static void layoutInit() {
  gGraphBottom = display.height() - BANNER_H;
  gPlotW = display.width() - MARGIN_LEFT - MARGIN_RIGHT;
  gPlotH = gGraphBottom - MARGIN_BOTTOM - MARGIN_TOP;
  gBaseline = MARGIN_TOP + gPlotH;

  for (int ch = 1; ch <= 13; ch++) {
    float frac = (float)(ch - 1) / 12;
    gChanX[ch] = MARGIN_LEFT + (int)(frac * gPlotW);
  }

  gRssiRowCount = 0;
  for (int rssi = RSSI_MAX; rssi >= RSSI_MIN; rssi -= 20) {
    float frac = (float)(RSSI_MAX - rssi) / (RSSI_MAX - RSSI_MIN);
    gRssiRowY[gRssiRowCount] = MARGIN_TOP + (int)(frac * gPlotH);
    gRssiRowVal[gRssiRowCount] = rssi;
    gRssiRowCount++;
  }

  gToastW = 96;
  gToastH = 28;
  gToastX = display.width() - gToastW;
  gToastY = MARGIN_TOP;
}

// Everything here is drawn exactly once and never touched again: the RSSI
// axis labels (left margin) and channel numbers (below the plot) live
// outside every dynamic region, and the status-bar divider sits just above
// where the status bar's own redraw clears to.
static void drawStaticChrome() {
  display.fillScreen(TFT_BLACK); // the only full-screen clear, ever
  display.drawFastHLine(0, STATUS_BAR_H - 1, display.width(), COLOR_DARKGREY);

  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  for (int r = 0; r < gRssiRowCount; r++) {
    display.setCursor(0, gRssiRowY[r] - 3);
    display.print(gRssiRowVal[r]);
  }
  for (int ch = 1; ch <= 13; ch++) {
    display.setCursor(gChanX[ch] - (ch < 10 ? 3 : 6), gBaseline + 3);
    display.print(ch);
  }
}

// Redraws one channel's vertical strip: gridline ticks (erased and put
// back every time, which is invisible since they're the same pixels),
// its occupancy bar, and its scatter dots. Called for all 13 channels
// every cycle - channels with nothing on them just go black-on-black,
// so in practice only the columns with real traffic ever visibly blink.
static void refreshChannelColumn(int ch, const NetworkInfo networks[], int count) {
  int x = gChanX[ch];
  int w = COLUMN_HALF_W * 2 + 1;

  display.fillRect(x - COLUMN_HALF_W, MARGIN_TOP, w, gPlotH, TFT_BLACK);
  display.drawFastVLine(x, MARGIN_TOP, gPlotH, COLOR_DARKGREY);
  for (int r = 0; r < gRssiRowCount; r++) {
    display.drawFastHLine(x - COLUMN_HALF_W, gRssiRowY[r], w, COLOR_DARKGREY);
  }

  int channelCount = 0;
  for (int i = 0; i < count; i++) {
    if (networks[i].channel == ch) channelCount++;
  }
  int capped = channelCount > CHANNEL_BAR_CAP ? CHANNEL_BAR_CAP : channelCount;
  int barMaxH = gPlotH - 4;
  int barH = (barMaxH * capped) / CHANNEL_BAR_CAP;
  if (barH > 0) {
    display.fillRect(x - COLUMN_HALF_W, gBaseline - barH, w, barH, COLOR_NAVY);
  }

  for (int i = 0; i < count; i++) {
    const NetworkInfo &n = networks[i];
    if (n.channel != ch) continue;

    int32_t rssiClamped = n.rssi;
    if (rssiClamped > RSSI_MAX) rssiClamped = RSSI_MAX;
    if (rssiClamped < RSSI_MIN) rssiClamped = RSSI_MIN;
    float frac = (float)(RSSI_MAX - rssiClamped) / (RSSI_MAX - RSSI_MIN);
    int y = MARGIN_TOP + (int)(frac * gPlotH);
    y = constrain(y, MARGIN_TOP + 3, gBaseline - 3); // keep the dot's radius inside this column's redrawn strip

    display.fillCircle(x, y, 3, n.secured ? TFT_CYAN : TFT_GREEN);
  }
}

static void updateStatusBar(int networkCount, int associationCount,
                             unsigned long totalAlertCount, unsigned long uptimeMs,
                             const BatteryStatus &battery) {
  display.fillRect(0, 0, display.width(), STATUS_BAR_H - 1, TFT_BLACK);
  unsigned long sec = uptimeMs / 1000;
  display.setTextSize(1);

  // Left group's width depends on how many digits each count has, so its
  // end position is computed rather than assumed - that's what lets the
  // elapsed-time field below slot in right after it with a fixed gap
  // instead of drifting into the battery field on the right.
  char leftBuf[40];
  snprintf(leftBuf, sizeof(leftBuf), "Nets:%-3d Assoc:%-3d Alerts:%-3lu",
           networkCount, associationCount, totalAlertCount);
  display.setTextColor(TFT_YELLOW);
  display.setCursor(2, 2);
  display.print(leftBuf);
  int leftEndX = 2 + (int)strlen(leftBuf) * 6; // 6px/char at text size 1

  int rightLimit = display.width() - 2;
  if (battery.sensorReady) {
    char battBuf[20];
    snprintf(battBuf, sizeof(battBuf), "Batt:%d%% %.2fV", battery.percent, battery.voltageV);
    int battWidth = (int)strlen(battBuf) * 6;
    int battX = display.width() - battWidth - 2;
    display.setTextColor(battery.percent <= 15 ? TFT_RED : TFT_YELLOW);
    display.setCursor(battX, 2);
    display.print(battBuf);
    rightLimit = battX - 4; // small gap before the battery field
  }

  char timeBuf[9];
  snprintf(timeBuf, sizeof(timeBuf), "%02lu:%02lu:%02lu",
           sec / 3600, (sec / 60) % 60, sec % 60);
  int timeWidth = (int)strlen(timeBuf) * 6;
  int timeX = rightLimit - timeWidth;
  if (timeX < leftEndX + 4) timeX = leftEndX + 4; // don't collide with the left group if things get tight

  display.setTextColor(TFT_YELLOW);
  display.setCursor(timeX, 2);
  display.print(timeBuf);
}

// Bottom banner: flashes the most recent deauth/disassoc/reauth for as
// long as it's fresh. This strip is never touched by anything else, so
// unlike the toast it just needs a plain clear on the way out.
static void updateAlertBanner(const AlertEvent alerts[], int count, unsigned long uptimeMs) {
  bool active = count > 0 && (uptimeMs - alerts[0].timestampMs) <= TOAST_WINDOW_MS;
  if (!active) {
    if (bannerWasVisible) {
      display.fillRect(0, gGraphBottom, display.width(), BANNER_H, TFT_BLACK);
    }
    bannerWasVisible = false;
    return;
  }

  uint16_t color = TFT_MAGENTA; // REAUTH
  if (alerts[0].kind == "DEAUTH") color = TFT_RED;
  else if (alerts[0].kind == "DISASSOC") color = TFT_ORANGE;

  display.fillRect(0, gGraphBottom, display.width(), BANNER_H, color);
  display.setTextSize(1);
  display.setTextColor(TFT_BLACK);
  display.setCursor(4, gGraphBottom + (BANNER_H - 8) / 2);
  display.printf("%s  %s -> %s", alerts[0].kind.c_str(),
                  shortMac(alerts[0].mac1).c_str(), shortMac(alerts[0].mac2).c_str());
  bannerWasVisible = true;
}

static int findFreshestAssociation(const DeviceAssociation associations[], int count) {
  int newest = -1;
  for (int i = 0; i < count; i++) {
    if (newest == -1 || associations[i].lastSeenMs > associations[newest].lastSeenMs) newest = i;
  }
  return newest;
}

static void drawJoinToast(const String &ssid) {
  display.fillRect(gToastX, gToastY, gToastW, gToastH, TFT_BLACK);
  display.drawRect(gToastX, gToastY, gToastW, gToastH, TFT_GREEN);
  display.setTextSize(1);
  display.setTextColor(TFT_GREEN);
  display.setCursor(gToastX + 3, gToastY + 3);
  display.print("+ JOINED");
  display.setTextColor(TFT_WHITE);
  display.setCursor(gToastX + 3, gToastY + 14);
  display.print(truncated(ssid, 15));
}

void screenInit() {
  display.init(); // dimensions/pins/SPI frequency come from platformio.ini build_flags
  display.invertDisplay(false); // this clone's init() defaults to inverted colors
  tftReady = true; // this library has no way to report a failed init over SPI

  display.setRotation(1); // landscape, 320x240 (try 3 instead if upside-down on your panel)
  layoutInit();
  drawStaticChrome();

  display.setTextSize(1);
  display.setTextColor(TFT_WHITE);
  display.setCursor(2, 2);
  display.print("booting..."); // lives in the status bar's own footprint, so the first real update below overwrites it for free
}

void screenRenderNextPage(const NetworkInfo networks[], int networkCount,
                           const DeviceAssociation associations[], int associationCount,
                           const AlertEvent alerts[], int alertCount,
                           unsigned long totalAlertCount, unsigned long uptimeMs,
                           const BatteryStatus &battery) {
  if (!tftReady) return;

  int freshest = findFreshestAssociation(associations, associationCount);
  bool showToast = freshest != -1 && (uptimeMs - associations[freshest].lastSeenMs) <= TOAST_WINDOW_MS;

  // The toast floats over the right edge of the channel graph, including
  // the gaps *between* columns that refreshChannelColumn() never touches.
  // On the frame it fades out, wipe its whole footprint first so those
  // gaps go back to black - the column loop right after repaints the
  // tile portions on top of that, leaving a clean result either way.
  if (!showToast && toastWasVisible) {
    display.fillRect(gToastX, gToastY, gToastW, gToastH, TFT_BLACK);
  }

  for (int ch = 1; ch <= 13; ch++) {
    refreshChannelColumn(ch, networks, networkCount);
  }

  updateStatusBar(networkCount, associationCount, totalAlertCount, uptimeMs, battery);
  updateAlertBanner(alerts, alertCount, uptimeMs);

  if (showToast) drawJoinToast(associations[freshest].ssid);
  toastWasVisible = showToast;
}

#endif // USE_TFT_DISPLAY
