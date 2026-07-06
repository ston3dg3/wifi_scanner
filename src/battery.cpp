#include "battery.h"
#include <Wire.h>
#include <Adafruit_INA219.h>

static Adafruit_INA219 ina219;
static bool sensorReady = false;

void batteryInit() {
  Wire.begin(); // idempotent if screenInit() (OLED) already called it; TFT builds need this call themselves
  sensorReady = ina219.begin();
  if (!sensorReady) return;
  ina219.setCalibration_32V_2A(); // headroom for the BMS board's rated 3.2A; the ESP8266 itself draws well under this
}

// Rough 1S Li-ion rest-voltage -> state-of-charge curve. Under load the
// pack sags a bit below its resting voltage, so treat this as an estimate,
// not a precision fuel gauge.
static int voltageToPercent(float v) {
  static const float points[][2] = {
    {4.20, 100}, {4.06, 90}, {3.98, 80}, {3.92, 70}, {3.87, 60},
    {3.82, 50}, {3.79, 40}, {3.77, 30}, {3.74, 20}, {3.68, 10},
    {3.45, 5}, {3.00, 0},
  };
  const int n = sizeof(points) / sizeof(points[0]);
  if (v >= points[0][0]) return 100;
  if (v <= points[n - 1][0]) return 0;
  for (int i = 0; i < n - 1; i++) {
    if (v <= points[i][0] && v >= points[i + 1][0]) {
      float frac = (v - points[i + 1][0]) / (points[i][0] - points[i + 1][0]);
      return (int)(points[i + 1][1] + frac * (points[i][1] - points[i + 1][1]));
    }
  }
  return 0;
}

BatteryStatus batteryRead() {
  BatteryStatus status = {0, 0, 0, sensorReady};
  if (!sensorReady) return status;
  status.voltageV = ina219.getBusVoltage_V();
  status.currentMA = ina219.getCurrent_mA();
  status.percent = voltageToPercent(status.voltageV);
  return status;
}
