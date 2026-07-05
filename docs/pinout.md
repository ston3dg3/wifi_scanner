# NodeMCU v3 (CH340) Pinout

Reference: [nodeMCUv3_pinout.png](nodeMCUv3_pinout.png) (source: mischianti.org)

The board has two rows of pins. Silkscreen labels (`D0`, `D1`, ... `A0`) are what
you use in Arduino/PlatformIO code via the NodeMCU pin aliases; `GPIOxx` is the
underlying ESP8266 chip pin you'd use with the raw pin number.

## Left header

| Silkscreen | Chip pin | ESP8266 GPIO | Notes |
|---|---|---|---|
| A0 | ADC0 | - | Analog input (0-1V range), also labeled TOUT |
| G | GND | - | Ground |
| VU | VOUT USB | - | 5V from USB (when connected) |
| S3 | GPIO10 | IO10 | SPI flash, shared with onboard flash chip - avoid using |
| S2 | GPIO9 | IO9 | SPI flash, shared with onboard flash chip - avoid using |
| S1 | GPIO8 | IO8 | SPI flash MOSI - avoid using |
| SC | GPIO11 | IO11 | SPI flash CS0 - avoid using |
| SO | GPIO7 | IO7 | SPI flash MISO - avoid using |
| SK | GPIO6 | IO6 | SPI flash SCLK - avoid using |
| G | GND | - | Ground |
| 3V | 3V3 | - | 3.3V output |
| EN | EN | - | Chip enable |
| RST | RST | - | Reset (active low) |
| G | GND | - | Ground |
| VIN | VIN | - | External supply input (~5V) |

> The `S1`-`SK` pins connect to the onboard SPI flash chip and are generally
> not usable as free GPIOs.

## Right header

| Silkscreen | Chip pin | ESP8266 GPIO | Function / Notes |
|---|---|---|---|
| D0 | GPIO16 | IO16 | WAKE (used to wake from deep sleep). No PWM, no interrupts, no I2C/SPI support |
| D1 | GPIO5 | IO5 | Commonly used as I2C **SCL** |
| D2 | GPIO4 | IO4 | Commonly used as I2C **SDA** |
| D3 | GPIO0 | IO0 | **Boot mode select** - pulled low to enter flash mode. Has internal pull-up, no pull-down. Avoid driving low at boot |
| D4 | GPIO2 | IO2 | TXD1, **onboard BUILTIN LED**, has internal pull-up, no pull-down. Must be HIGH at boot |
| 3V | 3V3 | - | 3.3V output |
| G | GND | - | Ground |
| D5 | GPIO14 | IO14 | HSPI SCK |
| D6 | GPIO12 | IO12 | HSPI MISO |
| D7 | GPIO13 | IO13 | HSPI MOSI, also CTS0 / RXD2 |
| D8 | GPIO15 | IO15 | HSPI CS, also RTS0 / TXD2. **Boot mode select** - must be LOW at boot, no internal pull-down |
| RX | GPIO3 | IO3 | RXD0 (UART0 RX, used for flashing/serial monitor) |
| TX | GPIO1 | IO1 | TXD0 (UART0 TX), also CTS1 |
| G | GND | - | Ground |
| 3V | 3V3 | - | 3.3V output |

## Key things to remember

- **Built-in LED**: `D4` / `GPIO2`. It's wired **active-LOW** — `digitalWrite(LED, LOW)` turns it ON, `HIGH` turns it OFF.
- **Boot-sensitive pins**: `D3` (GPIO0), `D4` (GPIO2), and `D8` (GPIO15) affect boot/flash mode. Avoid using them as inputs with external pull resistors that fight their required boot state.
- **Safe general-purpose GPIOs** for buttons/peripherals: `D1`, `D2`, `D5`, `D6`, `D7` (D0 works too but has no interrupt/PWM support).
- **I2C default pins** (used by libraries like Wire.h by default on NodeMCU): SDA = `D2`, SCL = `D1` — matches this board's silkscreen hints.
- **Analog input**: only `A0`, range is 0-1.0V on the bare ESP8266 module (NodeMCU boards often add a divider for 0-3.3V, check your specific board).
- Avoid using `S1`, `S2`, `S3`, `SC`, `SO`, `SK` (GPIO6-11) — they're wired to the onboard SPI flash.
