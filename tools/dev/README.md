# Development helpers

Small tools used while building Teevee, kept so the next session (or model) does not have to rewrite them. None of them is needed to build or run the robot.
They were written in a scratch directory: adjust paths in the commands below to where you put the build outputs. All of them are read-only with respect to the project.

## Looking at the faces and apps on the PC (the real renderer, no board)
Build from `desktop/` (needs SDL3 headers only for the shim-free ones below; these do not open a window):
    g++ -O1 -std=c++17 -I../src/core -I. ../tools/dev/faces.cpp platform_lcd.cpp ../src/core/renderer.cpp ../src/core/render_common.cpp ../src/core/render_fx.cpp ../src/core/render_widgets.cpp ../src/core/font_data.cpp -o faces
- `faces OUT.ppm "style options" tMs curve open`: renders one face (a look and a mouth pose) to a PPM; `ImageMagick` (`magick montage ...`) makes contact sheets.
- `emo.cpp`: the same for an emote (`emo OUT.ppm "style" emote tMs`). `glscan.cpp`: counts glitch bursts and saves frames (`DRIVE=0.5 glscan DIR "glitch 3"`).
- `alive_probe.cpp`: scores whether the robot behaves alive or scripted, in simulated scenes (`alive_control.py` makes a scripted copy to check the probe itself); how to use them: the `teevee-alive` skill.
- `bench.cpp`: renders many frames and prints the cost per frame (a quick performance check of a look, e.g. the wave mouth).
- `apppreview.cpp`: plays a Lua app with the real renderer and the mini face and saves frames (needs the Lua objects from `make -C test app_test`, see its compile line in the file comment).
- `tuitest.py` tested the terminal mode of the old SDL program (`teevee --tui`), which is no longer in the repository.

## Looking at the web pages (headless Firefox)
- `marionette.py` + `shoot.py`: a minimal Marionette client (no Selenium needed): `python3 shoot.py <dir-with-marionette.py> URL WIDTH HEIGHT out.png [javascript]`, or import `shoot.shoot([...])`
  for several screenshots in one browser. `javascript` runs before the screenshot (click a tab, press a key: dispatch events with `bubbles:true`).
- `mock_device.py` + `mock_dump.cpp`: the board's web portal without a board. `mock_dump` writes the firmware's own JSON (settings, style, mind net, diary) with the host headers; `mock_device.py PORT`
  serves `src/portal_page.h` and fake `/api` answers from those files. Used for the design checks of the portal.
- The browser version is served from `web/dist` (`python3 -m http.server 8090 --directory web/dist`).

## Talking to the real board
- `ser.py "cmd1" "cmd2"`: sends console commands over `/dev/ttyUSB0` and prints the replies. **Opening the port resets the board**: wait ~8 s before using the network.
- `tools/regress.py --host <ip> [--fps]` is the real regression (31 checks).
