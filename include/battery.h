#pragma once

#include <Arduino.h>

// Battery voltage/current monitoring via an INA219 wired into the high
// side of the battery -> HT7333 regulator path (see docs/wiring.md). It
// shares the I2C bus with the OLED (SDA=D2, SCL=D1) - different address
// (0x40 default vs. the OLED's 0x3C), so no conflict, and it works the
// same whether the OLED or TFT backend is active.

struct BatteryStatus {
  float voltageV;   // bus voltage at the INA219's V- pin, i.e. battery/BMS output
  float currentMA;  // load current draw, positive = discharging
  int percent;      // 0-100, estimated from voltageV via a 1S Li-ion discharge curve
  bool sensorReady; // false if the INA219 didn't ack on the bus (not wired / wrong address)
};

// Starts the INA219. Call once from setup(). Safe to call even if the
// sensor isn't wired up - batteryRead() just reports sensorReady = false.
void batteryInit();

// Reads the current voltage/current and derives a percentage estimate.
// Cheap enough to call every loop iteration.
BatteryStatus batteryRead();
