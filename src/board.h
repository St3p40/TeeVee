#pragma once
// The board Teevee runs on: pins and which parts it has. One section per board, picked by the build (platformio.ini):
// -DTEEVEE_BOARD_LILKA = Lilka v2 (ESP32-S3 handheld); nothing = the CYD (ESP32-2432S028R). Board-only code (src/*.cpp) reads these;
// src/core never does.
#include <stdint.h>

#if defined(TEEVEE_BOARD_AMOLED)
// Waveshare ESP32-S3-Touch-AMOLED-1.8 (V2): ESP32-S3 (16 MB flash, 8 MB PSRAM), a CO5300 AMOLED of 368x448 on QSPI (the face is
// drawn 320x240 in its middle), a CST816 or FT3168 touch and a QMI8658 motion sensor on I2C. Pins from the Waveshare demo's pin_config.h.
#define BOARD_NAME "Waveshare AMOLED 1.8"
constexpr int LCD_CS = 12, LCD_SCLK = 11, LCD_SDIO0 = 4, LCD_SDIO1 = 5, LCD_SDIO2 = 6, LCD_SDIO3 = 7;
constexpr int LCD_PANEL_W = 320, LCD_CROP = 0, LCD_X_OFFSET = 0, LCD_Y_OFFSET = 0;
constexpr uint8_t LCD_MADCTL = 0x00, LCD_MADCTL_FLIPPED = 0x00;  // the demo leaves the panel's default orientation
constexpr int IIC_SDA = 15, IIC_SCL = 14, TP_INT = 21;
constexpr int BOARD_POWER = -1;
constexpr int BOARD_SD_CS = -1;  // the card (SDMMC) is not used yet
// The speaker amplifier's enable (IO46) is held low from power-on; audio.cpp turns it on only while the voice speaks.
constexpr int BOARD_QUIET_PINS[] = {46};
constexpr int BOARD_BOOT_PINS[] = {0};
#define BOARD_HAS_TOUCH 1
#define BOARD_HAS_BUTTONS 0
#define BOARD_HAS_SD 0
// The microSD slot is on the SDMMC bus, 1-bit (Waveshare's pin_config.h); no card-detect or write-protect pin.
#define BOARD_HAS_SDMMC 1
constexpr int BOARD_SD_CLK = 2, BOARD_SD_CMD = 1, BOARD_SD_D0 = 3;
// The microphone and the speaker: an ES8311 codec (I2C 0x18 on the touch bus) on I2S, and an amplifier.
#define BOARD_HAS_AUDIO 1
constexpr int AUDIO_MCLK = 16, AUDIO_BCLK = 9, AUDIO_WS = 45, AUDIO_DIN = 10, AUDIO_DOUT = 8, AUDIO_AMP_EN = 46;
constexpr uint8_t AUDIO_CODEC_ADDR = 0x18;
#define BOARD_IMU_SDA 15  // a QMI8658 on the same I2C bus as the touch (IIC_SDA / IIC_SCL)
#define BOARD_IMU_SCL 14
#define BOARD_IMU_BUILTIN 1  // motion is switched on once by default (the setting `motion`)
// The QMI8658 is mounted turned: its x points up the screen and its z into it (measured: flat = z -1 g, upright = x +1 g).
#define BOARD_IMU_MAP {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}}

#elif defined(TEEVEE_BOARD_LILKA)
// Lilka v2: ESP32-S3 (16 MB flash, 8 MB PSRAM), ST7789 1.69" 280x240 IPS, buttons, no touch. Pins from the Lilka SDK
// (github.com/lilka-dev/sdk, lib/lilka/src/lilka/config.h).
#define BOARD_NAME "Lilka v2"
constexpr int LCD_SCLK = 18, LCD_MOSI = 17, LCD_MISO = 8, LCD_CS = 7, LCD_DC = 15, LCD_BL = -1;
constexpr int LCD_HZ = 80000000, LCD_SPI_MODE = 3;
constexpr bool LCD_ST7789 = true;
constexpr int LCD_PANEL_W = 280, LCD_CROP = 20, LCD_X_OFFSET = 20, LCD_Y_OFFSET = 0;
constexpr uint8_t LCD_MADCTL = 0xA0, LCD_MADCTL_FLIPPED = 0x60;
constexpr int BOARD_POWER = 46;
constexpr int BOARD_SD_CS = 16;
// Sound: a buzzer and an I2S amplifier. Teevee makes no sound here: the pins are driven low at power-on and never touched again.
constexpr int BOARD_QUIET_PINS[] = {11, 42, 2, 1};
constexpr int BTN_UP = 38, BTN_DOWN = 41, BTN_LEFT = 39, BTN_RIGHT = 40, BTN_A = 5, BTN_B = 6, BTN_C = 10,
              BTN_D = 9, BTN_START = 4;
constexpr int BOARD_BOOT_PINS[] = {0, BTN_START};
#define BOARD_HAS_TOUCH 0
#define BOARD_HAS_BUTTONS 1
#define BOARD_HAS_SD 0
#define BOARD_IMU_SDA -1
#define BOARD_IMU_SCL -1

#else
#define BOARD_NAME "CYD"
constexpr int LCD_SCLK = 14, LCD_MOSI = 13, LCD_MISO = 12, LCD_CS = 15, LCD_DC = 2, LCD_BL = 21;
constexpr int LCD_HZ = 40000000, LCD_SPI_MODE = 0;
constexpr bool LCD_ST7789 = false;
constexpr int LCD_PANEL_W = 320, LCD_CROP = 0, LCD_X_OFFSET = 0, LCD_Y_OFFSET = 0;
constexpr uint8_t LCD_MADCTL = 0xE8, LCD_MADCTL_FLIPPED = 0x28;
constexpr int BOARD_POWER = -1;
constexpr int BOARD_SD_CS = 5;
constexpr int BOARD_QUIET_PINS[] = {-1};
constexpr int BOARD_BOOT_PINS[] = {0};
#define BOARD_HAS_TOUCH 1
#define BOARD_HAS_BUTTONS 0
#define BOARD_HAS_SD 1
#define BOARD_IMU_SDA 27
#define BOARD_IMU_SCL 22
#endif

#ifndef BOARD_IMU_BUILTIN
#define BOARD_IMU_BUILTIN 0
#endif
#ifndef BOARD_HAS_AUDIO
#define BOARD_HAS_AUDIO 0
#endif
#ifndef BOARD_HAS_SDMMC
#define BOARD_HAS_SDMMC 0
#endif
