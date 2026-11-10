#pragma once
#include <stdint.h>

// The battery, on boards with a power chip that measures it (the AMOLED board's AXP2101). Read-only: nothing is ever written to the
// power chip, so a wrong guess here cannot switch anything off.
namespace battery {
struct State {
    bool answered = false;  // the chip is on the bus at all
    bool known = false;     // it answered and says a battery is connected
    bool charging = false;
    bool usb = false;       // power on the USB socket
    uint8_t percent = 0;    // 0..100, the chip's own fuel gauge
    uint8_t chip = 0, raw0 = 0, raw1 = 0, rawPct = 0;  // what was read (the console's "battery" prints it)
};
// Reads the chip (a few I2C bytes). Call from the brain's task, every few seconds at most.
State read();
}  // namespace battery
