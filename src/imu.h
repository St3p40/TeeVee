#pragma once
#include <Arduino.h>
#include "core/motion.h"

// Motion sensor: an I2C 6-axis chip (MPU-6050 family at 0x68/0x69, LSM6DS3/DSO family at 0x6A/0x6B), sampled at 100 Hz on its own
// task and fed to the MotionClassifier. Also a simulator (`imu sim`, `imu play`) so everything can be tried without a sensor.
// Off until switched on (setting `motion`); with no chip answering it just stays quiet.
//
// Wiring: board.h (BOARD_IMU_SDA / SCL; on the CYD IO27 / IO22 on the CN1 connector, 3V3 and GND; -1 = the board has none). Mounting: see MotionSample in motion.h
// (x right, y up the screen, z out of the screen); if yours differs, edit IMU_MAP (rows = device x, y, z; sensor axes in columns).
#include "board.h"
#define IMU_SDA BOARD_IMU_SDA
#define IMU_SCL BOARD_IMU_SCL
#ifdef BOARD_IMU_MAP  // a board with its sensor built in knows how it is mounted
static const int IMU_MAP[3][3] = BOARD_IMU_MAP;
#else
static const int IMU_MAP[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
#endif

namespace imu {
void enable(bool on);
void sensitivity(float s);
bool popEvent(MotionOut& out);
struct State {
    float tiltX, tiltY;
    float flatX, flatY;  // lying flat: which way is downhill
    float roll;          // standing: degrees turned round the screen's axis
    uint8_t orientation;
    bool carried, live;
    MotionSample last;
};
State state();
const char* chip();
void simulate(const MotionSample& s, uint32_t holdMs);
bool play(int scenario);
}  // namespace imu
