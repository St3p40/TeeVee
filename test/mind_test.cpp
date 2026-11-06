// Host tests for the mind net: the innate prior, that it stays alive and bounded, that it learns from reactions, saving
// and the explanations.
#include <cstdio>
#include <cmath>
#include <vector>
#include "idle.h"
#include "mind.h"
#include "mind_actions.h"
#include "json.h"
#include "needs.h"
#include <string>
#include "diary.h"
#include "appraisal.h"
#include "behaviour.h"
#include "taste.h"
#include "temper.h"
#include "wardrobe.h"
#include "daytint.h"
static int fails = 0;
#define CHECK(c)                                                                                             \
  do {                                                                                                       \
    if (!(c)) {                                                                                              \
      printf("FAIL line %d: %s\n", __LINE__, #c);                                                            \
      fails++;                                                                                               \
    }                                                                                                        \
  } while (0)
using namespace mind;

static MindInputs calm() {
  MindInputs i;
  i.v = 0.1f;
  i.a = 0.0f;
  i.mood = 0.1f;
  i.hour = 14;
  i.sinceTouch = 300;
  i.sinceEvent = 300;
  i.idleSec = 20;
  i.openness = 0.4f;
  return i;
}

static void run(MindNet& m, const MindInputs& in, int steps, float mean[OUT], float sd[OUT] = nullptr) {
  double s[OUT] = {}, s2[OUT] = {};
  for (int t = 0; t < steps; t++) {
    m.step(in, 0.1f);
    for (int k = 0; k < OUT; k++) {
      s[k] += m.raw(k);
      s2[k] += m.raw(k) * m.raw(k);
    }
  }
  for (int k = 0; k < OUT; k++) {
    mean[k] = (float)(s[k] / steps);
    if (sd) sd[k] = (float)sqrt(fmax(0.0, s2[k] / steps - (s[k] / steps) * (s[k] / steps)));
  }
}

int main() {
  float mu[OUT], sd[OUT];
  //==============================
  // innate prior
  //==============================
  {
    MindNet m;
    m.init(1);
    m.setRates(0.05f, 0);
    MindInputs happy = calm();
    happy.v = 0.8f;
    happy.mood = 0.5f;
    happy.a = 0.3f;
    run(m, happy, 100, mu);
    CHECK(mu[U_MIRTH] > 0.35f);
    MindNet m2;
    m2.init(1);
    m2.setRates(0.05f, 0);
    MindInputs sleepy = calm();
    sleepy.sleepy = 0.9f;
    sleepy.idleSec = 300;
    sleepy.a = -0.4f;
    run(m2, sleepy, 100, mu);
    CHECK(mu[U_DROWSY] > 0.5f && mu[U_ENERGY] < -0.2f);
    MindNet m3;
    m3.init(1);
    m3.setRates(0.05f, 0);
    MindInputs poked = calm();
    poked.v = -0.3f;
    poked.pokeRate = 4;
    run(m3, poked, 100, mu);
    CHECK(mu[U_SUSPICIOUS] > 0.2f);
    MindNet m4;
    m4.init(1);
    m4.setRates(0.05f, 0);
    MindInputs lean = calm();
    lean.tiltX = 0.8f;
    run(m4, lean, 100, mu);
    printf("  tilt gaze_x %.2f\n", mu[U_GAZE_X]);
    CHECK(mu[U_GAZE_X] > 0.2f);
  }
  //==============================
  // alive: in a constant context the urges keep wandering, bounded, repeatably
  //==============================
  {
    MindNet m;
    m.init(7);
    m.setRates(0.05f, 0.15f);
    run(m, calm(), 36000, mu, sd);
    int moving = 0;
    for (int k = 0; k < OUT; k++) {
      printf("  %-10s mean %+.2f sd %.2f\n", URGE_NAMES[k], mu[k], sd[k]);
      moving += sd[k] > 0.03f;
    }
    CHECK(moving >= 7);
    for (int k = 0; k < OUT; k++) CHECK(fabsf(m.urge(k)) <= 1.0f);
    CHECK(m.activity() > 0.02f && m.activity() < 1.0f);
    MindNet a, b;
    a.init(9);
    b.init(9);
    for (int t = 0; t < 500; t++) {
      a.step(calm(), 0.1f);
      b.step(calm(), 0.1f);
    }
    bool same = true;
    for (int k = 0; k < OUT; k++) same = same && a.urge(k) == b.urge(k);
    CHECK(same);
    MindNet c;
    c.init(10);
    for (int t = 0; t < 500; t++) c.step(calm(), 0.1f);
    bool differ = false;
    for (int k = 0; k < OUT; k++) differ = differ || a.urge(k) != c.urge(k);
    CHECK(differ);
  }
  //==============================
  // learning: an owner who pets it when it smiles and pokes it when it is restless
  //==============================
  {
    MindNet m;
    m.init(3);
    m.setRates(0.05f, 0.15f);
    float before[OUT], after[OUT];
    {
      MindNet probe = m;
      probe.setRates(0.05f, 0);
      run(probe, calm(), 3000, before);
    }
    uint32_t r = 12345;
    auto rnd = [&]() {
      r = r * 1664525u + 1013904223u;
      return (r >> 8) / 16777216.f;
    };
    MindInputs in = calm();
    int pets = 0, pokes = 0;
    for (int t = 0; t < 72000; t++) {
      m.step(in, 0.1f);
      if (m.urge(U_MIRTH) > 0.25f && m.urge(U_RESTLESS) < -0.2f && rnd() < 0.01f) {
        m.reward(0.8f);
        pets++;
      }
      if (m.urge(U_RESTLESS) > -0.2f && rnd() < 0.01f) {
        m.reward(-0.3f);
        pokes++;
      }
    }
    {
      MindNet probe = m;
      probe.setRates(0.05f, 0);
      run(probe, calm(), 3000, after);
    }
    printf("  pets %d pokes %d; mirth %+.2f -> %+.2f, restless %+.2f -> %+.2f, %u updates\n", pets, pokes,
           before[U_MIRTH], after[U_MIRTH], before[U_RESTLESS], after[U_RESTLESS],
           (unsigned)m.weights().updates);
    CHECK(after[U_MIRTH] > before[U_MIRTH] + 0.1f);
    CHECK(after[U_RESTLESS] < before[U_RESTLESS] - 0.05f);
    for (int k = 0; k < OUT; k++) CHECK(m.shift(k) < 20.f);
    MindNet::Reason why[3];
    CHECK(m.learned(U_MIRTH, why, 3) == 3);
    MindNet n2;
    n2.init(5);
    CHECK(n2.load(&m.weights(), sizeof(MindNet::Weights)));
    CHECK(memcmp(&n2.weights(), &m.weights(), sizeof(MindNet::Weights)) == 0);
    CHECK(!n2.load(&m.weights(), sizeof(MindNet::Weights) - 4));
    for (
        const mind::SavedLayout& lay : mind::
            OLD_LAYOUTS) {  // older saves load (v2, v3, v4): learning kept, new urges and senses from the prior
      const int ver = (int)lay.version, oOut = lay.out, oIn = lay.in;
      const MindNet::Weights& w = m.weights();
      std::vector<uint8_t> old;
      auto put = [&](const void* d, size_t len) {
        old.insert(old.end(), (const uint8_t*)d, (const uint8_t*)d + len);
      };
      const uint32_t head[3] = {(uint32_t)ver, w.updates, w.seed};
      put(head, sizeof head);
      for (int k = 0; k < oOut; k++) put(w.wr[k], sizeof w.wr[k]);
      for (int k = 0; k < oOut; k++) {
        put(w.wi[k], (oIn - 1) * sizeof(float));
        put(&w.wi[k][I_BIAS], sizeof(float));
      }
      put(w.b, oOut * sizeof(float));
      put(w.hourLike, sizeof w.hourLike);
      MindNet n3;
      n3.init(5);
      CHECK(n3.load(old.data(), old.size()));
      const MindNet::Weights& v = n3.weights();
      CHECK(v.version == MindNet::VERSION && v.updates == w.updates &&
            !memcmp(v.wi[U_MIRTH], w.wi[U_MIRTH], (oIn - 1) * sizeof(float)));
      CHECK(v.wi[U_MIRTH][I_BIAS] == w.wi[U_MIRTH][I_BIAS] && v.b[U_LENS] == w.b[U_LENS]);
      if (oIn <= I_PRESENCE)
        CHECK(v.wi[U_SLEEP][I_PRESENCE] == MindNet::prior(U_SLEEP, I_PRESENCE) &&
              v.wi[U_DROWSY][I_BATTERY] == MindNet::prior(U_DROWSY, I_BATTERY));
      else
        CHECK(v.wi[U_SLEEP][I_PRESENCE] == w.wi[U_SLEEP][I_PRESENCE]);
      if (oIn <= I_WATCHED) CHECK(v.wi[U_MIRTH][I_WATCHED] == MindNet::prior(U_MIRTH, I_WATCHED));
      if (oIn <= I_SCREEN) CHECK(v.wi[U_CURIOUS][I_SCREEN] == MindNet::prior(U_CURIOUS, I_SCREEN));
      CHECK(v.wi[U_MIRTH][I_TRUST] == MindNet::prior(U_MIRTH, I_TRUST));
      CHECK(!memcmp(v.hourLike, w.hourLike, sizeof v.hourLike) &&
            !memcmp(v.wr[oOut - 1], w.wr[oOut - 1], sizeof v.wr[0]));
      if (ver == 2)
        CHECK(v.wi[U_HEAD_X][I_TOUCH_X] == MindNet::prior(U_HEAD_X, I_TOUCH_X) && n3.shift(U_HEAD_Y) == 0);
      if (ver >= 3) CHECK(!memcmp(v.wi[U_HEAD_X], w.wi[U_HEAD_X], (oIn - 1) * sizeof(float)));
      CHECK(!n3.load(old.data(), old.size() - 4));
      old[0] = 1;
      CHECK(!n3.load(old.data(), old.size()));
    }
  }
  //==============================
  // presence and a low battery: with the owner there it stays up and is livelier; a low battery makes it drowsy
  //==============================
  {
    MindNet m;
    m.init(4);
    m.setRates(0.05f, 0);
    float base[OUT], here[OUT], low[OUT], tired[OUT], tiredHere[OUT];
    run(m, calm(), 600, base);
    MindInputs h = calm();
    h.presence = 1;
    run(m, h, 600, here);
    MindInputs t = calm();
    t.sleepy = 0.8f;
    t.needRest = 0.8f;
    t.idleSec = 300;
    run(m, t, 600, tired);
    t.presence = 1;
    run(m, t, 600, tiredHere);
    MindInputs b = calm();
    b.batteryLow = 1;
    run(m, b, 600, low);
    float seen[OUT], busy[OUT], liked[OUT], hurt[OUT];
    MindInputs tr = calm();
    tr.trust = 1;
    run(m, tr, 600, liked);
    tr.trust = -1;
    run(m, tr, 600, hurt);
    printf("  trust +1 / -1: mirth %+.2f / %+.2f, suspicious %+.2f / %+.2f\n", liked[U_MIRTH], hurt[U_MIRTH],
           liked[U_SUSPICIOUS], hurt[U_SUSPICIOUS]);
    CHECK(liked[U_MIRTH] > hurt[U_MIRTH] + 0.3f && hurt[U_SUSPICIOUS] > liked[U_SUSPICIOUS] + 0.5f);
    MindInputs sc = calm();
    sc.screen = 1;
    run(m, sc, 600, busy);
    printf("  screen busy: curious %+.2f -> %+.2f\n", base[U_CURIOUS], busy[U_CURIOUS]);
    CHECK(busy[U_CURIOUS] > base[U_CURIOUS] + 0.2f);
    MindInputs wt = calm();
    wt.watched = 1;
    run(m, wt, 600, seen);
    printf("  watched: mirth %+.2f -> %+.2f, emote %+.2f -> %+.2f\n", base[U_MIRTH], seen[U_MIRTH],
           base[U_EMOTE], seen[U_EMOTE]);
    CHECK(seen[U_MIRTH] > base[U_MIRTH] + 0.1f && seen[U_EMOTE] >= base[U_EMOTE]);
    printf(
        "  presence: energy %+.2f -> %+.2f; sleepy, sleep %+.2f -> %+.2f with the owner there; battery low: drowsy %+.2f -> %+.2f\n",
        base[U_ENERGY], here[U_ENERGY], tired[U_SLEEP], tiredHere[U_SLEEP], base[U_DROWSY], low[U_DROWSY]);
    CHECK(here[U_ENERGY] > base[U_ENERGY] + 0.05f && tiredHere[U_SLEEP] < tired[U_SLEEP] - 0.2f);
    CHECK(low[U_DROWSY] > base[U_DROWSY] + 0.3f && low[U_ENERGY] < base[U_ENERGY] - 0.2f);
  }
  //==============================
  // the head urges: it turns toward a touch, droops when sleepy, lifts its chin when happy
  //==============================
  {
    MindNet m;
    m.init(3);
    m.setRates(0.05f, 0);
    float base[OUT], right[OUT], sleepy[OUT], happy[OUT];
    run(m, calm(), 600, base);
    MindInputs t = calm();
    t.tx = 0.9f;
    t.sinceTouch = 1;
    t.touching = true;
    run(m, t, 600, right);
    MindInputs sl = calm();
    sl.sleepy = 0.9f;
    sl.idleSec = 200;
    run(m, sl, 600, sleepy);
    MindInputs h = calm();
    h.v = 0.9f;
    h.mood = 0.6f;
    run(m, h, 600, happy);
    printf("  head_x %+.2f -> touched right %+.2f; head_y %+.2f -> sleepy %+.2f, happy %+.2f\n",
           base[U_HEAD_X], right[U_HEAD_X], base[U_HEAD_Y], sleepy[U_HEAD_Y], happy[U_HEAD_Y]);
    CHECK(right[U_HEAD_X] > base[U_HEAD_X] + 0.3f);
    CHECK(sleepy[U_HEAD_Y] > base[U_HEAD_Y] + 0.3f && happy[U_HEAD_Y] < base[U_HEAD_Y] - 0.15f);
  }
  //==============================
  // no reward without exploration or learning rate: the weights stay as they were
  //==============================
  {
    MindNet m;
    m.init(3);
    m.setRates(0.05f, 0);
    MindNet::Weights w0 = m.weights();
    for (int t = 0; t < 600; t++) {
      m.step(calm(), 0.1f);
      m.reward(1.f);
    }
    CHECK(fabsf(m.weights().wi[U_MIRTH][I_VALENCE] - w0.wi[U_MIRTH][I_VALENCE]) < 1e-3f);
  }
  //==============================
  // explain
  //==============================
  {
    MindNet m;
    m.init(1);
    m.setRates(0.05f, 0);
    MindInputs happy = calm();
    happy.v = 0.9f;
    happy.mood = 0.0f;
    for (int t = 0; t < 50; t++) m.step(happy, 0.1f);
    MindNet::Reason why[3];
    int n = m.explain(U_MIRTH, why, 3);
    CHECK(n == 3 && why[0].input == I_VALENCE && why[0].amount > 0.5f);
    CHECK(fabsf(why[0].amount) >= fabsf(why[1].amount) && fabsf(why[1].amount) >= fabsf(why[2].amount));
  }
  //==============================
  // through the idle layer: the face moves on its own, within limits, and the urges show
  //==============================
  {
    IdleGen idle;
    idle.begin(5, 0);
    MindNet m;
    m.init(5);
    m.setRates(0.05f, 0.15f);
    FaceParams base{};
    base.eye_open_l = base.eye_open_r = 0.8f;
    double s[4] = {}, s2[4] = {};
    int n = 0;
    bool bounded = true;
    for (uint32_t now = 0; now < 600000; now += 20) {
      if (now % 100 == 0) {
        MindInputs in = calm();
        in.idleSec = idle.idleSeconds(now);
        m.step(in, 0.1f);
        float u[OUT];
        for (int k = 0; k < OUT; k++) u[k] = m.urge(k);
        idle.setUrges(u, 1.f);
      }
      FaceParams p = idle.apply(base, now);
      const float v[4] = {p.pupil_x, p.mouth_curve, p.brow_height, p.squash};
      for (int i = 0; i < 4; i++) {
        s[i] += v[i];
        s2[i] += v[i] * v[i];
      }
      n++;
      bounded = bounded && fabsf(p.pupil_x) <= 1 && fabsf(p.pupil_y) <= 1 && p.eye_open_l >= 0 &&
                p.eye_open_l <= 1.5f && fabsf(p.mouth_curve) <= 2;
    }
    const char* nm[4] = {"pupil_x", "mouth_curve", "brow_height", "squash"};
    int alive = 0;
    for (int i = 0; i < 4; i++) {
      float sd = (float)sqrt(fmax(0.0, s2[i] / n - (s[i] / n) * (s[i] / n)));
      printf("  face %-12s sd %.3f\n", nm[i], sd);
      alive += sd > 0.05f;
    }
    CHECK(bounded);
    CHECK(alive >= 3);
  }
  //==============================
  // spontaneous emotes / yawns: rare but they happen when nobody has been around for a while
  //==============================
  {
    MindNet m;
    m.init(21);
    m.setRates(0.05f, 0.15f);
    MindInputs in = calm();
    in.idleSec = 1200;
    in.v = 0.2f;
    int emotes = 0, yawns = 0;
    bool eh = false, yh = false;
    for (int t = 0; t < 36000; t++) {
      in.sleepy = t > 18000 ? 0.4f : 0.f;
      m.step(in, 0.1f);
      const float e = m.urge(U_EMOTE), y = m.urge(U_YAWN);
      if (e > 0.4f && !eh) {
        eh = true;
        emotes++;
      } else if (e < 0.1f)
        eh = false;
      if (y > 0.5f && !yh) {
        yh = true;
        yawns++;
      } else if (y < 0.2f)
        yh = false;
    }
    printf("  an idle hour: %d emotes, %d yawns\n", emotes, yawns);
    CHECK(emotes >= 1 && emotes <= 40);
    CHECK(yawns >= 1);
  }
  //==============================
  // the explanations fit their buffers (console 2800, board web reply 3600, desktop 3000, web 3200)
  //==============================
  {
    MindNet m;
    m.init(2);
    m.setRates(0.05f, 0.15f);
    for (int t = 0; t < 300; t++) m.step(calm(), 0.1f);
    char big[8000];
    const size_t js = mindJson(m, big, sizeof big), tx = mindReport(m, big + 4000, 4000);
    printf("  mindJson %zu bytes, mindReport %zu bytes\n", js, tx);
    CHECK(js < (MindNet::N > 32 ? 4400u : 3300u) && tx < 2700);
  }
  //==============================
  // needs: they grow with neglect, shrink with care, and make it want things
  //==============================
  {
    Needs n;
    for (int i = 0; i < 3600; i++) n.update(1.f, true, false, false, false);
    CHECK(n.attention > 0.8f && n.rest > 0.2f && n.rest < 0.4f && n.play > 0.4f);
    CHECK(n.valenceShift() < -0.1f && n.arousalShift() < -0.05f);
    n.fed(0.5f);
    CHECK(n.attention < 0.5f);
    n.played(1.f);
    CHECK(n.play == 0.f);
    for (int i = 0; i < 1800; i++) n.update(1.f, false, true, false, false);
    CHECK(n.rest < 0.05f);
    Needs::Saved s = n.save();
    Needs m;
    CHECK(m.load(&s, sizeof s) && m.attention == n.attention && !m.load(&s, sizeof s - 1));
    MindNet net;
    net.init(4);
    net.setRates(0.05f, 0);
    MindInputs lonely = calm();
    lonely.needAttention = 1.f;
    lonely.needPlay = 1.f;
    lonely.idleSec = 600;
    MindInputs sated = calm();
    sated.idleSec = 600;
    float a[OUT], b[OUT];
    run(net, lonely, 100, a);
    MindNet net2;
    net2.init(4);
    net2.setRates(0.05f, 0);
    run(net2, sated, 100, b);
    CHECK(a[U_EMOTE] > b[U_EMOTE] + 0.5f && a[U_RESTLESS] > b[U_RESTLESS] + 0.5f && a[U_MIRTH] < b[U_MIRTH]);
  }
  //==============================
  // diary and day tint
  //==============================
  {
    static Tastes t;
    t.emote.chose(EM_HEART, 1000);
    for (int i = 0; i < 20; i++) t.react(0.9f, 1000 + i, 2, 20);
    CHECK(t.emote.score[EM_HEART] > 0.5f && t.emote.weight(EM_HEART) > 1.5f);
    t.emote.chose(EM_ANGRY, 100000);
    t.react(-0.8f, 100000 + Tastes::EMOTE_WINDOW_MS + 1, 2, 20);
    CHECK(t.emote.score[EM_ANGRY] == 0);
    t.react(-0.8f, 100001, 2, 20);
    CHECK(t.emote.score[EM_ANGRY] < 0);
    CHECK(t.weekLike(2, 20) > 0.1f && t.weekLike(3, 20) == 0 && t.weekLike(-1, 0) == 0 &&
          t.weekLike(9, 0) == 0);
    t.react(1, 0, -1, 0);
    t.react(1, 0, 7, 0);
    t.react(1, 0, 0, 24);
    uint32_t rr = 5;
    const auto rnd = [&]() {
      rr ^= rr << 13;
      rr ^= rr >> 17;
      rr ^= rr << 5;
      return (rr >> 8) / 16777216.f;
    };
    t.eye.chose(4, 0);
    for (int i = 0; i < 40; i++) t.react(0.9f, 200000, -1, 0);
    int four = 0;
    for (int i = 0; i < 700; i++) {
      const int c = t.eye.pick(EYE_COUNT, 0, rnd);
      CHECK(c >= 1 && c < EYE_COUNT);
      four += c == 4;
    }
    CHECK(four > 700 / 6 * 1.5f);
    CHECK(four < 650);
    t.gameStarted("snake", 0);
    for (int i = 0; i < 30; i++) t.won(0.8f, true, 0);
    t.gameStarted("dice", 0);
    for (int i = 0; i < 30; i++) t.lost(0.8f, true, 0);
    t.gameEnded();
    CHECK(t.appScore("snake") > 0.4f && t.appScore("dice") < -0.2f && t.appScore("never") == 0);
    CHECK(t.streak < -0.5f);
    t.won(1, false, 0);
    CHECK(t.streak > 0);
    t.update(6000);
    CHECK(fabsf(t.streak) < 0.01f);
    const char* names[3] = {"snake", "dice", "never"};
    int picks[3] = {};
    for (int i = 0; i < 600; i++) picks[t.suggest(names, 3, rnd)]++;
    CHECK(picks[0] > picks[2] && picks[2] > picks[1] && picks[1] > 0);
    static Tastes::Saved s;
    s = t.save();
    static Tastes u;
    CHECK(u.load(&s, sizeof s) && u.appScore("snake") == t.appScore("snake") &&
          u.weekLike(2, 20) == t.weekLike(2, 20));
    CHECK(u.load(&s, offsetof(Tastes::Saved, appName)));  // an older, shorter save
    s.emote[EM_HEART] = NAN;
    s.week[0] = 50;
    CHECK(u.load(&s, sizeof s) && u.emote.score[EM_HEART] == 0 && u.week[0] == 0);
    s.version = 99;
    CHECK(!u.load(&s, sizeof s) && !u.load(&s, 2) && !u.load(&s, sizeof s + 1));
    for (int i = 0; i < 20; i++) t.gameStarted(("game" + std::to_string(i)).c_str(), 0);
    char line[200];
    CHECK(t.summary(line, sizeof line) > 0 && strstr(line, "likes heart"));
    printf("  tastes: %s\n", line);
    CHECK(t.summary(line, 8) <= 7);
    // the emote picker leans too, and never picks outside its three
    float liked[EM_COUNT] = {};
    liked[EM_SPARKLE] = 1;
    float urge[MindNet::OUT] = {};
    urge[U_MIRTH] = 0.9f;
    int sparkle = 0;
    for (uint32_t r = 0; r < 1024; r++) {
      const uint8_t e = mindPickEmote(urge, r * 7, liked);
      CHECK(e == EM_HEART || e == EM_MUSIC || e == EM_SPARKLE);
      sparkle += e == EM_SPARKLE;
    }
    CHECK(sparkle > 1024 / 2);
  }
  {
    static Behaviour b, c, d, keep;
    static char text[8192];
    const size_t n = behaviourExport(b, text, sizeof text);
    printf("  behaviour export: %zu bytes\n", n);
    CHECK(n > 500 && n < 3900);
    char err[64];
    int skipped = -1;
    CHECK(behaviourParse(text, n, c, err, sizeof err, &skipped) && skipped == 0 && !err[0]);
    CHECK(c.attentionHours == b.attentionHours &&
          c.personality[3].sensitivity == b.personality[3].sensitivity &&
          c.personality[4].baseline.a == b.personality[4].baseline.a);
    const char good[] =
        "# my robot\r\nteevee-behaviour 1\r\nneeds.play_hours 5 # slower\n\nemote.gap_seconds 1\npersonality.grumpy.sensitivity 2.5\n"
        "react.pet heart 3 none 1\nreact.poke auto\ninstinct mirth pet_rate 1.5\nnonsense 1\nneeds.rest_hours abc\nreact.pet nosuch 1\ninstinct mirth nosuch 1\npersonality.nobody.sensitivity 1\n";
    CHECK(behaviourParse(good, strlen(good), c, err, sizeof err, &skipped));
    CHECK(skipped == 5 && strstr(err, "line 10"));
    CHECK(c.playHours == 5 && c.emoteGapSeconds == 5 && c.personality[2].sensitivity == 2.5f);
    CHECK(c.restHours == 6);
    CHECK(c.rule[EV_PET].n == 2 && c.rule[EV_PET].emote[0] == EM_HEART &&
          c.rule[EV_PET].emote[1] == EM_NONE && c.rule[EV_POKE].n == 0);
    CHECK(c.instincts == 1 && c.instinct[0].urge == U_MIRTH && c.instinct[0].sense == I_PET &&
          c.instinct[0].w == 1.5f);
    const size_t back = behaviourExport(c, text, sizeof text);
    CHECK(behaviourParse(text, back, d, err, sizeof err, &skipped) && skipped == 0 && d.rule[EV_PET].n == 2 &&
          d.instincts == 1 && d.playHours == 5);
    const float before = keep.playHours;
    CHECK(!behaviourParse("hello\n", 6, keep, err, sizeof err) && strstr(err, "first line"));
    CHECK(!behaviourParse("teevee-behaviour 2\nneeds.play_hours 9\n", 38, keep, err, sizeof err) &&
          strstr(err, "version"));
    CHECK(!behaviourParse("", 0, keep, err, sizeof err));
    CHECK(keep.playHours == before);
    std::string longLine = "teevee-behaviour 1\n" + std::string(400, 'x') + "\nneeds.play_hours 7\n";
    CHECK(behaviourParse(longLine.data(), longLine.size(), d, err, sizeof err, &skipped) && skipped == 1 &&
          d.playHours == 7);
    // what it does: the instinct reaches a new net; a rule replaces the appraisal's choice, and never with what it does not list
    mind::Instinct one = {U_MIRTH, I_PET, 1.5f};
    mind::INSTINCTS = &one;
    mind::INSTINCT_COUNT = 1;
    CHECK(MindNet::prior(U_MIRTH, I_PET) == 1.5f);
    mind::INSTINCTS = nullptr;
    mind::INSTINCT_COUNT = 0;
    CHECK(MindNet::prior(U_MIRTH, I_PET) != 1.5f);
    Appraisal ap;
    ap.begin(3);
    ap.setRules(c.rule);
    bool onlyListed = true;
    int hearts = 0;
    PAD glad{0.9f, 0, 0};
    for (int i = 0; i < 200; i++) {
      const uint8_t e = ap.emoteFor(EV_PET, glad);
      onlyListed = onlyListed && (e == EM_HEART || e == EM_NONE);
      hearts += e == EM_HEART;
    }
    CHECK(onlyListed && hearts > 30);
    Needs fast;
    fast.attention = 0;
    fast.attentionSec = 100;
    fast.update(10, true, false, false, false);
    CHECK(fabsf(fast.attention - 0.1f) < 0.001f);
  }
  {
    char out[300];
    const char whole[] =
        "day,pet,poke,praise,scold,greet,love,annoy,win,mood\n1,2,1,0,1,0,0,0,0,40\n2,0,0,0,0,0,0,0,0,\n";
    diaryLogJson(whole, strlen(whole), out, sizeof out);
    CHECK(!strcmp(out, "{\"log\":[[1,2,2,40],[2,0,0,null]]}"));
    const char cut[] = "0,1,0,0,0,0,40\n3,1,1,1,1,1,1,1,1,-20\nbroken,line\n4,1";
    diaryLogJson(cut, strlen(cut), out, sizeof out);
    CHECK(!strcmp(out, "{\"log\":[[3,5,3,-20]]}"));
    diaryLogJson("", 0, out, sizeof out);
    CHECK(!strcmp(out, "{\"log\":[]}"));
  }
  {
    Diary none;
    none.begin(1);
    CHECK(fabsf(none.feel().warmth) < 0.01f && none.feel().lonely == 0);
    Diary alone;
    alone.begin(1);
    for (int i = 0; i < 200; i++) alone.mood(0, 10);
    CHECK(alone.feel().lonely > 0.95f);
    Diary kind;
    kind.begin(1);
    for (int i = 0; i < 20; i++) kind.event(EV_PET);
    CHECK(kind.feel().warmth > 0.5f && kind.feel().lonely == 0);
    Diary hard;
    hard.begin(1);
    for (int i = 0; i < 8; i++) hard.event(EV_SCOLD);
    CHECK(hard.feel().warmth < -0.5f);
    Diary one;
    one.begin(1);
    one.event(EV_PET);
    CHECK(fabsf(one.feel().warmth) < 0.2f);
    const auto antics = [](float warm, float lonely, int* seen) {
      IdleGen g;
      g.begin(11, 0);
      g.setDrowsyAfter(0x7FFFFFFFu);
      float u[IdleGen::URGES] = {};
      g.setUrges(u, 1.f);
      g.setDay(warm, lonely);
      FaceParams base{};
      int last = 0, total = 0;
      for (uint32_t t = 0; t < 3600000u; t += 20) {
        g.apply(base, t);
        const int b = g.behaviour(t);
        if (b && b != last) seen[b]++, total++;
        last = b;
      }
      return total;
    };
    int k[IdleGen::B_COUNT] = {}, h[IdleGen::B_COUNT] = {}, l[IdleGen::B_COUNT] = {},
        n[IdleGen::B_COUNT] = {};
    const int tk = antics(0.8f, 0, k), th = antics(-0.8f, 0, h), tl = antics(0, 0.9f, l),
              tn = antics(0, 0, n);
    printf("  a day's antics in an hour alone: kind %d, hard %d, lonely %d, ordinary %d\n", tk, th, tl, tn);
    CHECK(k[IdleGen::B_SMILE] > 3 && k[IdleGen::B_SQUINT] == 0 && k[IdleGen::B_EYEROLL] == 0);
    CHECK(h[IdleGen::B_SQUINT] > 3 && h[IdleGen::B_SMILE] == 0 && h[IdleGen::B_WHISTLE] == 0);
    CHECK(l[IdleGen::B_LOOK_AROUND] > 3 && l[IdleGen::B_SMILE] == 0);
    CHECK(tn == 0);
    CHECK(tk < 120 && th < 120);
  }
  {
    Diary d;
    d.begin(10);
    d.event(EV_PET);
    d.event(EV_PET);
    d.event(EV_POKE);
    d.event(EV_STARTLE);
    d.mood(0.5f, 9.2f);
    d.mood(0.7f, 9.8f);
    d.mood(-0.2f, 20.1f);
    CHECK(d.day(0).n[Diary::K_PET] == 2 && d.day(0).n[Diary::K_POKE] == 1 &&
          Diary::moodAt(d.day(0), 9) == 60 && d.bestHour() == 9);
    d.newDay(11);
    d.event(EV_PRAISE);
    CHECK(d.today() == 11 && d.day(1).id == 10 && d.day(1).n[Diary::K_PET] == 2 &&
          d.day(0).n[Diary::K_PRAISE] == 1);
    for (int i = 12; i < 22; i++) d.newDay(i);
    CHECK(d.day(Diary::DAYS - 1).id == 15);
    char js[2000];
    size_t n = d.json(js, sizeof js);
    (void)n;
    Diary e;
    e.begin(1);
    CHECK(e.load(&d.saved(), sizeof(Diary::Saved)) && e.today() == d.today() && !e.load(&d.saved(), 10));
    Diary f;
    f.begin(5);
    f.event(EV_PET);
    f.mood(0.4f, 12.f);
    n = f.json(js, sizeof js);
    printf("  diary json %zu bytes\n", n);
    JsonView jv;
    CHECK(jsonParse(js, n, jv) && jv.get("diary").get("days").at(0).get("n").at(0).num(0) == 1 &&
          jv.get("diary").get("days").at(0).get("mood").at(12).num(0) == 40);
    CHECK(sizeof(Diary::Saved) < 1500);
    DayTint noon = dayTintAt(12, 1), night = dayTintAt(2, 1), off = dayTintAt(2, 0),
            unknown = dayTintAt(-1, 1);
    CHECK(noon.hue == 0 && noon.val == 1 && night.hue > 10 && night.val < 0.8f && off.hue == 0 &&
          off.val == 1 && unknown.val == 1);
    DayTint dusk = dayTintAt(18.5f, 1);
    CHECK(dusk.hue < -10);
  }
  //==============================
  // sleep decision, pupils and the memory of liked hours
  //==============================
  {
    MindNet a;
    a.init(8);
    a.setRates(0.05f, 0);
    MindInputs tired = calm();
    tired.needRest = 1.f;
    tired.sleepy = 0.3f;
    tired.idleSec = 300;
    tired.hour = 23.f;
    tired.a = -0.3f;
    MindInputs fresh = calm();
    fresh.hour = 14.f;
    float t1[OUT], f1[OUT];
    run(a, tired, 100, t1);
    MindNet b;
    b.init(8);
    b.setRates(0.05f, 0);
    run(b, fresh, 100, f1);
    CHECK(t1[U_SLEEP] > 0.3f && f1[U_SLEEP] < -0.3f);
    MindInputs excited = calm();
    excited.a = 0.8f;
    excited.light = -0.8f;
    MindNet c;
    c.init(8);
    c.setRates(0.05f, 0);
    float e1[OUT], k1[OUT];
    run(c, excited, 100, e1);
    MindNet d;
    d.init(8);
    d.setRates(0.05f, 0);
    run(d, calm(), 100, k1);
    CHECK(e1[U_PUPIL] > k1[U_PUPIL] + 0.3f);
    MindNet m;
    m.init(2);
    m.setRates(0.05f, 0.15f);
    MindInputs morning = calm();
    morning.hour = 9.3f;
    MindInputs afternoon = calm();
    afternoon.hour = 15.3f;
    for (int i = 0; i < 20; i++) {
      m.step(morning, 0.1f);
      m.reward(0.8f);
      m.step(afternoon, 0.1f);
      m.reward(-0.6f);
    }
    CHECK(m.weights().hourLike[9] > 0.5f && m.weights().hourLike[15] < -0.4f &&
          m.weights().hourLike[3] == 0.f);
    m.step(morning, 0.1f);
    CHECK(m.input(I_HOUR_LIKE) > 0.5f);
  }
  //==============================
  // power-on: asleep, then it wakes up with a yawn
  //==============================
  {
    IdleGen idle;
    idle.begin(3, 1000);
    idle.fallAsleepNow(1000);
    FaceParams base{};
    base.eye_open_l = base.eye_open_r = 0.8f;
    CHECK(idle.state(1000) == IdleGen::ASLEEP && idle.apply(base, 1100).eye_open_l < 0.1f);
    CHECK(sleepingPose(base).eye_open_l < 0.1f);
    idle.wakeUp(2600);
    CHECK(idle.state(2600) == IdleGen::AWAKE && idle.behaviour(2700) == IdleGen::B_YAWN);
    float open = 0;
    for (uint32_t t = 2600; t < 6000; t += 20) open = fmaxf(open, idle.apply(base, t).eye_open_l);
    CHECK(open > 0.3f);
    IdleGen never;
    never.begin(5, 0);
    never.setDrowsyAfter(0x7FFFFFFFu);
    never.fallAsleepNow(0);  // sleep_after = 0 (never doze) must not stop the power-on sleep
    CHECK(never.state(100) == IdleGen::ASLEEP);
    never.wakeUp(1000);
    CHECK(never.state(1100) == IdleGen::AWAKE);
    IdleGen early;
    early.begin(4, 0);
    early.fallAsleepNow(0);
    early.poke(500);
    CHECK(early.state(600) == IdleGen::AWAKE);
  }
  //==============================
  // temperament drift and the wardrobe
  //==============================
  {
    Temper t;
    for (int i = 0; i < 60; i++) t.react(EV_PET, 1.f, 1.f);
    CHECK(t.valence > 0.07f && t.valence < 0.12f);
    for (int i = 0; i < 400; i++) t.react(EV_PRAISE, 1.f, 1.f);
    CHECK(t.valence == 0.2f);
    for (int i = 0; i < 100; i++) t.react(EV_SCOLD, 1.f, 2.f);
    CHECK(t.valence < 0.0f);
    Temper u;
    u.react(EV_PET, 1.f, 0.f);
    CHECK(u.valence == 0.f);
    Temper w;
    w.valence = 0.2f;
    for (int d = 0; d < 5; d++) w.update(86400.f);
    CHECK(w.valence > 0.09f && w.valence < 0.11f);
    Temper::Saved sv = t.save();
    Temper z;
    CHECK(z.load(&sv, sizeof sv) && z.valence == t.valence && !z.load(&sv, 3));
    const char* names[] = {"sweet", "monster", "dreamy", "noir", "sparkle"};
    Wardrobe wr;
    uint32_t rs = 7;
    auto rnd = [&]() {
      rs = rs * 1664525u + 1013904223u;
      return (rs >> 8) / 16777216.f;
    };
    int happy[6] = {}, sad[6] = {}, tired[6] = {};
    for (int i = 0; i < 300; i++) {
      happy[wr.pick(names, 5, 0.8f, 0.2f, 0, rnd)]++;
      sad[wr.pick(names, 5, -0.7f, 0.1f, 0, rnd)]++;
      tired[wr.pick(names, 5, 0.0f, -0.5f, 0.9f, rnd)]++;
    }
    CHECK(happy[1] + happy[5] > happy[2] + happy[3]);
    CHECK(sad[2] > sad[1] && sad[2] > sad[5]);
    CHECK(tired[3] + tired[4] > tired[1] + tired[2]);
    Wardrobe ww;
    ww.current = 2;
    CHECK(ww.pick(names, 5, 0.8f, 0.2f, 0, rnd) != 2);
    for (int i = 0; i < 30; i++) ww.reward(0.9f);
    CHECK(ww.score[2] > 0.8f);
    {  // a save from the presets' old order: scores go with their looks, a look that is gone takes its score with it; the mouths' never move
      Wardrobe::Saved v1{};
      v1.version = 1;
      v1.score[0] = 0.1f;
      v1.score[1] = 0.2f;
      v1.score[3] = 0.3f;
      v1.score[32] = 0.4f;
      Wardrobe a;
      CHECK(a.load(&v1, sizeof v1, styleOldPreset));
      CHECK(a.score[0] == 0.1f && a.score[1] == 0.2f && a.score[styleFindPreset("moonlit") + 1] == 0.4f);
      float sum = 0;
      for (float f : a.score) sum += f;
      CHECK(fabsf(sum - 0.7f) < 1e-5f);
      Wardrobe m;
      CHECK(m.load(&v1, sizeof v1) && m.score[3] == 0.3f && m.score[32] == 0.4f);
    }
    Wardrobe::Saved ws = ww.save(5);
    Wardrobe w2;
    CHECK(w2.load(&ws, sizeof ws) && w2.score[2] == ww.score[2]);
    {
      Wardrobe old;
      old.score[3] = 0.5f;
      old.score[39] = -0.25f;
      Wardrobe::Saved os = old.save(39);
      Wardrobe w3;
      w3.score[45] = 0.9f;
      const size_t oldLen = 8 + 40 * sizeof(float);
      CHECK(w3.load(&os, oldLen) && w3.score[3] == 0.5f && w3.score[39] == -0.25f && w3.score[45] == 0);
      CHECK(!w3.load(&os, oldLen - 2) && !w3.load(&os, 4) && !w3.load(&os, sizeof os + 4));
    }
  }
  //==============================
  // the urges the idle layer and the actions know about
  //==============================
  CHECK(MindNet::OUT == IdleGen::URGES && U_GLITCH == 10 && U_YAWN == 11 && U_EMOTE == 12 && U_PUPIL == 13 &&
        U_SLEEP == 14 && U_WARDROBE == 15);
  {
    float u[OUT] = {};
    u[U_MIRTH] = 0.8f;
    CHECK(mindPickEmote(u, 0) == EM_HEART && mindPickEmote(u, 1) == EM_MUSIC);
    u[U_MIRTH] = 0;
    u[U_CURIOUS] = 0.7f;
    CHECK(mindPickEmote(u, 0) == EM_QUESTION);
    u[U_CURIOUS] = 0;
    CHECK(mindPickEmote(u, 5) == EM_MUSIC);
  }
  printf("mind_test: sizeof Weights %zu, sizeof MindNet %zu: %s\n", sizeof(MindNet::Weights), sizeof(MindNet),
         fails ? "FAILED" : "ok");
  return fails != 0;
}
