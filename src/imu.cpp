#include "imu.h"
#include <Wire.h>
#include "core/motion_sim.h"

namespace imu {
namespace {
enum Chip : uint8_t { NONE, MPU, LSM, QMI };
constexpr uint32_t SAMPLE_MS = 10;

TaskHandle_t s_task = nullptr;
QueueHandle_t s_evQ = nullptr;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_enabled = false;
volatile float s_sens = 1.f;
Chip s_chip = NONE;
uint8_t s_addr = 0;
volatile bool s_probed = false;

State s_state{};
MotionSample s_hold{};
volatile uint32_t s_holdUntil = 0;
volatile int s_play = -1;
volatile uint32_t s_playStart = 0;
volatile bool s_resetReq = false;

bool wr(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(s_addr);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission() == 0;
}
bool rd(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)addr, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) buf[i] = Wire.read();
  return true;
}

bool probe() {
  if (IMU_SDA < 0) return false;
  Wire.begin(IMU_SDA, IMU_SCL, 400000);
  Wire.setTimeOut(20);
  for (uint8_t addr : {0x68, 0x69, 0x6A, 0x6B}) {
    uint8_t id;
    // QMI8658 (the Waveshare AMOLED): asked first at 0x6A/0x6B, before the LSM6DS check reads a register it does not have
    if (addr >= 0x6A && rd(addr, 0x00, &id, 1) && id == 0x05) {
      s_addr = addr;
      s_chip = QMI;
      wr(0x60, 0xB0);  // soft reset (done within 15 ms)
      delay(20);
      wr(0x02, 0x40);  // address auto-increment, little-endian
      wr(0x03, 0x15);  // accelerometer +-4 g, 250 Hz
      wr(0x04, 0x55);  // gyroscope +-512 dps, 224 Hz
      wr(0x08, 0x03);  // both on
      return true;
    }
    if (addr <= 0x69 && rd(addr, 0x75, &id, 1) && id != 0x00 && id != 0xFF) {
      s_addr = addr;
      s_chip = MPU;
      wr(0x6B, 0x00);
      delay(10);
      wr(0x1A, 0x03);
      wr(0x1B, 0x08);
      wr(0x1C, 0x08);
      return true;
    }
    if (addr >= 0x6A && rd(addr, 0x0F, &id, 1) && (id == 0x69 || id == 0x6A || id == 0x6B || id == 0x6C)) {
      s_addr = addr;
      s_chip = LSM;
      wr(0x12, 0x44);
      wr(0x10, 0x58);
      wr(0x11, 0x54);
      return true;
    }
  }
  s_chip = NONE;
  return false;
}

int16_t be16(const uint8_t* p) { return (int16_t)(p[0] << 8 | p[1]); }
int16_t le16(const uint8_t* p) { return (int16_t)(p[1] << 8 | p[0]); }

bool readSensor(MotionSample& out) {
  float v[6];
  uint8_t b[14];
  if (s_chip == MPU) {
    if (!rd(s_addr, 0x3B, b, 14)) return false;
    v[0] = be16(b) / 8192.f;
    v[1] = be16(b + 2) / 8192.f;
    v[2] = be16(b + 4) / 8192.f;
    v[3] = be16(b + 8) / 65.5f;
    v[4] = be16(b + 10) / 65.5f;
    v[5] = be16(b + 12) / 65.5f;
  } else if (s_chip == LSM) {
    if (!rd(s_addr, 0x22, b, 12)) return false;
    v[3] = le16(b) / 57.14f;
    v[4] = le16(b + 2) / 57.14f;
    v[5] = le16(b + 4) / 57.14f;
    v[0] = le16(b + 6) / 8192.f;
    v[1] = le16(b + 8) / 8192.f;
    v[2] = le16(b + 10) / 8192.f;
  } else if (s_chip == QMI) {
    if (!rd(s_addr, 0x35, b, 12)) return false;
    for (int i = 0; i < 3; i++) {
      v[i] = le16(b + 2 * i) / 8192.f;
      v[3 + i] = le16(b + 6 + 2 * i) / 64.f;
    }
  } else {
    return false;
  }
  float d[6];
  for (int i = 0; i < 3; i++) {
    d[i] = d[3 + i] = 0;
    for (int j = 0; j < 3; j++) {
      d[i] += IMU_MAP[i][j] * v[j];
      d[3 + i] += IMU_MAP[i][j] * v[3 + j];
    }
  }
  out = {d[0], d[1], d[2], d[3], d[4], d[5]};
  return true;
}

void imuTask(void*) {
  MotionClassifier cls;
  uint32_t lastUs = micros(), failures = 0, lastProbe = 0;
  TickType_t wake = xTaskGetTickCount();
  for (;;) {
    vTaskDelayUntil(&wake, pdMS_TO_TICKS(SAMPLE_MS));
    const uint32_t nowMs = millis();
    if (s_resetReq) {
      cls = MotionClassifier();
      s_resetReq = false;
    }
    cls.setSensitivity(s_sens);
    MotionSample s{};
    bool have = false;
    if (s_play >= 0) {
      float t = (nowMs - s_playStart) * 1e-3f;
      if (t > SIM_SECONDS[s_play])
        s_play = -1;
      else {
        s = simSample(s_play, t);
        have = true;
      }
    }
    if (!have && (int32_t)(s_holdUntil - nowMs) > 0) {
      s = s_hold;
      have = true;
    }
    if (!have && s_enabled) {
      if (s_chip == NONE) {
        if (nowMs - lastProbe > 5000 || lastProbe == 0) {
          lastProbe = nowMs;
          if (probe())
            Serial.printf("imu: found a %s chip\n", chip());
        }
      } else if (readSensor(s)) {
        have = true;
        failures = 0;
      } else if (++failures > 50) {
        s_chip = NONE;
        failures = 0;
        lastProbe = nowMs;
      }
    }
    if (!have) {
      portENTER_CRITICAL(&s_mux);
      s_state.live = false;
      portEXIT_CRITICAL(&s_mux);
      if (!s_enabled && s_play < 0) break;
      lastUs = micros();
      continue;
    }
    uint32_t us = micros();
    float dt = (us - lastUs) * 1e-6f;
    lastUs = us;
    if (dt > 0.1f) dt = 0.1f;
    MotionOut o = cls.update(s, dt);
    if (o.event != ME_NONE) xQueueSend(s_evQ, &o, 0);
    portENTER_CRITICAL(&s_mux);
    s_state = {cls.tiltX(), cls.tiltY(), cls.flatX(), cls.flatY(), cls.rollDeg(), cls.orientation(), cls.carried(), true, s};
    portEXIT_CRITICAL(&s_mux);
  }
  s_task = nullptr;
  vTaskDelete(nullptr);
}

void ensureTask() {
  if (!s_evQ) s_evQ = xQueueCreate(8, sizeof(MotionOut));
  if (!s_task) xTaskCreatePinnedToCore(imuTask, "imu", 4096, nullptr, 2, &s_task, 0);
}
}  // namespace

void enable(bool on) {
  if (on == s_enabled) return;
  s_enabled = on;
  if (on) ensureTask();
}

void sensitivity(float s) { s_sens = s; }

bool popEvent(MotionOut& out) { return s_evQ && xQueueReceive(s_evQ, &out, 0) == pdTRUE; }

State state() {
  State s;
  portENTER_CRITICAL(&s_mux);
  s = s_state;
  portEXIT_CRITICAL(&s_mux);
  return s;
}

const char* chip() {
  return s_chip == MPU   ? "MPU-6050 family"
         : s_chip == LSM ? "LSM6DS family"
         : s_chip == QMI ? "QMI8658"
         : s_enabled     ? "none"
                         : "none (off)";
}

void simulate(const MotionSample& s, uint32_t holdMs) {
  s_hold = s;
  s_holdUntil = millis() + holdMs;
  ensureTask();
}

bool play(int scenario) {
  if (scenario < 0 || scenario >= SIM_COUNT) return false;
  s_resetReq = true;
  s_playStart = millis();
  s_play = scenario;
  ensureTask();
  return true;
}
}  // namespace imu
