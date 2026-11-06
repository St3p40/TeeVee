# Cross-compiling the Windows teevee-qt.exe on Linux with MinGW-w64 (Arch: pacman -S mingw-w64-gcc cmake; Debian/Ubuntu: apt install mingw-w64 cmake).
# NOT WORKING YET: this needs Qt 6 built for MinGW (not installed here; Arch's qt6 packages are not for MinGW), so configuring fails with "Qt 6 not found".
#   cmake -S desktop -B build-win -DCMAKE_TOOLCHAIN_FILE=desktop/mingw-w64.cmake -DTEEVEE_FETCH_SDL3=ON -DQt6_DIR=<MinGW Qt 6 lib/cmake/Qt6>
#   cmake --build build-win --parallel          -> build-win/teevee-qt.exe (SDL is inside; Qt DLLs must sit beside it)
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)
set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
