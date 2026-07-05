# ESP8266 Wi-Fi Scanner + Packet Sender Project

## Goal
Build a small ESP8266 device that:
- scans nearby Wi-Fi networks,
- shows them on a small OLED screen,
- displays useful details such as SSID, signal strength, channel, and security type,
- lets you select a network and send your own simple packets (for example UDP/TCP data).

## What you need
### Hardware
- ESP8266 board: NodeMCU or WEMOS D1 Mini is the easiest choice.
- Small OLED display: 0.96" or 1.3" SSD1306, I2C, 128x64.
- Buttons: 2 or 3 tactile buttons.
- Power: 3.3V supply, preferably a Li-ion/Li-po battery with protection and a charging circuit.
- Optional: voltage regulator and a power switch.

### Software
- Arduino IDE or PlatformIO.
- Recommended Arduino libraries:
  - ESP8266WiFi
  - Adafruit_GFX
  - Adafruit_SSD1306
  - OneButton
- Optional: ArduinoJson if you want to store settings or parse data.

## Recommended setup
For a beginner-friendly build, use:
- Board: NodeMCU V3 with ESP8266 (this matches your hardware)
- Display: SSD1306 OLED over I2C
- Buttons: 2 buttons for up/down and 1 button for select
- Battery: 3.7V Li-ion cell with protection board

## PlatformIO setup
If you are using PlatformIO, create a new project for the ESP8266 and use this board setting:
- board = nodemcuv2

Example platformio.ini section:
```ini
[env:nodemcuv2]
platform = espressif8266
board = nodemcuv2
framework = arduino
lib_deps =
    adafruit/Adafruit GFX Library
    adafruit/Adafruit SSD1306
    mathertel/OneButton
```

## Wiring overview
- OLED SDA -> ESP8266 D2 (or any I2C data pin)
- OLED SCL -> ESP8266 D1 (or any I2C clock pin)
- Button 1 -> GPIO pin, with pull-down or use internal pull-up
- Button 2 -> GPIO pin
- Button 3 -> GPIO pin
- Battery -> 3.3V regulator or direct 3.3V if the board supports it safely

## Recommended development tools
- Arduino IDE is easiest for first-time users.
- PlatformIO is better if you want cleaner project structure and easier library management.

## How to program the ESP8266
### Option 1: PlatformIO (recommended for you)
1. Open PlatformIO Home.
2. Create a new project.
3. Set the board to nodemcuv2.
4. Add the required libraries in platformio.ini.
5. Write your code in src/main.cpp.
6. Click Upload to flash the firmware.

### Option 2: Arduino IDE
1. Install Arduino IDE.
2. Add this board manager URL in Preferences:
   - http://arduino.esp8266.com/stable/package_esp8266com_index.json
3. Open Boards Manager and install the ESP8266 package.
4. Select your board:
   - NodeMCU 1.0 (ESP-12E Module)
5. Select the correct COM port.
6. Press Upload to flash the firmware.

### Flashing tip
If the board does not upload correctly:
- hold BOOT/FLASH,
- press RESET,
- release BOOT/FLASH,
- then try the upload again.

## What to implement
### 1. Wi-Fi scanning
Use the ESP8266 Wi-Fi scan API to list nearby networks.
Show:
- SSID
- RSSI (signal strength)
- Channel
- Encryption type
- BSSID if needed

### 2. OLED display
Use the screen to show:
- a list of networks,
- a selected network,
- extra details on a second screen or after pressing a button.

### 3. Buttons
Use buttons to:
- scroll the list,
- open details,
- trigger a simple send action.

### 4. Packet sending
For simple network communication, the ESP8266 can send:
- UDP packets
- TCP packets

Important limitation:
- raw Wi-Fi frame injection and monitor-mode packet sending are not practical on ESP8266 for most beginner projects.
- If you need true raw packet crafting or advanced wireless injection, an ESP32 or a dedicated radio board is a better choice.

## Suggested project order
1. Get the OLED working.
2. Get the buttons working.
3. Add Wi-Fi scanning.
4. Show network information on the screen.
5. Add a simple packet sender function.
6. Add battery monitoring and sleep mode if needed.

## Good first version
A strong first version is:
- scan nearby Wi-Fi networks,
- list them on the OLED,
- show RSSI and encryption type,
- send a small UDP packet to a known IP/port when a button is pressed.

## Final advice
Start simple. Build a working scanner first, then add features one by one. The ESP8266 is excellent for scanning and basic networking, but it is not the best choice if your main goal is advanced raw packet injection.
