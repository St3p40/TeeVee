#include "battery.h"
#include "board.h"

#if BOARD_HAS_BATTERY
#include <Arduino.h>
#include <Wire.h>

// The AXP2101 power chip (I2C 0x34, on the same bus as the touch, the motion sensor and the codec; the display driver starts the bus).
// Registers, as in the chip's data sheet and XPowersLib: 0x00 status 1 (bit 5: power on VBUS, bit 3: a battery is connected),
// 0x01 status 2 (bits 6..5: 01 = charging), 0x03 the chip's ID (0x4A), 0xA4 the battery's charge in percent.
namespace {
bool readReg(uint8_t reg, uint8_t& v) {
  Wire.beginTransmission(BATTERY_PMU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)BATTERY_PMU_ADDR, 1) != 1) return false;
  v = (uint8_t)Wire.read();
  return true;
}
}  // namespace

battery::State battery::read() {
  State s;
  s.answered = readReg(0x03, s.chip) && readReg(0x00, s.raw0) && readReg(0x01, s.raw1) && readReg(0xA4, s.rawPct);
  if (!s.answered) return s;
  s.usb = s.raw0 & 0x20;
  s.charging = ((s.raw1 >> 5) & 3) == 1;
  s.percent = s.rawPct > 100 ? 100 : s.rawPct;
  s.known = (s.raw0 & 0x08) && s.rawPct <= 100;  // no battery, or a reading that is not a percentage: show nothing
  return s;
}
#else
battery::State battery::read() { return State(); }
#endif
