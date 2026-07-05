#include "display_config.h"

#if !USE_TFT_DISPLAY // this backend is inactive - compiles to nothing

#include "screen.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_I2C_ADDRESS 0x3C
#define PAGE_COUNT 4

// Many cheap 128x64 SSD1306 panels are physically two-tone: a yellow strip
// covering the top rows and blue underneath, with a visible seam baked
// into the glass. The controller is 1-bit monochrome with no color control
// at all, so this can't be fixed in software - instead we lay text out so
// nothing straddles the seam. Adjust this if your panel's split differs.
#define COLOR_SEAM_Y 16

static Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
static bool oledReady = false;
static uint8_t currentPage = 0;

static String truncated(const String &s, uint8_t maxLen) {
  if (s.length() <= maxLen) return s;
  return s.substring(0, maxLen - 1) + ".";
}

static void drawHeader(const char *title) {
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print(title);
  // Body text starts exactly at the panel's color seam so no line of text
  // ever gets split across the yellow/blue boundary.
  display.setCursor(0, COLOR_SEAM_Y);
}

static void renderOverview(int networkCount, int associationCount,
                            unsigned long totalAlertCount, unsigned long uptimeMs) {
  drawHeader("Overview");
  unsigned long sec = uptimeMs / 1000;
  display.printf("Up: %02lu:%02lu:%02lu\n", sec / 3600, (sec / 60) % 60, sec % 60);
  display.printf("Networks: %d\n", networkCount);
  display.printf("Devices:  %d\n", associationCount);
  display.printf("Alerts:   %lu total\n", totalAlertCount);
}

static void renderNetworks(const NetworkInfo networks[], int count) {
  drawHeader("Top Networks");
  if (count == 0) {
    display.println("(none seen yet)");
    return;
  }

  // Selection-sort indices by RSSI descending, without touching the
  // caller's array (it's a shared snapshot buffer owned by main.cpp).
  int order[MAX_NETWORKS];
  for (int i = 0; i < count; i++) order[i] = i;
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (networks[order[j]].rssi > networks[order[i]].rssi) {
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
      }
    }
  }

  int shown = count < 4 ? count : 4;
  for (int i = 0; i < shown; i++) {
    const NetworkInfo &n = networks[order[i]];
    display.printf("%2d %-13s%4ld\n", n.channel, truncated(n.ssid, 13).c_str(), (long)n.rssi);
  }
}

static void renderAlerts(const AlertEvent alerts[], int count) {
  drawHeader("Recent Alerts");
  if (count == 0) {
    display.println("(none)");
    return;
  }
  int shown = count < 4 ? count : 4;
  for (int i = 0; i < shown; i++) {
    display.printf("%-8s %s\n", alerts[i].kind.c_str(), truncated(alerts[i].mac1, 17).c_str());
  }
}

static void renderDevices(const DeviceAssociation associations[], int count) {
  drawHeader("Joined Devices");
  if (count == 0) {
    display.println("(none seen yet)");
    return;
  }
  int shown = count < 4 ? count : 4;
  for (int i = 0; i < shown; i++) {
    display.println(truncated(associations[i].ssid, 21));
  }
}

void screenInit() {
  Wire.begin(); // defaults to SDA=D2, SCL=D1 on NodeMCU
  oledReady = display.begin(SSD1306_SWITCHCAPVCC, OLED_I2C_ADDRESS);
  if (!oledReady) return;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("WiFi Analyzer");
  display.println("booting...");
  display.display();
}

void screenRenderNextPage(const NetworkInfo networks[], int networkCount,
                           const DeviceAssociation associations[], int associationCount,
                           const AlertEvent alerts[], int alertCount,
                           unsigned long totalAlertCount, unsigned long uptimeMs) {
  if (!oledReady) return;

  display.clearDisplay();
  switch (currentPage) {
    case 0: renderOverview(networkCount, associationCount, totalAlertCount, uptimeMs); break;
    case 1: renderNetworks(networks, networkCount); break;
    case 2: renderAlerts(alerts, alertCount); break;
    case 3: renderDevices(associations, associationCount); break;
  }
  display.display();

  currentPage = (currentPage + 1) % PAGE_COUNT;
}

#endif // !USE_TFT_DISPLAY
