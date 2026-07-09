# ESP8266 WiFi Scanner

A pocketable, battery-powered WiFi scanner/analyzer built on a NodeMCU v3
(ESP8266). It passively sniffs 802.11 management frames (no packet
injection at devices) across channels 1-13 and shows nearby networks,
their signal strength, security level, and device join/deauth activity -
either on its own screen or streamed to a companion desktop GUI.

## Hardware

- NodeMCU v3 (ESP8266), soldered and portable
- 2.8" SPI TFT (ST7789 panel, commonly sold mislabeled as "ILI9341"),
  240x320 native, run in 320x240 landscape. The module has an XPT2046
  touch footprint but this particular unit's chip is unpopulated (see
  [docs/wiring.md](docs/wiring.md)), so the UI has no touch input.
- 3.7V Li-ion cell + BMS + INA219 current/voltage monitor + HT7333 3.3V LDO

Full wiring reference, pin tables, and the reasoning behind each choice
(driver quirks, boot-sensitive pins, battery regulation, etc.) live in
[docs/](docs/):

- [docs/wiring.md](docs/wiring.md) - display, touch, and battery wiring
- [docs/pinout.md](docs/pinout.md) - NodeMCU v3 pin reference
- [docs/instructions.md](docs/instructions.md) - original project brief

## Firmware

PlatformIO project (`platformio.ini`, board `nodemcuv2`). Source is in
`src/`:

- `network.cpp` - promiscuous-mode frame sniffer: builds up the network,
  device-association, and deauth/disassoc/reauth alert tables; also
  derives each network's security level (Open/WEP/WPA/WPA2/WPA3) from its
  beacon's capability bits and RSN/WPA tagged parameters, and logs a
  filtered feed of interesting frames (new networks, probes, auth/assoc,
  deauth/disassoc/reauth) for the Packets screen
- `tft.cpp` / `oled.cpp` - display backends, selected at compile time via
  `USE_TFT_DISPLAY` in `include/display_config.h`
- `protocol.cpp` - emits the sniffer state as a compact serial protocol
  for the companion Python GUI (`gui.py`)
- `battery.cpp` - INA219-based voltage/current/state-of-charge reporting

### TFT UI

Dark-themed UI with a status bar (network count, uptime, battery), a
content area, and a bottom nav bar showing which screen is active. With
no touch input, screens rotate automatically every 10 seconds:

- **Graph** - channel vs. RSSI scatter plot, each dot colored by security
  level and labeled with the first few letters of its SSID
- **Networks** - full, paged list of every network seen, with channel,
  RSSI, and security level. If there are more networks than fit on one
  page, the page advances each time this screen comes back around, so
  everything is visible over time.
- **Packets** - scrolling, color-coded feed of recent activity: new
  networks appearing, probe requests, auth/assoc/reassoc, and
  deauth/disassoc/reauth. Like the other screens it only redraws when its
  turn comes back around (not continuously), so it's closer to "what's
  happened recently" than a true real-time ticker.

More screens (device associations, per-device history) are planned -
see the comment above `SCREEN_COUNT` in `src/tft.cpp` for how to add one.

### Serial commands (9600 baud)

- `beacon` - broadcast a fake AP for 5s (advertises only, never targets a
  real device)

## Companion GUI

`gui.py` is a desktop dashboard (customtkinter) that reads the same
serial protocol and renders a live view of networks, joins, and alerts.
See `requirements.txt`.

## Status

Hardware build is complete and portable (battery + BMS + INA219, see
[docs/wiring.md](docs/wiring.md)). Firmware has a working passive
sniffer and a fresh TFT UI with three auto-rotating screens (graph,
network list, packet feed); more screens are planned next. The panel's
touch controller turned out to be unpopulated, so navigation is
timer-driven rather than touch-driven.
