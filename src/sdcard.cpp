#include "sdcard.h"
#include <SPI.h>
#if BOARD_HAS_SDMMC
#include <SD_MMC.h>
#define CARD SD_MMC
#else
#include <SD.h>
#define CARD SD
#endif
#include "board.h"

namespace sdcard {
namespace {
File s_shot;
#if BOARD_HAS_SD
constexpr int SD_CS = BOARD_SD_CS, SD_SCK = 18, SD_MISO = 19, SD_MOSI = 23;
SPIClass spi(VSPI);
bool s_spiStarted = false;
#endif
bool s_ok = false;
}  // namespace

bool begin() {
#if BOARD_HAS_SDMMC
  if (s_ok) CARD.end();
  s_ok = false;
  // 1-bit mode, as the Waveshare board package uses it; the card's own clock is the default.
  CARD.setPins(BOARD_SD_CLK, BOARD_SD_CMD, BOARD_SD_D0);
  for (int hz : {20000000, 10000000, 4000000}) {  // the slower speeds, if a card does not take the fast one
    if (CARD.begin("/sdcard", true, false, hz) && CARD.cardType() != CARD_NONE) {
      s_ok = true;
      break;
    }
    CARD.end();
  }
  return s_ok;
#elif BOARD_HAS_SD
  if (s_ok) SD.end();
  s_ok = false;
  if (!s_spiStarted) {
    spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    s_spiStarted = true;
  }
  for (uint32_t hz : {20000000u, 10000000u, 4000000u}) {
    if (SD.begin(SD_CS, spi, hz)) {
      s_ok = SD.cardType() != CARD_NONE;
      if (s_ok) break;
      SD.end();
    }
  }
  return s_ok;
#else
  return false;
#endif
}

bool mounted() { return s_ok; }
fs::FS& fs() { return CARD; }
uint64_t totalBytes() { return s_ok ? CARD.totalBytes() : 0; }
uint64_t usedBytes() { return s_ok ? CARD.usedBytes() : 0; }
const char* typeName() {
  if (!s_ok) return "none";
  switch (CARD.cardType()) {
    case CARD_MMC: return "MMC";
    case CARD_SD: return "SDSC";
    case CARD_SDHC: return "SDHC";
    default: return "unknown";
  }
}

bool shotBegin(uint16_t background) {
  if (!mounted()) return false;
  if (!fs().exists("/teevee")) fs().mkdir("/teevee");
  if (!fs().exists("/teevee/shots")) fs().mkdir("/teevee/shots");
  char path[40];
  int n = 0;
  do snprintf(path, sizeof path, "/teevee/shots/shot%03d.ba", n);
  while (fs().exists(path) && ++n < 1000);
  if (n >= 1000) return false;
  s_shot = fs().open(path, FILE_WRITE);
  if (!s_shot) return false;
  const uint8_t head[8] = {'b', 'A', 'b', '2', 320 & 255, 320 >> 8, 240, 0};
  bool ok = s_shot.write(head, sizeof head) == sizeof head;
  const uint8_t hi = (uint8_t)(background >> 8), lo = (uint8_t)background;
  uint8_t row[640];
  for (int i = 0; i < 640; i += 2) row[i] = hi, row[i + 1] = lo;
  for (int y = 0; ok && y < 240; y++) ok = s_shot.write(row, sizeof row) == sizeof row;
  if (!ok) s_shot.close();
  return ok;
}

void shotPush(int x, int y, int w, int h, const uint16_t* pixels) {
  if (!s_shot) return;
  for (int r = 0; r < h; r++) {
    s_shot.seek(8 + ((size_t)(y + r) * 320 + x) * 2);
    s_shot.write((const uint8_t*)(pixels + r * w),
                 (size_t)w * 2);  // the strips are byte-swapped: high byte first, as b2 wants
  }
}

void shotEnd() {
  if (s_shot) s_shot.close();
}
}  // namespace sdcard
