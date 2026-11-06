#pragma once
#include <stdint.h>

class ScreenIdle {
  public:
    void activity(uint32_t now) {
      _last = now;
      _off = false;
    }
    bool wake(uint32_t now) {
      const bool wasOff = _off;
      activity(now);
      return wasOff;
    }
    // Call often. minutes 0: never dark. keepOn: something on the screen has to be seen (a PIN, the setup details, an app, a timer); the wait starts again after it.
    bool update(uint32_t now, uint32_t minutes, bool keepOn) {
      if (keepOn) activity(now);
      if (!minutes)
        _off = false;
      else if (!_off && now - _last >= minutes * 60000u)
        _off = true;
      return _off;
    }
    bool off() const { return _off; }
    void sleepNow() { _off = true; }  // the console's `screen off`; any activity wakes it again

  private:
    uint32_t _last = 0;
    bool _off = false;
};
