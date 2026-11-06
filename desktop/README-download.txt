TeeVee for the desktop
======================

A little robot face that lives on your computer. It reacts to you: click it (poke), drag it (pet), and it has moods,
gets sleepy, and learns what you like. The window shows the face beside its controls, and can also shrink to a small
picture-in-picture window on top of your other windows.

Start it
  Windows:  double-click teevee-qt.exe in this folder
            (Windows may warn about an unknown program: "More info" -> "Run anyway".)
  macOS:    in Terminal, in this folder:   xattr -dr com.apple.quarantine teevee-qt.app ; open teevee-qt.app
            (the program is not signed by Apple, so the first start needs that line.)
  Linux:    ./teevee-qt   (needs Qt 6 installed: Arch: qt6-base, Ubuntu: qt6-base-dev)

Mouse
  click: poke      drag: pet      hold still: gentle pet      hover: the eyes follow

Picture in picture
  F3: open or close it
  right-drag: move      wheel: size      right-click: menu
  the corner button or Esc: back to the full window
  Start in it with --pip.

Keys (click the face first)
  H or F1: this list           F2: show or hide the controls
  I: its mind                  Space: sleep / wake            Esc: quit
  P pet   O poke   V praise   S scold   D startle
  W win   F fail   G greet    Z tired   N annoy
  L love  C confuse   B bore
  1-9: hold a face     , .: previous / next      Backspace: back to the mood
  E / Shift+E: next / previous emote
  Tab / Shift+Tab: next / previous look
  R: change a part of the look     Shift+R: all of it
  Q: next personality
  T: always on top     F11 or Alt+Enter: fullscreen

Options (teevee-qt --help)
  --pip            start in the picture-in-picture window
  --light          the light theme for this run (the Light button changes it and remembers)
  --tray           an icon in the system tray: closing the window keeps it running there
  --scale N        size of the face as a multiple of 320x240 (default 2)
  --fullscreen     start fullscreen          --top        keep the window on top
  --nearest        sharp pixels instead of smooth scaling
  --no-follow      eyes do not follow the mouse    --no-wake    start awake
  --fisheye N      a fisheye lens on the picture, 0..1 (0 = off)
  --head-turn N    the head turns like a box up to N degrees (0 = off)
  --reset          forget the saved look, personality and learning
  --tab NAME       open this tab: look, play, mind, keys

It remembers its look, personality and what it has learned between starts.
