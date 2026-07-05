# Wiring

Reference for connecting peripherals to the NodeMCU v3. See
[pinout.md](./pinout.md) for the full pin reference this is based on.

Exactly one display is active at a time, selected at compile time via
`USE_TFT_DISPLAY` in `wifi_analyzer/include/display_config.h` (`0` = OLED,
`1` = TFT). Since they use different GPIOs, both can stay physically wired
at once — flip the flag and reflash to switch which one renders.

## OLED - SSD1306, 128x64, I2C

| OLED pin | NodeMCU pin |
|---|---|
| VCC | 3V3 |
| GND | G |
| SCL | D1 (GPIO5) |
| SDA | D2 (GPIO4) |

- I2C address: `0x3C` (confirmed with the `i2c_scanner` PlatformIO
  environment - see below if you need to re-check on a different module).
- `Wire.begin()` with no arguments defaults to SDA=D2/SCL=D1 on this board,
  so no explicit pin arguments are needed in code.
- These modules are commonly two-tone (yellow top strip, blue below, with a
  visible seam around row 16) - that's a physical property of the panel,
  not a wiring or software issue.

## TFT - ST7789 (sold as "ILI9341"), 240x320, SPI (e.g. MSP2807 2.8")

The product page for this exact SKU claims ILI9341, but it's actually a
mislabeled ST7789 clone - a documented mix-up for cheap unmarked SPI TFTs
at this resolution. Confirmed on this project's hardware: the
`Adafruit_ILI9341` driver only ever addressed part of the panel (garbage
pixels in the untouched remainder, and the boundary flipped sides with
rotation), while `Adafruit_ST7735 and ST7789 Library`'s `Adafruit_ST7789`
class covers the whole screen correctly with the exact same wiring. Don't
trust the seller's chip label - if a display behaves like this, trying the
other driver is worth it before assuming a wiring fault.

This clone's `init()` also defaults to inverted colors - call
`invertDisplay(false)` right after `init(240, 320)` to fix that.

Product pages often describe these as "320x240" since that's how they're
usually photographed, but the native power-on orientation (`setRotation(0)`)
is portrait: 240 wide x 320 tall. The dashboard firmware uses
`setRotation(1)` to run it in landscape (320x240) instead, which is what
gives the networks graph room to spread all 13 channels horizontally.

| TFT pin | NodeMCU pin | Note |
|---|---|---|
| VCC | 3V3 | |
| GND | G | |
| SCK | D5 (GPIO14) | hardware SPI clock |
| SDI / MOSI | D7 (GPIO13) | hardware SPI data out |
| SDO / MISO | not connected | display is write-only in this project |
| CS | D8 (GPIO15) | manually toggled by the library, not auto-claimed by `SPI.begin()` |
| DC / RS | D3 (GPIO0) | **crucial** - see note below, do not use D6 |
| RESET | NodeMCU RST pin | shared with the board's own reset - no dedicated GPIO needed |
| LED (backlight) | 3V3 | always on |

The one thing that actually caused the "all-white, never-actually-
initialized panel" symptom (backlight on, nothing else): **DC must not be
on D6 (GPIO12).** `SPI.begin()` unconditionally claims GPIO12 as the
hardware SPI peripheral's MISO line, whether or not anything is physically
wired to it. The display library reclaims the pin as a plain GPIO output
afterward, but the SPI hardware can re-assert its claim during transfers,
making the DC line unreliable - which corrupts the command/data framing
for every single byte sent during init. D3 isn't touched by `SPI.begin()`
at all, so it's safe for DC. (Tying RESET to a dedicated GPIO was tried as
a fix at one point during bring-up and turned out to be unnecessary once
DC was moved - the board's own RST pin works fine.)

### If your module also has an SD card slot and/or touch controller

Common on these 2.4" boards - the SD card and resistive touch controller
share the same SPI bus (SCK/MOSI/MISO) as the LCD, each gated by its own
chip-select pin. Neither is used by this project, but if their CS pins are
left floating they can drift low and cause the SD/touch chip to interfere
with LCD communication - "backlight on but nothing renders" is the classic
symptom. Tie both directly to 3V3 (hardware-only, no GPIO or code needed):

| Pin | Wire to |
|---|---|
| `SD_CS` | 3V3 (permanently deselected - SD card unused) |
| `T_CS` (touch CS) | 3V3 (permanently deselected - touch unused) |
| `T_IRQ` (touch interrupt) | leave unconnected - it's an output from the touch chip, can't interfere with the bus |

`T_CLK`/`T_DIN`/`T_DO`, if separately labeled on the board, are internally
the same nets as the LCD's `SCK`/`MOSI`/`MISO` - no extra wiring needed.

## Onboard (no external wiring)

| Function | NodeMCU pin | Note |
|---|---|---|
| Heartbeat LED | D4 (GPIO2) | onboard LED, active-LOW, blinks once per sniffer/display refresh cycle |

## Finding an unknown OLED's I2C address

Flash the `i2c_scanner` PlatformIO environment (`src/i2c_scanner.cpp`),
wire SDA/SCL/VCC/GND as above, and check the serial monitor (115200 baud):

```
pio run -e i2c_scanner -t upload
pio device monitor -e i2c_scanner
```

It prints every responding address every 5 seconds, e.g. `Found I2C device
at 0x3C`. Switch back to the `nodemcuv2` environment afterward for the real
firmware.
