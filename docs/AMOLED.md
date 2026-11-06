# The AMOLED screen (Waveshare ESP32-S3-Touch-AMOLED-1.8, V2)

This is the screen, touch, motion and sound side of the AMOLED build (`pio run -e amoled`). The face itself is described in
[FACE.md](FACE.md).

## The panel

- **Size:** 368 x 448 pixels. The face is 320 x 240 and sits in the middle, so there is a 24 px margin on each side and a
  104 px margin top and bottom. The face's corner on the panel is `(24, 104)`.
- **Controller:** CO5300, on QSPI with the hardware chip-select turned off (`src/lcd_amoled.cpp`).
- **Board revision:** the board is a **V2**. The touch chip answers at 0x15 (CST816), and that is how the driver knows.
  A V1 image (FT3168 touch at 0x38) showed no picture on this board; the V2 image does.
- **Pins** (`src/board.h`): QSPI CS 12, CLK 11, data 4/5/6/7; I2C SDA 15, SCL 14; touch interrupt 21.

## How pixels get to the panel

Source: `src/lcd_amoled.cpp`. It follows Waveshare's own board package (`esp32_s3_touch_amoled_1_8` 2.0.3, the one their
V2 factory firmware uses).

- **Transport:** ESP-IDF's `esp_lcd` SPI panel IO in quad mode. Commands go out as opcode `0x02` with the command byte
  in the address field; pixels as opcode `0x32`, address `0x002C00`, data on four lines.
- **Window:** every update sets columns (`0x2A`) and rows (`0x2B`), then sends the pixels. Windows must start on an even
  column and row and have even sizes, so partial updates are rounded out to even edges, using a copy of the screen in
  PSRAM (`s_shadow`).
- **The 16-column offset:** with a CST816 touch the visible area starts 16 columns into the panel's memory
  (`CST816_X_GAP`). Without it there is a stale green line on the right edge.
- **Start-up** (`Lcd::begin`): software reset, then `0x36 = 00` (orientation), `0x3A = 55` (16-bit colour), then the
  vendor table (`0xFE 00`, `0xC4 80`, `0x35 00`, `0x53 20`, `0x51 FF`, `0x63 FF`), sleep out, clear, display on.
- **Sunlight mode:** not written, so the chip keeps its default. Our earlier table left it on, and black looked green.
- **The expander is never touched.** The XCA9554 at 0x20 drives the display's power and reset on the old V1 demos. On
  this V2 board, driving it made the black glow green. Nothing in the V2 firmware touches it.

### Colour

Pixels are stored byte-swapped (big-endian RGB565), like the rest of the engine. The colour bars of a test firmware show
the path is right for white, yellow, cyan, green, magenta and red. Two colours still look wrong on the camera: **blue
shows as violet and black as brown** in the bars, and **the face's pink shows as green** on the panel. The renderer's
pixel data for the face decodes to pink, so the open question is on the panel side. Not solved yet.

## Speed

Measured on the board (`s` prints it):

| What | Flat face | Turned (3D) face (per turned frame) |
|---|---|---|
| Frame | about 18.6 ms (about 54 fps, current look) | about 48 ms (about 21 fps, current look) |
| Sending to the panel | part of the above | about 5.7 ms |
| Drawing the turned face's texture | – | about 18 ms (the glow and the eyes and mouth) |
| Warping the rows | – | about 24 ms (the main cost) |
| Pixel pass after the warp | – | under 0.1 ms |

Measured with `s` on the owner's current look (glow 0.6, pixel 3); `warp us/turned frame` counts only turned frames.
- The turned frame sends whole strips straight from the drawing buffer, with no shadow copy (`g_lcdFullFrame`).
- The glow behind each eye and the mouth is kept between frames while the colour and the background do not change
  (it cost about 3.5 ms a frame; now about 1.6 ms, the copy from PSRAM).
- Pixelation is done once, in the warp (the pass after it is skipped for turned strips). The texture's glow reads
  its column and row terms once each: the ESP32 has no hardware divide, so a divide per texel was the slow part.
  Both changes give the same pixels (the golden frames are identical).
- Running the warp code from internal RAM gained about 2 ms, so the flash cache is not the limit. Reverted.

## Touch

`TouchPanel` in `lcd_amoled.cpp` reads the CST816 (or FT3168) over I2C. A touch in the black margins counts at the
nearest edge of the face. The glass is larger than the face, so coordinates are clamped to the face.

## Motion (QMI8658)

The board has a QMI8658 accelerometer and gyro on the same I2C bus (`src/imu.cpp`, enabled once by default through
`BOARD_IMU_BUILTIN`). Its axes are mapped to the board's mounting in `board.h` (`BOARD_IMU_MAP`). Measured: flat reads
face up, standing upright reads upright.

Reactions (`src/core/motion.h`, `brain.cpp` `reactMotion`):

- **knock, shake, pick up, put down, fall** and the **face down / upside down / face up / upright** changes, as on
  the other boards;
- **rock** (to and fro, gently, four swings): soothed, then sleepy, then dozes off;
- **spin** (on the table): dizzy;
- **eyes**: tilt is the change from the resting pose (they lean while it moves and settle back when it is still); flat,
  they roll downhill;
- **level** (`cfg level`): standing and turned, the face counter-rotates to stay level (4° slack).

Motion and picking it up wake the screen (`screenIdle.wake`).

## Sound

- **Codec:** ES8311 at I2C 0x18, on I2S (MCLK 16, BCLK 9, WS 45, DIN 10, DOUT 8). The speaker amplifier is enable pin 46,
  on only while the voice speaks.
- **Microphone** (`src/audio.cpp`, `src/core/hearing.h`): loudness only, nothing recorded. It reports a sudden loud sound
  (startle, and it wakes the screen) and voice that keeps going (someone near). Steady noise becomes the new floor.
- **Voice** (`src/core/voice.h`): a synthesized chirp per event, coloured by mood. The microphone is deaf while it speaks
  and for 0.8 s after, so it never hears itself.
- **Settings:** `sound`, `volume`, `hearing`, `hearing_sens`. Console: `say <event>`, `audio`.

## Screen timer and wake

- `screen_off` (minutes idle before the screen goes dark; default 10) runs in `ScreenIdle` (`src/core/screen_idle.h`).
- Wake: a touch, the BOOT button, sound, motion, or any console command except `screen off`.
- `screen` shows the state; `screen off` and `screen on` force it (for testing).
- **Not yet confirmed:** that the panel lights again on every wake path on the device. See "Known issues".

## SD card

The microSD slot is on the SDMMC bus, 1-bit: CLK 2, CMD 1, DATA0 3 (from Waveshare's board package). The card mounts
at `/sdcard` underneath the library, so Teevee's `/teevee/...` paths are the same on every board. Details in `src/sdcard.cpp`.

## Build, flash and backup

- `pio run -e amoled -t upload` (the board is on `/dev/ttyACM0`; the Lilka uses the same port, so check which board is there).
- The full 16 MB flash of this board, from before the first TeeVee flash, is in `builds/amoled-backup/`. It holds the
  board's Wi-Fi settings, so keep it out of anything published.

## Known issues

1. **Blue reads violet, black reads brown, and the face's pink reads green** on the panel. The renderer is right; the cause
   is on the panel side and still open.
2. **The PIN screen fix** (the face goes flat while the PIN shows) is flashed but not yet checked by eye.
3. **Screen wake** on every path (touch, button, motion, sound, console) is not yet confirmed on the device.
4. **Turned face** runs at about 21 fps with the current look. Faster needs a smaller texture or coarser sampling, which changes the look.
5. **Touch** works; **sound** plays and the mic doesn't hear the voice, but the quality of both is not yet judged.

## Where the code is

| What | File |
|---|---|
| Panel driver | `src/lcd_amoled.cpp` |
| Pins and board flags | `src/board.h` |
| Motion sensor driver | `src/imu.cpp`, `src/imu.h` |
| Motion reactions | `src/core/motion.h`, `src/core/motion_sim.h` |
| Sound | `src/audio.cpp`, `src/audio.h`, `src/core/hearing.h`, `src/core/voice.h` |
| SD card | `src/sdcard.cpp`, `src/sdcard.h` |
| Screen timer | `src/core/screen_idle.h` |
| Tests | `test/motion_test.cpp`, `test/hearing_test.cpp`, `test/voice_test.cpp`, `test/settings_test.cpp` |
