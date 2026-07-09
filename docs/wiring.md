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

> **This project's specific panel has no touch controller populated.**
> Every SPI-side connection (`T_CLK`/`T_DIN`/`T_DO`/`T_CS`) checked out
> with a continuity meter, `T_CS` was confirmed toggling correctly (and
> tested forced permanently low), yet `T_IRQ` sits flat at 0V regardless
> of touch - the XPT2046 chip itself simply isn't on the board. The
> firmware doesn't attempt touch input; screens rotate on a timer
> instead. The wiring below is left for reference in case a touch-capable
> panel gets swapped in later.

Common on these 2.4" boards - the SD card and resistive touch controller
(typically an XPT2046) share the same SPI bus (SCK/MOSI/MISO) as the LCD,
each gated by its own chip-select pin. If their CS pins are left floating
they can drift low and cause the SD/touch chip to interfere with LCD
communication - "backlight on but nothing renders" is the classic
symptom.

**If you're not using either** (the SD card slot is unused by this
project either way), tie both CS pins directly to 3V3 (hardware-only, no
GPIO or code needed):

| Pin | Wire to |
|---|---|
| `SD_CS` | 3V3 (permanently deselected - SD card unused) |
| `T_CS` (touch CS) | 3V3 (permanently deselected - touch unused) |
| `T_IRQ` (touch interrupt) | leave unconnected - it's an output from the touch chip, can't interfere with the bus |

**If you want to actually read the touch controller**, `T_CS` needs to
move off 3V3 onto a real GPIO so firmware can select/deselect it. Every
other GPIO (`D1`-`D8`) is already claimed by I2C/SPI/the heartbeat LED, so
`D0` (GPIO16) - the one pin left unused on this board - is what's left:

| Touch pin | NodeMCU pin | Note |
|---|---|---|
| `T_CLK` | D5 (GPIO14) | same net as the LCD's `SCK` - no new wire, it's already run there |
| `T_DIN` | D7 (GPIO13) | same net as the LCD's `MOSI` - no new wire |
| `T_DO` | D6 (GPIO12) | same net as the LCD's `MISO` - this is the first real use of that line, since the display itself is write-only |
| `T_CS` | **D0 (GPIO16)** | the last free pin on the board; fine for CS since chip-select only needs plain digitalWrite, not interrupts/PWM |
| `T_IRQ` | leave unconnected | there's no free interrupt-capable pin left anyway (`D0`/GPIO16 doesn't support interrupts) - poll the touch controller from the main loop instead of reacting to `T_IRQ` |

`SD_CS` still ties to 3V3 in this scenario, since the SD slot goes unused
either way and D0 is the only spare pin, already spoken for by `T_CS`.

## Battery power - Li-ion + BMS + INA219 + HT7333

Powers the whole board from a 1S Li-ion cell instead of USB. The NodeMCU's
onboard 5V->3.3V regulator (AMS1117) is bypassed entirely and never wired
to the battery side - it needs ~4.4V+ in to hold a clean 3.3V out, and a
Li-ion cell (3.0-4.2V) never reaches that. Feeding the raw cell into the
board's **VIN** pin won't regulate; wiring it straight to the **3V3** pin
would expose the ESP8266 (absolute max VDD 3.6V) to a freshly-charged
4.2V and risk damage. A dedicated 3.3V LDO in the middle is required.

Parts: a Qoltec i9300 Li-ion cell (3.7V nominal, 3100mAh), a generic
charge/protection board silkscreened `03962A` (micro-USB in, `B+`/`B-` to
the raw cell, `OUT+`/`OUT-` protected output - TP4056-style charger plus
over-discharge/overcurrent protection), an INA219 current/voltage
monitor, and a hand-built HT7333 LDO (with 1000uF + 100nF on `Vout`->GND).

```
cell(+) ---- B+  [03962A BMS]  OUT+ ---- INA219 VIN+
cell(-) ---- B-                          INA219 VIN- ---- HT7333 Vin
                                OUT- --+  HT7333 GND ------+
                                       |                   |
                                       +---- INA219 GND ---+---- NodeMCU G
                                                            |
                                          HT7333 Vout ------+---- NodeMCU 3V3
                                                            |
                                                       INA219 VCC
```

| Connection | Notes |
|---|---|
| Battery (+) / (-) | to BMS `B+` / `B-` |
| BMS `OUT+` | to INA219 `VIN+` |
| INA219 `VIN-` | to HT7333 `Vin` |
| HT7333 `Vout` | to NodeMCU **3V3** pin - leave **VIN unconnected** |
| HT7333 `Vout` | also to INA219 `VCC` (its logic supply, 3-5V range) |
| BMS `OUT-`, HT7333 `GND`, INA219 `GND` | common ground, tied to NodeMCU `G` |
| INA219 `SDA` | NodeMCU `D2` (GPIO4) - shares the bus with the OLED, different address |
| INA219 `SCL` | NodeMCU `D1` (GPIO5) |
| BMS micro-USB | charging input only - plug in a USB cable to charge, no NodeMCU wiring involved |

- INA219 default I2C address is `0x40`, distinct from the OLED's `0x3C` -
  both can sit on the same SDA/SCL pair with no conflict. If you're on the
  TFT backend (SPI, no OLED), `D1`/`D2` are otherwise unused so the wiring
  is identical either way.
- INA219 sits on the *unregulated* side (battery voltage, pre-HT7333), not
  the 3.3V rail - that's deliberate, since the 3.3V rail stays flat
  regardless of charge state and would be useless for estimating state of
  charge. Its onboard shunt is rated for the ~3.2A this specific breakout
  advertises, well above anything the ESP8266 draws.
- If you flash/debug over the NodeMCU's own USB port while the battery is
  also connected to 3V3, the onboard AMS1117 (powered from USB 5V) ends up
  driving the same 3.3V rail in parallel with the HT7333. Both regulate to
  ~3.3V so this is generally fine (they just share the load), but
  disconnecting the battery first is the safer habit if you want to avoid
  it entirely.

### Consolidating for a portable build

The jumper-wire version of the above (battery -> BMS -> INA219 -> HT7333,
each leg its own wire, ground run separately to every board) works but
turns into a rat's nest fast. Since every one of those connections is
fixed and never changes, none of it needs to be flying wire - solder the
BMS, INA219, and HT7333 (+ its two caps) onto one small perfboard/
stripboard "power module" instead, wired point-to-point in place with
short leads. That turns ~10 loose jumpers into fixed solder joints on one
board, with only **4 wires leaving the module**:

| Wire off the power module | To |
|---|---|
| 3.3V (HT7333 `Vout` rail) | NodeMCU `3V3` |
| GND (shared rail - BMS `OUT-`, HT7333 `GND`, INA219 `GND` all land on one strip on the board, not separate wires) | NodeMCU `G` |
| INA219 `SDA` | NodeMCU `D2` |
| INA219 `SCL` | NodeMCU `D1` |

INA219 `VCC` taps the same 3.3V rail *on the board itself* - it doesn't
need its own wire back to the NodeMCU. The TFT's own ~7 wires (SPI +
power) are unaffected - that count is fixed by the display interface, not
by how the power section is built.

Two additions worth making at the same time, since you're rebuilding this
anyway:

- **A 2-pin connector (JST-PH or similar) between the battery pads and the
  BMS `B+`/`B-`** instead of a permanent solder joint. Lets you unplug the
  cell to charge it separately or swap packs without touching a soldering
  iron again.
- **A small inline SPST switch** on the 3.3V wire between the power
  module and the NodeMCU. Without one, "off" means physically
  disconnecting something - not very practical for something meant to be
  pocketable.

For durability once it's all soldered: heat-shrink every joint, and
hot-glue or foam-tape the power module and battery down inside whatever
enclosure you're using so flex/vibration doesn't fatigue the solder
joints over time - that's usually what kills a portable build, not the
wiring plan itself.

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
