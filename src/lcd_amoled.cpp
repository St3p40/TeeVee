#if defined(TEEVEE_BOARD_AMOLED)  // the other boards use lcd.cpp
#include "core/lcd.h"
#include "core/platform.h"
#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_io_spi.h>
#include <esp_lcd_panel_io.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include "board.h"

// The Waveshare AMOLED (ESP32-S3-Touch-AMOLED-1.8, V2): a 368x448 CO5300 panel on QSPI, with the 320x240 face in its middle.
// Everything follows Waveshare's board package (esp32_s3_touch_amoled_1_8 2.0.3, which their factory firmware uses): ESP-IDF's
// esp_lcd SPI panel IO in quad mode with the hardware chip-select, a software reset, their command table, and a 16-column
// offset when the touch is a CST816. Commands go out as 0x02 <cmd> 00, pixels as 0x32 2C 00 with the data on four lines.
// The XCA9554 expander is never touched: its pins are board power and control lines, and driving them (as the old V1 demos
// do) leaves the panel's black glowing green.

namespace {
constexpr int PANEL_W = 368, PANEL_H = 448;
constexpr int X_OFF = (PANEL_W - Lcd::W) >> 1, Y_OFF = (PANEL_H - Lcd::H) >> 1;  // the face's corner on the panel: 24, 104
constexpr int QSPI_HZ = 40000000;
constexpr int FILL_ROWS = 16;
constexpr uint8_t FT3168_ADDR = 0x38;
constexpr uint8_t CST816_ADDR = 0x15;
// With a CST816 the panel's visible area starts 16 columns into its memory (Waveshare's board package).
constexpr int CST816_X_GAP = 16;

esp_lcd_panel_io_handle_t s_io = nullptr;
// The panel takes only windows that start on an even column and row and are an even size. A copy of the face (PSRAM) lets
// a push be rounded out to even edges; the rounded window goes out through two bounce buffers in DMA memory, one filled
// while the other is sent.
constexpr int BOUNCE_BYTES = 16384;
uint16_t* s_shadow = nullptr;  // Lcd::W x Lcd::H, byte-swapped like the strips
uint16_t* s_bounce[2] = {};
int s_next = 0;
std::atomic<int> s_pending{0};  // pixel transfers queued and not yet out (counted down in the SPI interrupt)
bool s_wire = false;
int s_xGap = CST816_X_GAP;
uint8_t s_touchAddr = CST816_ADDR;

void wireStart() {
  if (s_wire) return;
  Wire.begin(IIC_SDA, IIC_SCL, 400000);
  s_wire = true;
}

bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

bool IRAM_ATTR colorDone(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
  s_pending--;
  return false;
}

void waitPixels() {
  while (s_pending > 0) taskYIELD();
}

// One panel command and its parameters. esp_lcd waits for any queued pixels first.
void sendCommand(uint8_t c, const uint8_t* data, int len) {
  esp_lcd_panel_io_tx_param(s_io, 0x02 << 24 | c << 8, data, len);
}

// The window, then the pixels, in absolute panel coordinates. Queued: `pixels` must stay untouched until waitPixels().
void sendPixels(int x, int y, int w, int h, const uint16_t* pixels) {
  x += s_xGap;
  const uint8_t ca[4] = {(uint8_t)(x >> 8), (uint8_t)x, (uint8_t)((x + w - 1) >> 8), (uint8_t)(x + w - 1)};
  const uint8_t ra[4] = {(uint8_t)(y >> 8), (uint8_t)y, (uint8_t)((y + h - 1) >> 8), (uint8_t)(y + h - 1)};
  sendCommand(0x2A, ca, 4);
  sendCommand(0x2B, ra, 4);
  s_pending++;
  if (esp_lcd_panel_io_tx_color(s_io, 0x32 << 24 | 0x2C << 8, pixels, (size_t)w * h * 2) != ESP_OK) s_pending--;
}
}  // namespace

void (*g_lcdTee)(int x, int y, int w, int h, const uint16_t* pixels) = nullptr;

void Lcd::cmd(uint8_t c, const uint8_t* data, int len) {
  wait();
  sendCommand(c, data, len);
}

void Lcd::begin(uint8_t madctl) {
  _madctl = madctl;
  // The touch chip tells the panel's offset: a CST816 = 16 columns, an FT3168 = none. If neither answers, 16 (this board).
  wireStart();
  const bool cst = i2cPresent(CST816_ADDR), ft = !cst && i2cPresent(FT3168_ADDR);
  s_touchAddr = ft ? FT3168_ADDR : CST816_ADDR;
  s_xGap = ft ? 0 : CST816_X_GAP;
  Serial.printf("lcd: touch %s, x gap %d\n", cst ? "CST816" : ft ? "FT3168" : "not found", s_xGap);
  spi_bus_config_t bus = {};
  bus.data0_io_num = LCD_SDIO0;
  bus.data1_io_num = LCD_SDIO1;
  bus.data2_io_num = LCD_SDIO2;
  bus.data3_io_num = LCD_SDIO3;
  bus.sclk_io_num = LCD_SCLK;
  bus.max_transfer_sz = BOUNCE_BYTES > PANEL_W * FILL_ROWS * 2 ? BOUNCE_BYTES : PANEL_W * FILL_ROWS * 2;
  ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));
  esp_lcd_panel_io_spi_config_t io = {};  // CO5300_PANEL_IO_QSPI_CONFIG
  io.cs_gpio_num = LCD_CS;
  io.dc_gpio_num = -1;
  io.spi_mode = 0;
  io.pclk_hz = QSPI_HZ;
  io.trans_queue_depth = 10;
  io.on_color_trans_done = colorDone;
  io.lcd_cmd_bits = 32;
  io.lcd_param_bits = 8;
  io.flags.quad_mode = 1;
  ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io, &s_io));
  s_shadow = (uint16_t*)heap_caps_calloc(Lcd::W * Lcd::H, 2, MALLOC_CAP_SPIRAM);
  for (auto& b : s_bounce) b = (uint16_t*)heap_caps_malloc(BOUNCE_BYTES, MALLOC_CAP_DMA);
  // As the board package and Espressif's CO5300 driver: software reset, orientation and pixel format, the vendor table,
  // sleep out, then the screen is cleared before display on.
  sendCommand(0x01, nullptr, 0);
  delay(80);
  static const uint8_t INIT[][2] = {{0x36, 0x00}, {0x3A, 0x55}, {0xFE, 0x00}, {0xC4, 0x80}, {0x3A, 0x55},
                                    {0x35, 0x00}, {0x53, 0x20}, {0x51, 0xFF}, {0x63, 0xFF}};
  for (const auto& c : INIT) sendCommand(c[0], c + 1, 1);
  sendCommand(0x11, nullptr, 0);
  delay(100);
  fillScreen(0);
  sendCommand(0x29, nullptr, 0);
}

void Lcd::setMadctl(uint8_t m) {
  wait();
  _madctl = m;
  cmd(0x36, &m, 1);
}

void Lcd::setBrightness(uint8_t level) {
  wait();
  cmd(0x51, &level, 1);
}

// Off: the picture is blacked out and the brightness goes to nothing. An AMOLED's black pixel is switched off, so this is as dark
// as the panel's own "display off", and it uses only what is known to work on this panel (a fill and the brightness). The
// panel's display off / on commands (0x28 / 0x29) are not used: after them the screen stayed dark.
// On: nothing to do here. The caller sets the brightness and draws the whole screen again.
void Lcd::setDisplayOn(bool on) {
  if (on) return;
  const bool was = g_lcdSparse;
  g_lcdSparse = false;
  fillScreen(0);
  g_lcdSparse = was;
  const uint8_t none = 0;
  cmd(0x51, &none, 1);
}

// Dark mode (g_lcdSparse): every second pixel of the block black, in a chequer pattern fixed to the panel. x and y are the
// block's corner on the panel.
static void sparse(uint16_t* p, int x, int y, int w, int h) {
  for (int r = 0; r < h; r++, p += w)
    for (int i = (x + y + r + 1) & 1; i < w; i += 2) p[i] = 0;
}

void Lcd::push(int x, int y, int w, int h, const uint16_t* pixels) {
  if (g_lcdTee) g_lcdTee(x, y, w, h, pixels);
  // A turned frame sends whole strips with even edges straight from the renderer's buffer (DMA memory). The shadow copy is
  // skipped: fillScreen refills it before the next normal frame, which is the only frame that needs it.
  if (g_lcdFullFrame && s_bounce[0] && !((x | y | w | h) & 1) && x >= 0 && y >= 0 && x + w <= W && y + h <= H) {
    // (dark mode: in the renderer's own strip, which it draws again from nothing before it is next sent)
    if (g_lcdSparse) sparse(const_cast<uint16_t*>(pixels), x + X_OFF, y + Y_OFF, w, h);
    sendPixels(x + X_OFF, y + Y_OFF, w, h, pixels);
    return;
  }
  if (!s_shadow || !s_bounce[0] || !s_bounce[1]) {
    sendPixels(x + X_OFF, y + Y_OFF, w, h, pixels);
    return waitPixels();
  }
  const int x0 = x < 0 ? 0 : x, y0 = y < 0 ? 0 : y, x1 = x + w > W ? W : x + w, y1 = y + h > H ? H : y + h;
  if (x1 <= x0 || y1 <= y0) return;
  for (int r = y0; r < y1; r++)
    memcpy(s_shadow + r * W + x0, pixels + (r - y) * w + (x0 - x), (size_t)(x1 - x0) * 2);
  // Rounded out to even edges (W and H are even, and so are the panel offsets).
  const int ax0 = x0 & ~1, ay0 = y0 & ~1, ax1 = (x1 + 1) & ~1, ay1 = (y1 + 1) & ~1, aw = ax1 - ax0;
  const int band = (BOUNCE_BYTES / (aw * 2)) & ~1;
  for (int by = ay0; by < ay1; by += band) {
    const int bh = by + band < ay1 ? band : ay1 - by;
    // The buffer filled here was sent two transfers ago; sendPixels' window commands waited for the one after it, so
    // only the other buffer can still be going out.
    uint16_t* b = s_bounce[s_next];
    s_next ^= 1;
    for (int r = 0; r < bh; r++) memcpy(b + r * aw, s_shadow + (by + r) * W + ax0, (size_t)aw * 2);
    if (g_lcdSparse) sparse(b, ax0 + X_OFF, by + Y_OFF, aw, bh);  // (the copy of the face keeps every pixel)
    sendPixels(ax0 + X_OFF, by + Y_OFF, aw, bh, b);
  }
}

// The bands above and below the face: whole panel rows, from the same visible left edge as push() (x 0 there too).
void Lcd::pushRows(int y, int h, const uint16_t* pixels) {
  if (g_lcdSparse) sparse(const_cast<uint16_t*>(pixels), 0, y, PANEL_W, h);  // (the band's strip is drawn again each time)
  sendPixels(0, y, PANEL_W, h, pixels);
}

void Lcd::wait() { waitPixels(); }

void Lcd::fillScreen(uint16_t c) {
  uint16_t* rows = (uint16_t*)heap_caps_malloc(PANEL_W * FILL_ROWS * 2, MALLOC_CAP_DMA);
  if (!rows) return;
  const uint16_t s = (uint16_t)(c >> 8 | c << 8);
  wait();
  if (s_shadow)
    for (int i = 0; i < W * H; i++) s_shadow[i] = s;
  for (int i = 0; i < PANEL_W * FILL_ROWS; i++) rows[i] = s;
  if (g_lcdSparse) sparse(rows, 0, 0, PANEL_W, FILL_ROWS);  // (FILL_ROWS is even: the pattern is the same in every block)
  for (int y = 0; y < PANEL_H; y += FILL_ROWS)
    sendPixels(0, y, PANEL_W, FILL_ROWS < PANEL_H - y ? FILL_ROWS : PANEL_H - y, rows);
  waitPixels();
  free(rows);
}

//==============================
// touch: FT3168 or CST816 on I2C (the same register layout)
//==============================
void TouchPanel::begin() { wireStart(); }

static bool touchRead(uint8_t reg, uint8_t* out, int len) {
  Wire.beginTransmission(s_touchAddr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)s_touchAddr, len) != len) return false;
  for (int i = 0; i < len; i++) out[i] = (uint8_t)Wire.read();
  return true;
}

// The glass is bigger than the face: a touch in the margins counts at the nearest edge of the face.
bool TouchPanel::read(int& x, int& y, uint8_t madctl) {
  (void)madctl;  // the panel's coordinates are the same as the drawing's
  uint8_t fingers = 0;
  if (!touchRead(0x02, &fingers, 1) || fingers == 0) return false;
  uint8_t d[4];
  if (!touchRead(0x03, d, 4)) return false;
  x = constrain((((d[0] & 0x0F) << 8) | d[1]) - X_OFF, 0, Lcd::W - 1);
  y = constrain((((d[2] & 0x0F) << 8) | d[3]) - Y_OFF, 0, Lcd::H - 1);
  return true;
}

// The platform hooks of core/platform.h, for the board: pixel buffers in DMA-capable memory, and the microsecond clock.
uint16_t* tvAllocPixels(size_t bytes) {
  return (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
}
uint32_t tvMicros() { return (uint32_t)micros(); }

// The other core for the turned face's rows (platform.h): a worker task on core 0 that waits for a job, does it, and says so.
// The brain and the web server share core 0 too, so the worker is short-lived per strip and sleeps in between.
namespace {
SemaphoreHandle_t s_bgGo = nullptr, s_bgDone = nullptr;
void (*s_bgFn)(void*) = nullptr;
void* s_bgArg = nullptr;
bool s_bgBusy = false;
void bgLoop(void*) {
  for (;;) {
    xSemaphoreTake(s_bgGo, portMAX_DELAY);
    s_bgFn(s_bgArg);
    xSemaphoreGive(s_bgDone);
  }
}
}  // namespace

void tvBackground(void (*fn)(void*), void* arg) {
  if (!s_bgGo) {
    s_bgGo = xSemaphoreCreateBinary();
    s_bgDone = xSemaphoreCreateBinary();
    if (!s_bgGo || !s_bgDone || xTaskCreatePinnedToCore(bgLoop, "warp", 6144, nullptr, 2, nullptr, 0) != pdPASS) {
      fn(arg);  // no worker: this core does it all
      return;
    }
  }
  s_bgFn = fn;
  s_bgArg = arg;
  s_bgBusy = true;
  xSemaphoreGive(s_bgGo);
}
void tvBackgroundWait() {
  if (!s_bgBusy) return;
  xSemaphoreTake(s_bgDone, portMAX_DELAY);
  s_bgBusy = false;
}
#endif
