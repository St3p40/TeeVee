#include "audio.h"
#include "board.h"

#if BOARD_HAS_AUDIO
#include <Wire.h>
#include <driver/i2s_std.h>
#include "core/voice.h"

namespace audio {
namespace {
constexpr int RATE = 16000;
constexpr int BLOCK = RATE / 50;          // 20 ms
constexpr uint32_t AMP_OFF_AFTER_MS = 400;
// The mic waits this long after the voice stops: past the amplifier switching off (it clicks) and the room's echo.
constexpr uint32_t DEAF_AFTER_MS = AMP_OFF_AFTER_MS + 400;

struct SayReq {
    uint8_t ev;
    PAD mood;
    float k;
};

TaskHandle_t s_task = nullptr;
QueueHandle_t s_evQ = nullptr, s_sayQ = nullptr;
i2s_chan_handle_t s_tx = nullptr, s_rx = nullptr;
volatile bool s_mic = false, s_sound = false, s_ok = false, s_failed = false;
volatile bool s_talking = false, s_speaking = false;
volatile float s_level = 0, s_sens = 1, s_volume = 0.6f;

bool wr(uint8_t reg, uint8_t v) {
  Wire.beginTransmission(AUDIO_CODEC_ADDR);
  Wire.write(reg);
  Wire.write(v);
  return Wire.endTransmission() == 0;
}
uint8_t rd(uint8_t reg) {
  Wire.beginTransmission(AUDIO_CODEC_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0 || Wire.requestFrom((int)AUDIO_CODEC_ADDR, 1) != 1) return 0;
  return (uint8_t)Wire.read();
}

// The ES8311 as Espressif's es8311 driver sets it (Waveshare's 15_ES8311 example): slave, I2S 16-bit, MCLK from its pin at
// 256 x 16 kHz, analog mic with the PGA and ADC on, and the DAC with its output driver.
bool codecInit() {
  if (!wr(0x00, 0x1F)) return false;  // reset
  wr(0x00, 0x00);
  wr(0x00, 0x80);                     // power on
  wr(0x01, 0x3F);                     // all clocks on, MCLK from the MCLK pin
  wr(0x06, rd(0x06) & ~0x20);         // SCLK not inverted
  // clock dividers for 4.096 MHz MCLK at 16 kHz: pre_div 1, x1, adc/dac div 1, single speed, OSR 0x10, LRCK 256
  wr(0x02, rd(0x02) & 0x07);
  wr(0x03, 0x10);
  wr(0x04, 0x10);
  wr(0x05, 0x00);
  wr(0x06, (rd(0x06) & 0xE0) | 0x03);
  wr(0x07, rd(0x07) & 0xC0);
  wr(0x08, 0xFF);
  wr(0x00, rd(0x00) & 0xBF);  // slave
  wr(0x09, 0x0C);             // serial in: 16-bit I2S
  wr(0x0A, 0x0C);             // serial out: 16-bit I2S
  wr(0x0D, 0x01);             // analog on
  wr(0x0E, 0x02);             // PGA and ADC modulator on
  wr(0x12, 0x00);             // DAC on
  wr(0x13, 0x10);             // output driver on
  wr(0x1C, 0x6A);             // ADC equaliser bypassed, DC offset removed
  wr(0x37, 0x08);             // DAC equaliser bypassed
  wr(0x17, 0xC8);             // ADC volume
  wr(0x14, 0x1A);             // analog mic, PGA gain
  wr(0x16, 0x03);             // ADC gain +18 dB
  wr(0x32, 0xBF);             // DAC volume 75 %: the setting `volume` scales the samples
  wr(0x31, rd(0x31) & ~0x60); // unmuted
  return true;
}

bool i2sInit() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;  // silence, not the last block again, if the task is late
  if (i2s_new_channel(&chan, &s_tx, &s_rx) != ESP_OK) return false;
  i2s_std_config_t std = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(RATE),  // MCLK = 256 x rate
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
      .gpio_cfg = {.mclk = (gpio_num_t)AUDIO_MCLK,
                   .bclk = (gpio_num_t)AUDIO_BCLK,
                   .ws = (gpio_num_t)AUDIO_WS,
                   .dout = (gpio_num_t)AUDIO_DOUT,
                   .din = (gpio_num_t)AUDIO_DIN,
                   .invert_flags = {}},
  };
  if (i2s_channel_init_std_mode(s_tx, &std) != ESP_OK || i2s_channel_init_std_mode(s_rx, &std) != ESP_OK ||
      i2s_channel_enable(s_tx) != ESP_OK || i2s_channel_enable(s_rx) != ESP_OK) {
    i2s_del_channel(s_tx);
    i2s_del_channel(s_rx);
    s_tx = s_rx = nullptr;
    return false;
  }
  return true;
}

void amp(bool on) { digitalWrite(AUDIO_AMP_EN, on ? HIGH : LOW); }

void audioTask(void*) {
  static int16_t in[BLOCK * 2], out[BLOCK * 2], mono[BLOCK];
  Hearing hearing;
  Voice voice;
  pinMode(AUDIO_AMP_EN, OUTPUT);
  amp(false);
  // The clock first (the codec needs MCLK running), then the codec.
  s_ok = i2sInit() && codecInit();
  s_failed = !s_ok;
  Serial.printf("audio: %s\n", s_ok ? "on (ES8311)" : "no codec");
  bool ampOn = false;
  uint32_t lastVoice = 0;
  while (s_ok && (s_mic || s_sound)) {
    size_t got = 0;
    if (i2s_channel_read(s_rx, in, sizeof in, &got, pdMS_TO_TICKS(200)) != ESP_OK || got == 0) continue;
    const uint32_t nowMs = millis();
    SayReq r;
    while (xQueueReceive(s_sayQ, &r, 0) == pdTRUE)
      if (s_sound) voice.say(r.ev, r.mood, r.k);
    const bool speak = s_sound && voice.busy();
    if (speak) {
      if (!ampOn) amp(ampOn = true);
      voice.render(mono, BLOCK, RATE, s_volume);
      for (int i = 0; i < BLOCK; i++) out[2 * i] = out[2 * i + 1] = mono[i];
      lastVoice = nowMs;
    } else {
      memset(out, 0, sizeof out);
      if (ampOn && nowMs - lastVoice > AMP_OFF_AFTER_MS) amp(ampOn = false);
    }
    s_speaking = speak;
    size_t put = 0;
    i2s_channel_write(s_tx, out, sizeof out, &put, pdMS_TO_TICKS(50));
    // Listening, unless it is (or just was) speaking itself.
    if (!s_mic || speak || (lastVoice && nowMs - lastVoice < DEAF_AFTER_MS)) {
      hearing.pause();
      continue;
    }
    const int n = got / 2;
    // The mic is on one slot; the other is silence or a copy, so the louder slot is the mic.
    double sum[2] = {0, 0}, mean[2] = {0, 0};
    for (int i = 0; i < n; i++) mean[i & 1] += in[i];
    for (double& m : mean) m /= n / 2 > 0 ? n / 2 : 1;
    for (int i = 0; i < n; i++) {
      const double v = in[i] - mean[i & 1];
      sum[i & 1] += v * v;
    }
    const float rms = (float)sqrt((sum[0] > sum[1] ? sum[0] : sum[1]) / (n / 2 > 0 ? n / 2 : 1));
    hearing.setSensitivity(s_sens);
    HearingOut o = hearing.update(rms, (float)(n / 2) / RATE);
    s_talking = hearing.talking();
    s_level = hearing.level();
    if (o.loud || o.voiceStart) xQueueSend(s_evQ, &o, 0);
  }
  amp(false);
  if (s_tx) i2s_channel_disable(s_tx), i2s_del_channel(s_tx);
  if (s_rx) i2s_channel_disable(s_rx), i2s_del_channel(s_rx);
  s_tx = s_rx = nullptr;
  if (s_ok) wr(0x31, rd(0x31) | 0x60), wr(0x12, 0x02), wr(0x0D, 0xFC);  // muted, DAC off, analog off
  s_ok = s_talking = s_speaking = false;
  s_level = 0;
  s_task = nullptr;
  vTaskDelete(nullptr);
}
}  // namespace

void enable(bool mic, bool sound) {
  if (mic == s_mic && sound == s_sound) return;
  s_mic = mic, s_sound = sound;
  if (!s_evQ) s_evQ = xQueueCreate(4, sizeof(HearingOut));
  if (!s_sayQ) s_sayQ = xQueueCreate(2, sizeof(SayReq));
  if ((mic || sound) && !s_task && !s_failed) xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 2, &s_task, 0);
}
void sensitivity(float s) { s_sens = s; }
void volume(float v) { s_volume = v < 0 ? 0 : v > 1 ? 1 : v; }
bool popEvent(HearingOut& out) { return s_evQ && xQueueReceive(s_evQ, &out, 0) == pdTRUE; }
bool talking() { return s_talking; }
float level() { return s_level; }
void say(uint8_t ev, const PAD& mood, float k) {
  if (!s_sayQ || !s_ok || !s_sound) return;
  const SayReq r{ev, mood, k};
  xQueueSend(s_sayQ, &r, 0);
}
bool speaking() { return s_speaking; }
const char* status() {
  if (s_failed) return "no codec";
  if (!s_ok) return s_mic || s_sound ? "starting" : "off";
  return s_speaking ? "on, speaking" : s_talking ? "on, hearing someone" : "on";
}
}  // namespace audio

#else
namespace audio {
void enable(bool, bool) {}
void sensitivity(float) {}
void volume(float) {}
bool popEvent(HearingOut&) { return false; }
bool talking() { return false; }
float level() { return 0; }
void say(uint8_t, const PAD&, float) {}
bool speaking() { return false; }
const char* status() { return "none on this board"; }
}  // namespace audio
#endif
