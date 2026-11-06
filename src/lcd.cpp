#include "core/lcd.h"
#include "core/platform.h"
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include "board.h"

namespace {
constexpr int TP_SCLK = 25, TP_MOSI = 32, TP_MISO = 39, TP_CS = 33, TP_IRQ = 36;
constexpr int MAX_XFER = 16384;  // largest single DMA transfer (widget panel 104x70 = 14.5 KB)
constexpr int FILL_ROWS = 16;

constexpr int TP_X_MIN = 300, TP_X_MAX = 3900, TP_Y_MIN = 3700, TP_Y_MAX = 200;
constexpr int TP_PRESSURE = 400;

spi_device_handle_t s_lcd;
uint16_t* s_clip = nullptr;

// D/C line: set from the transaction's user field just before it goes out (0 = command, 1 = data).
void IRAM_ATTR dcCallback(spi_transaction_t* t) {
  gpio_set_level((gpio_num_t)LCD_DC, (int)(intptr_t)t->user);
}
}  // namespace

void Lcd::cmd(uint8_t c, const uint8_t* data, int len) {
  spi_transaction_t t = {};
  t.length = 8;
  t.flags = SPI_TRANS_USE_TXDATA;
  t.tx_data[0] = c;
  t.user = (void*)0;
  spi_device_polling_transmit(s_lcd, &t);
  if (len > 0) {
    spi_transaction_t d = {};
    d.length = len * 8;
    d.user = (void*)1;
    if (len <= 4) {
      d.flags = SPI_TRANS_USE_TXDATA;
      memcpy(d.tx_data, data, len);
    } else {
      d.tx_buffer = data;
    }
    spi_device_polling_transmit(s_lcd, &d);
  }
}

void Lcd::window(int x, int y, int w, int h) {
  x += LCD_X_OFFSET;
  y += LCD_Y_OFFSET;
  const uint8_t ca[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)((x + w - 1) >> 8), (uint8_t)(x + w - 1)};
  const uint8_t ra[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)((y + h - 1) >> 8), (uint8_t)(y + h - 1)};
  cmd(0x2A, ca, 4);
  cmd(0x2B, ra, 4);
  cmd(0x2C);
}

void Lcd::begin(uint8_t madctl) {
  gpio_set_direction((gpio_num_t)LCD_DC, GPIO_MODE_OUTPUT);
  spi_bus_config_t bus = {};
  bus.mosi_io_num = LCD_MOSI;
  bus.miso_io_num = LCD_MISO;
  bus.sclk_io_num = LCD_SCLK;
  bus.quadwp_io_num = -1;
  bus.quadhd_io_num = -1;
  bus.max_transfer_sz = MAX_XFER;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  if (LCD_CROP) s_clip = (uint16_t*)heap_caps_malloc(MAX_XFER, MALLOC_CAP_DMA);
  spi_device_interface_config_t dev = {};
  dev.mode = LCD_SPI_MODE;
  dev.clock_speed_hz = LCD_HZ;
  dev.spics_io_num = LCD_CS;
  dev.queue_size = 2;
  dev.pre_cb = dcCallback;
  dev.flags = SPI_DEVICE_NO_DUMMY;
  ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_lcd));

  struct Init {
      uint8_t cmd, len;
      uint8_t data[15];
      uint16_t delayMs;
  };
  static const Init ILI9341[] = {
    {0x01, 0, {}, 150},
    {0xEF, 3, {0x03, 0x80, 0x02}, 0},
    {0xCF, 3, {0x00, 0xC1, 0x30}, 0},
    {0xED, 4, {0x64, 0x03, 0x12, 0x81}, 0},
    {0xE8, 3, {0x85, 0x00, 0x78}, 0},
    {0xCB, 5, {0x39, 0x2C, 0x00, 0x34, 0x02}, 0},
    {0xF7, 1, {0x20}, 0},
    {0xEA, 2, {0x00, 0x00}, 0},
    {0xC0, 1, {0x23}, 0},
    {0xC1, 1, {0x10}, 0},
    {0xC5, 2, {0x3E, 0x28}, 0},
    {0xC7, 1, {0x86}, 0},
    {0x3A, 1, {0x55}, 0},
    {0xB1, 2, {0x00, 0x18}, 0},
    {0xB6, 3, {0x08, 0x82, 0x27}, 0},
    {0xF2, 1, {0x00}, 0},
    {0x26, 1, {0x01}, 0},
    {0xE0, 15, {0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00}, 0},
    {0xE1, 15, {0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F}, 0},
    {0x11, 0, {}, 120},
    {0x29, 0, {}, 20},
  };
  static const Init ST7789[] = {
    {0x01, 0, {}, 120},
    {0x11, 0, {}, 120},
    {0x3A, 1, {0x55}, 0},
    {0xB0, 2, {0x00, 0xF0}, 0},                            // RAM control: big-endian pixels (as the strips hold them)
    {0xB2, 5, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 0},
    {0xB7, 1, {0x35}, 0},
    {0xBB, 1, {0x19}, 0},
    {0xC0, 1, {0x2C}, 0},
    {0xC2, 1, {0x01}, 0},
    {0xC3, 1, {0x12}, 0},
    {0xC4, 1, {0x20}, 0},
    {0xC6, 1, {0x0F}, 0},
    {0xD0, 2, {0xA4, 0xA1}, 0},
    {0xE0, 14, {0xF0, 0x09, 0x13, 0x12, 0x12, 0x2B, 0x3C, 0x44, 0x4B, 0x1B, 0x18, 0x17, 0x1D, 0x21}, 0},
    {0xE1, 14, {0xF0, 0x09, 0x13, 0x0C, 0x0D, 0x27, 0x3B, 0x44, 0x4D, 0x0B, 0x17, 0x17, 0x1D, 0x21}, 0},
    {0x21, 0, {}, 0},
    {0x13, 0, {}, 10},
    {0x29, 0, {}, 20},
  };
  const Init* init = LCD_ST7789 ? ST7789 : ILI9341;
  const size_t n = LCD_ST7789 ? sizeof ST7789 / sizeof ST7789[0] : sizeof ILI9341 / sizeof ILI9341[0];
  for (size_t i = 0; i < n; i++) {
    cmd(init[i].cmd, init[i].data, init[i].len);
    if (init[i].delayMs) delay(init[i].delayMs);
  }
  setMadctl(madctl);

  if (LCD_BL >= 0) ledcAttach(LCD_BL, 5000, 8);
  setBrightness(200);
}

void Lcd::setMadctl(uint8_t m) {
  wait();
  _madctl = m;
  cmd(0x36, &m, 1);
}

void Lcd::setBrightness(uint8_t level) {
  if (LCD_BL >= 0) ledcWrite(LCD_BL, level);
}

void Lcd::setDisplayOn(bool on) {
  wait();
  cmd(on ? 0x29 : 0x28);
}

void (*g_lcdTee)(int x, int y, int w, int h, const uint16_t* pixels) = nullptr;

void Lcd::push(int x, int y, int w, int h, const uint16_t* pixels) {
  if (g_lcdTee) g_lcdTee(x, y, w, h, pixels);
  wait();  // window commands can't go out while a DMA transfer is queued (and s_clip is free again)
  if (LCD_CROP) {
    const int x0 = x > LCD_CROP ? x : LCD_CROP,
              x1 = x + w < LCD_CROP + LCD_PANEL_W ? x + w : LCD_CROP + LCD_PANEL_W;
    if (x1 <= x0 || !s_clip) return;
    if (x0 != x || x1 != x + w) {
      const int cw = x1 - x0;
      for (int r = 0; r < h; r++) memcpy(s_clip + r * cw, pixels + r * w + (x0 - x), cw * 2);
      pixels = s_clip;
    }
    w = x1 - x0;
    x = x0 - LCD_CROP;
  }
  window(x, y, w, h);
  static spi_transaction_t t;
  t = {};
  t.length = w * h * 16;
  t.tx_buffer = pixels;
  t.user = (void*)1;
  ESP_ERROR_CHECK(spi_device_queue_trans(s_lcd, &t, portMAX_DELAY));
  _pending = true;
}

void Lcd::wait() {
  if (!_pending) return;
  spi_transaction_t* done;
  spi_device_get_trans_result(s_lcd, &done, portMAX_DELAY);
  _pending = false;
}

void Lcd::fillScreen(uint16_t c) {
  uint16_t* rows = (uint16_t*)heap_caps_malloc(W * FILL_ROWS * 2, MALLOC_CAP_DMA);
  if (!rows) return;
  const uint16_t s = (uint16_t)(c >> 8 | c << 8);
  wait();
  for (int i = 0; i < W * FILL_ROWS; i++) rows[i] = s;
  for (int y = 0; y < H; y += FILL_ROWS) push(0, y, W, FILL_ROWS < H - y ? FILL_ROWS : H - y, rows);
  wait();
  free(rows);
}

//==============================
// touch
//==============================

// The XPT2046 is read by bit-banging its four pins (a few bytes at 50 Hz, ~150 us a read): that leaves the VSPI hardware bus (SPI3) free for the
// microSD slot, whose pins (IO5/18/19/23) are different from the touch pins and would otherwise need a third SPI host.
void TouchPanel::begin() {
  if (!BOARD_HAS_TOUCH) return;
  pinMode(TP_IRQ, INPUT);
  pinMode(TP_CS, OUTPUT);
  pinMode(TP_SCLK, OUTPUT);
  pinMode(TP_MOSI, OUTPUT);
  pinMode(TP_MISO, INPUT);
  digitalWrite(TP_CS, HIGH);
  digitalWrite(TP_SCLK, LOW);
}

static int tpRead(uint8_t command) {
  digitalWrite(TP_CS, LOW);
  delayMicroseconds(1);
  uint32_t v = 0;
  for (int i = 0; i < 24; i++) {
    digitalWrite(TP_MOSI, i < 8 ? (command >> (7 - i)) & 1 : 0);
    delayMicroseconds(1);
    digitalWrite(TP_SCLK, HIGH);
    delayMicroseconds(1);
    if (i >= 8) v = (v << 1) | (uint32_t)digitalRead(TP_MISO);
    digitalWrite(TP_SCLK, LOW);
  }
  digitalWrite(TP_CS, HIGH);
  return (int)(v >> 3) & 0xFFF;
}

static int median3(int a, int b, int c) {
  return a > b ? (b > c ? b : (a > c ? c : a)) : (a > c ? a : (b > c ? c : b));
}

bool TouchPanel::read(int& x, int& y, uint8_t madctl) {
  if (!BOARD_HAS_TOUCH || digitalRead(TP_IRQ)) return false;
  int z = tpRead(0xB1) + 4095 - tpRead(0xC1);
  int rx = median3(tpRead(0xD1), tpRead(0xD1), tpRead(0xD1));
  int ry = median3(tpRead(0x91), tpRead(0x91), tpRead(0x91));
  tpRead(0xD0);
  if (z < TP_PRESSURE) return false;
  int px = (rx - TP_X_MIN) * 240 / (TP_X_MAX - TP_X_MIN);
  int py = (ry - TP_Y_MIN) * 320 / (TP_Y_MAX - TP_Y_MIN);
  px = px < 0 ? 0 : px > 239 ? 239 : px;
  py = py < 0 ? 0 : py > 319 ? 319 : py;
  if (madctl & 0x40) px = 239 - px;
  if (madctl & 0x80) py = 319 - py;
  if (madctl & 0x20) {
    x = 319 - py;
    y = px;
  } else {
    x = px;
    y = py;
  }
  return true;
}

// The platform hooks of core/platform.h, for the board: pixel buffers in DMA-capable memory, and the microsecond clock.
uint16_t* tvAllocPixels(size_t bytes) {
  return (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}
uint32_t tvMicros() { return (uint32_t)micros(); }
