#pragma once
// The desktop pet's body (--pet): where the window is on the screen. It sits on the floor (the bottom of the usable screen area,
// above a taskbar) or on a ledge (the top edge of another window), now and then walks to another spot, falls when it is let go in
// the air, flies when it is thrown, and is carried while the owner drags it.
// Platform-free: main.cpp feeds it the area and the robot's state and moves the window to x(), y(). Host test: test/pet_test.cpp.
#include <math.h>
#include <stdint.h>

struct PetLedge {
    float x0, x1, y;
};

struct PetArea {
    float left, right;
    float floor;
    float top = -1e9f;
    const PetLedge* ledges = nullptr;
    int nLedges = 0;
};

class PetWalker {
  public:
    enum State { SIT, WALK, FALL, HELD };

    void place(float x, float y) { _x = x, _y = y; }
    void grab() { _state = HELD; }
    // Let go (from where it was carried), with the drag's speed in px/s: it flies, bounces off the screen's edges and lands.
    void release(float vx = 0, float vy = 0) {
      _state = FALL;
      _vx = vx < -MAX_V ? -MAX_V : vx > MAX_V ? MAX_V : vx;
      _vy = vy < -MAX_V ? -MAX_V : vy > MAX_V ? MAX_V : vy;
    }
    void setWalking(bool on) { _walkOn = on; }
    bool walking() const { return _walkOn; }
    bool goTo(float x, const PetArea& a) {
      if (_state != SIT) return false;
      const float right = a.right > a.left ? a.right : a.left;
      x = x < a.left ? a.left : x > right ? right : x;
      if (fabsf(x - _x) < STRIDE) return false;
      startWalk(x);
      _rush = true;
      return true;
    }

    float step(float dt, const PetArea& a, bool awake, float lively) {
      if (dt > 0.1f) dt = 0.1f;
      if (lively < 0) lively = 0;
      if (lively > 1) lively = 1;
      const float right = a.right > a.left ? a.right : a.left;
      if (_state == HELD) return 0;
      if (_x < a.left) _x = a.left;
      if (_x > right) _x = right;
      if (_y > a.floor) _y = a.floor;
      if (_state != FALL && _y < groundAt(a, _x, _y) - 0.5f) _state = FALL, _vx = 0, _vy = 0;
      float landed = 0;
      switch (_state) {
        case FALL: {
          const float wasY = _y;
          _vy += GRAVITY * dt;
          _vx *= expf(-0.6f * dt);
          _x += _vx * dt;
          _y += _vy * dt;
          if (_x < a.left) _x = a.left, _vx = -_vx * BOUNCE_WALL;
          if (_x > right) _x = right, _vx = -_vx * BOUNCE_WALL;
          if (_y < a.top && _vy < 0) _y = a.top, _vy = -_vy * BOUNCE_WALL;
          const float g = groundAt(a, _x, wasY);
          if (_vy > 0 && _y >= g) {
            _y = g;
            landed = _vy > 1 ? _vy : 1;
            if (_vy > BOUNCE_MIN) {
              _vy = -_vy * BOUNCE_FLOOR;
            } else {
              _vx = 0;
              sit(3.f);
            }
          }
          break;
        }
        case SIT:
          _timer -= dt;
          if (_timer <= 0) {
            if (!_walkOn || !awake || right - a.left < MIN_TRIP) {
              sit(2.f);
              break;
            }
            float lo = a.left, hi = right;
            if (const PetLedge* l = ledgeUnder(a, _x, _y))
              if (l->x1 - l->x0 >= 2 * MIN_TRIP && rnd() < 0.75f)
                lo = l->x0 > a.left ? l->x0 : a.left, hi = l->x1 < right ? l->x1 : right;
            float t = lo + rnd() * (hi - lo);
            if (t > _x - MIN_TRIP && t < _x + MIN_TRIP)
              t = (_x - lo > hi - _x) ? _x - MIN_TRIP - rnd() * (_x - MIN_TRIP - lo)
                                      : _x + MIN_TRIP + rnd() * (hi - _x - MIN_TRIP);
            if (t < a.left) t = a.left;
            if (t > right) t = right;
            if (fabsf(t - _x) < MIN_TRIP) {
              sit(2.f);
              break;
            }
            startWalk(t);
            _rush = false;
          }
          break;
        case WALK: {
          if ((!_walkOn && !_rush) || !awake) {
            sit(4.f);
            break;
          }
          const float v = (40.f + 60.f * lively) * (_rush ? 2.f : 1.f);
          _x += _dir * v * dt;
          _phase += v * dt / STRIDE;
          _hopH = 3.f + 5.f * lively;
          if ((_dir > 0 && _x >= _target) || (_dir < 0 && _x <= _target)) {
            _x = _target;
            arrived = _rush;
            sit(_rush ? 6.f : (5.f + 10.f * rnd()) * (1.5f - lively));
            _rush = false;
          }
          break;
        }
        case HELD: break;
      }
      return landed;
    }

    float x() const { return _x; }
    float y() const { return _y; }
    State state() const { return _state; }
    float vx() const { return _state == FALL ? _vx : 0; }
    float vy() const { return _state == FALL ? _vy : 0; }
    int dir() const { return _state == WALK ? _dir : 0; }
    bool rushing() const { return _state == WALK && _rush; }
    bool arrived = false;
    float hop() const {
      if (_state != WALK) return 0;
      const float f = _phase - (int)_phase;
      return _hopH * 4.f * f * (1.f - f);
    }
    static float groundAt(const PetArea& a, float x, float y) {
      float g = a.floor;
      for (int i = 0; i < a.nLedges; i++) {
        const PetLedge& l = a.ledges[i];
        if (x >= l.x0 && x <= l.x1 && l.y >= y - 0.5f && l.y < g) g = l.y;
      }
      return g;
    }

    static constexpr float GRAVITY = 2400.f;
    static constexpr float MIN_TRIP = 80.f;
    static constexpr float STRIDE = 20.f;
    static constexpr float MAX_V = 3000.f;
    static constexpr float BOUNCE_MIN = 700.f, BOUNCE_FLOOR = 0.3f, BOUNCE_WALL = 0.6f;

  private:
    static const PetLedge* ledgeUnder(const PetArea& a, float x, float y) {
      for (int i = 0; i < a.nLedges; i++)
        if (x >= a.ledges[i].x0 && x <= a.ledges[i].x1 && fabsf(a.ledges[i].y - y) < 0.5f)
          return &a.ledges[i];
      return nullptr;
    }
    void startWalk(float t) {
      _dir = t > _x ? 1 : -1;
      _target = _x + _dir * STRIDE * (int)((t - _x) * _dir / STRIDE);
      _state = WALK;
    }
    void sit(float s) { _state = SIT, _timer = s, _phase = 0; }
    float rnd() {
      _seed ^= _seed << 13, _seed ^= _seed >> 17, _seed ^= _seed << 5;
      return (_seed >> 8) / 16777216.f;
    }
    State _state = FALL;
    float _x = 0, _y = 0, _vx = 0, _vy = 0, _timer = 0, _target = 0, _phase = 0, _hopH = 0;
    int _dir = 1;
    bool _walkOn = true, _rush = false;
    uint32_t _seed = 0x9e3779b9u;
};

struct PetRect {
    float x, y, w, h;
};
inline int petLedges(const PetRect* z, int n, float w, float h, float under, float minY, PetLedge* out,
                     int max) {
  int k = 0;
  for (int i = 0; i < n && k < max; i++) {
    const PetRect& r = z[i];
    if (r.w < 60 || r.h < 30) continue;
    const float top = r.y, standY = top - h + under;
    if (standY < minY) continue;
    float seg[16][2];
    int ns = 1;
    seg[0][0] = r.x, seg[0][1] = r.x + r.w;
    for (int j = 0; j < i && ns > 0; j++) {
      const PetRect& c = z[j];
      if (!(c.y <= top && top < c.y + c.h)) continue;
      const float c0 = c.x, c1 = c.x + c.w;
      float next[16][2];
      int nn = 0;
      for (int s = 0; s < ns; s++) {
        const float a = seg[s][0], b = seg[s][1];
        if (c1 <= a || c0 >= b) {
          if (nn < 16) next[nn][0] = a, next[nn][1] = b, nn++;
          continue;
        }
        if (c0 > a && nn < 16) next[nn][0] = a, next[nn][1] = c0, nn++;
        if (c1 < b && nn < 16) next[nn][0] = c1, next[nn][1] = b, nn++;
      }
      for (int s = 0; s < nn; s++) seg[s][0] = next[s][0], seg[s][1] = next[s][1];
      ns = nn;
    }
    for (int s = 0; s < ns && k < max; s++)
      if (seg[s][1] - seg[s][0] >= 40) out[k++] = {seg[s][0] - w / 2, seg[s][1] - w / 2, standY};
  }
  return k;
}
