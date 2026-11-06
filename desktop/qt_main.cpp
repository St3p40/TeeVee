//  ######### ####### ####### ##   ## ####### #######
//     ##     ##      ##      ##   ## ##      ##
//     ##     #####   #####   ##   ## #####   #####
//     ##     ##      ##       ## ##  ##      ##
//     ##     ####### #######   ###   ####### #######
// MADE BY ST3P40
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCursor>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QFileDialog>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSlider>
#include <QSplitter>
#include <QStyleFactory>
#include <QSystemTrayIcon>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWindow>
#include <SDL3/SDL.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <functional>
#include <string>
#include <vector>
#include "canvas.h"
#include "engine.h"
#include "keys.h"
#include "platform_lcd.h"
#include "pet_os.h"
#include "render_common.h"
#include "renderer.h"
#include "pack_io.h"
#include "settings_panel.h"

namespace {
const char* HELP_TEXT =
    "TeeVee for the desktop (Qt window)\n"
    "\n"
    "Options:\n"
    "  --pip            start in the picture-in-picture window\n"
    "  --light          the light theme for this run (the bar's Light button changes it and remembers)\n"
    "  --tray           an icon in the system tray: closing the window keeps it running there, the menu can start it with the computer\n"
    "  --scale N        size of the face as a multiple of 320x240 (default 2)\n"
    "  --fullscreen     start fullscreen          --top        keep the window on top\n"
    "  --nearest        sharp pixels instead of smooth scaling\n"
    "  --no-follow      eyes do not follow the mouse    --no-wake    start awake\n"
    "  --fisheye N      a fisheye lens on the picture, 0..1 (0 = off)\n"
    "  --head-turn N    the head turns like a box up to N degrees (0 = off)\n"
    "  --reset          forget the saved look, personality and learning\n"
    "  --seed N         random seed\n"
    "  --frames N       stop after N frames       --shot FILE  save the window as an image at the end\n"
    "  --tab NAME       open this tab: look, play, mind, keys\n";

const char* KEYS_TEXT = "Mouse\n"
                        "  click: poke    drag: pet    hold still: gentle pet\n"
                        "  hover: the eyes follow\n"
                        "\n"
                        "Picture in picture\n"
                        "  F3: open or close it\n"
                        "  right-drag: move    wheel: size    right-click: menu\n"
                        "  the corner button or Esc: back to the full window\n"
                        "\n"
                        "Keys (click the face first)\n"
                        "  1-9: hold a face    , .: previous / next\n"
                        "  Backspace: back to the mood\n"
                        "  P pet   O poke   V praise   S scold   D startle\n"
                        "  W win   F fail   G greet   Z tired   N annoy\n"
                        "  L love   C confuse   B bore\n"
                        "  E / Shift+E: next / previous emote\n"
                        "  Tab / Shift+Tab: next / previous look\n"
                        "  R: change a part of the look    Shift+R: all of it\n"
                        "  Q: next personality    Space: sleep / wake\n"
                        "  T: always on top    F11 or Alt+Enter: fullscreen\n"
                        "  F2: show or hide the controls    I: mind    H or F1: this list\n"
                        "  Esc: quit";

QColor C_BG(0x10, 0x0d, 0x14), C_PANEL(0x1a, 0x15, 0x20), C_RAISED(0x24, 0x1c, 0x2b),
    C_LINE(0x33, 0x29, 0x3b);
QColor C_TEXT(0xf4, 0xec, 0xf1), C_DIM(0xa9, 0x9d, 0xab), C_ACCENT(0xf2, 0x52, 0x59);

const char* STYLE_SHEET = R"(
* { color: @text; }
QMainWindow, QScrollArea, QScrollArea > QWidget > QWidget, QTabWidget::pane, QSplitter { background: @panel; border: none; }
QToolBar { background: @raised; border: none; padding: 4px; spacing: 4px; }
QToolButton { background: transparent; border-radius: 6px; padding: 6px 10px; }
QToolButton:hover { background: @line; }
QToolButton:checked { background: @line; color: @accent; }
QTabBar::tab { background: @panel; color: @dim; padding: 8px 16px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }
QLabel[role="section"] { color: @dim; font-weight: bold; padding-top: 12px; }
QLabel[role="value"] { color: @dim; }
QPushButton, QComboBox { background: @raised; border: 1px solid @line; border-radius: 6px; padding: 5px 10px; min-height: 18px; }
QPushButton:hover, QComboBox:hover { background: @line; }
QPushButton:focus, QComboBox:focus, QCheckBox:focus, QSlider:focus { border: 1px solid @accent; }
QComboBox QAbstractItemView { background: @raised; selection-background-color: @line; border: 1px solid @line; }
QSlider::groove:horizontal { height: 4px; background: @line; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @text; width: 14px; margin: -5px 0; border-radius: 7px; }
QCheckBox::indicator { width: 16px; height: 16px; border-radius: 4px; background: @raised; border: 1px solid @dim; }
QCheckBox::indicator:checked { background: @accent; border: 1px solid @accent; }
QScrollBar:vertical { background: @panel; width: 10px; }
QScrollBar::handle:vertical { background: @line; border-radius: 5px; min-height: 24px; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QMenu { background: @raised; border: 1px solid @line; }
QMenu::item { padding: 6px 20px; }
QMenu::item:selected { background: @line; }
QMenu::separator { height: 1px; background: @line; margin: 4px 0; }
)";

QString themedSheet(bool light) {
  C_BG = light ? QColor(0xe4, 0xdc, 0xcf) : QColor(0x10, 0x0d, 0x14);
  C_PANEL = light ? QColor(0xef, 0xe7, 0xda) : QColor(0x1a, 0x15, 0x20);
  C_RAISED = light ? QColor(0xf6, 0xf0, 0xe5) : QColor(0x24, 0x1c, 0x2b);
  C_LINE = light ? QColor(0xc9, 0xbc, 0xaa) : QColor(0x33, 0x29, 0x3b);
  C_TEXT = light ? QColor(0x2a, 0x22, 0x30) : QColor(0xf4, 0xec, 0xf1);
  C_DIM = light ? QColor(0x5e, 0x53, 0x61) : QColor(0xa9, 0x9d, 0xab);
  C_ACCENT = light ? QColor(0xb0, 0x2a, 0x33) : QColor(0xf2, 0x52, 0x59);
  QString css(STYLE_SHEET);
  css.replace("@bg", C_BG.name())
      .replace("@panel", C_PANEL.name())
      .replace("@raised", C_RAISED.name())
      .replace("@line", C_LINE.name());
  css.replace("@text", C_TEXT.name()).replace("@dim", C_DIM.name()).replace("@accent", C_ACCENT.name());
  return css;
}

struct Options {
    int scale = 2, frames = -1;
    float headTurn = -1, fisheye = -1;
    bool light = false, tray = false, pip = false, fullscreen = false, top = false, nearest = false,
         follow = true, wake = true, reset = false;
    std::string shot, tab;
    uint32_t seed = 0;
};

bool parse(int argc, char** argv, Options& o, bool& help) {
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto need = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        fprintf(stderr, "%s needs a value\n", name);
        return nullptr;
      }
      return argv[++i];
    };
    if (a == "--help" || a == "-h") {
      fputs(HELP_TEXT, stdout);
      help = true;
      return false;
    } else if (a == "--scale") {
      const char* v = need("--scale");
      if (!v) return false;
      o.scale = atoi(v) < 1 ? 1 : atoi(v);
    } else if (a == "--frames") {
      const char* v = need("--frames");
      if (!v) return false;
      o.frames = atoi(v);
    } else if (a == "--seed") {
      const char* v = need("--seed");
      if (!v) return false;
      o.seed = (uint32_t)strtoul(v, nullptr, 10);
    } else if (a == "--shot") {
      const char* v = need("--shot");
      if (!v) return false;
      o.shot = v;
    } else if (a == "--tab") {
      const char* v = need("--tab");
      if (!v) return false;
      o.tab = v;
    } else if (a == "--fisheye") {
      const char* v = need("--fisheye");
      if (!v) return false;
      o.fisheye = (float)atof(v);
    } else if (a == "--head-turn") {
      const char* v = need("--head-turn");
      if (!v) return false;
      o.headTurn = (float)atof(v);
    } else if (a == "--pip")
      o.pip = true;
    else if (a == "--tray")
      o.tray = true;
    else if (a == "--light")
      o.light = true;
    else if (a == "--fullscreen")
      o.fullscreen = true;
    else if (a == "--top")
      o.top = true;
    else if (a == "--nearest")
      o.nearest = true;
    else if (a == "--no-follow")
      o.follow = false;
    else if (a == "--no-wake")
      o.wake = false;
    else if (a == "--reset")
      o.reset = true;
    else {
      fprintf(stderr, "unknown option %s (try --help)\n", a.c_str());
      return false;
    }
  }
  return true;
}

std::string prefDir() {
  char* p = SDL_GetPrefPath("teevee", "Teevee");
  std::string s = p ? p : "";
  if (p) SDL_free(p);
  return s;
}

struct PipPrefs {
    int x = 0, y = 0, w = 320, light = 0;
    bool placed = false;
    void load(const std::string& dir) {
      if (FILE* f = dir.empty() ? nullptr : fopen((dir + "/qt.cfg").c_str(), "r")) {
        int px, py, pw;
        if (fscanf(f, "pip=%d %d %d\n", &px, &py, &pw) == 3)
          x = px, y = py, w = pw < 160 ? 160 : pw > 1280 ? 1280 : pw, placed = true;
        fscanf(f, "theme=%d", &light);
        fclose(f);
      }
    }
    void save(const std::string& dir) const {
      if (FILE* f = dir.empty() ? nullptr : fopen((dir + "/qt.cfg").c_str(), "w")) {
        fprintf(f, "pip=%d %d %d\ntheme=%d\n", x, y, w, light);
        fclose(f);
      }
    }
};

volatile sig_atomic_t stopAsked = 0;
void askStop(int) { stopAsked = 1; }

struct Robot {
    Lcd lcd;
    FaceRenderer renderer;
    Engine engine;
    KeyState keys;
    QImage frame{desktop::W, desktop::H, QImage::Format_ARGB32};
    QString toast;
    uint64_t toastUntil = 0;

    void say(const QString& text) {
      toast = text;
      toastUntil = SDL_GetTicks() + 1800;
    }
    void touch(bool down, float x, float y) {
      float fx, fy;
      renderer.screenToFace(x, y, fx, fy);
      engine.pointer(down, (int)fx, (int)fy);
    }
    void tick(uint64_t now) {
      engine.update((uint32_t)now);
      renderer.setStyle(engine.style());
      renderer.setGlitchDrive(engine.glitchDrive());
      renderer.setGlitchScale(engine.glitchScale());
      renderer.setPupilScale(engine.pupilScale());
      renderer.setTurn(engine.turnX(), engine.turnY(), engine.turnRoll());
      renderer.setFisheye(engine.fisheye());
      {
        const DayTint dt = engine.dayTint();
        renderer.setDayTint(dt.hue, dt.val);
      }
      renderer.render(engine.face(), engine.overlay(), (uint32_t)now, false);
      desktop::toArgb((uint32_t*)frame.bits());
    }
};

Robot& theRobot() {
  static Robot r;
  return r;
}

class FaceView : public QWidget {
  public:
    FaceView(Robot& r, bool nearest, bool follow, QWidget* parent = nullptr)
        : QWidget(parent), robot(r), smooth(!nearest), follow(follow) {
      setMouseTracking(true);
      setFocusPolicy(Qt::StrongFocus);
      setMinimumSize(160, 120);
      setAttribute(Qt::WA_OpaquePaintEvent);
    }
    std::function<bool(QKeyEvent*)> onKey;

  protected:
    Robot& robot;
    bool smooth, follow, pressed = false;

    QRectF picture() const {
      const double s = qMin(width() / (double)desktop::W, height() / (double)desktop::H);
      const double w = desktop::W * s, h = desktop::H * s;
      return QRectF((width() - w) / 2, (height() - h) / 2, w, h);
    }
    QPointF toScreen(const QPointF& p) const {
      const QRectF r = picture();
      return QPointF((p.x() - r.x()) * desktop::W / r.width(), (p.y() - r.y()) * desktop::H / r.height());
    }
    static float clampTo(double v, int hi) { return v < 0 ? 0.f : v >= hi ? (float)(hi - 1) : (float)v; }

    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.fillRect(rect(), C_BG);
      p.setRenderHint(QPainter::SmoothPixmapTransform, smooth);
      p.drawImage(picture(), robot.frame);
      if (!robot.toast.isEmpty() && SDL_GetTicks() < robot.toastUntil) {
        const QRectF r = picture();
        const QRectF bar(r.x(), r.bottom() - 28, r.width(), 28);
        QColor bg = C_PANEL;
        bg.setAlpha(215);
        p.fillRect(bar, bg);
        p.setPen(C_TEXT);
        p.drawText(bar, Qt::AlignCenter, robot.toast);
      }
    }
    void mousePressEvent(QMouseEvent* e) override {
      if (e->button() != Qt::LeftButton) return;
      const QPointF s = toScreen(e->position());
      if (s.x() < 0 || s.x() >= desktop::W || s.y() < 0 || s.y() >= desktop::H) return;
      pressed = true;
      robot.touch(true, (float)s.x(), (float)s.y());
    }
    void mouseMoveEvent(QMouseEvent* e) override {
      const QPointF s = toScreen(e->position());
      const float x = clampTo(s.x(), desktop::W), y = clampTo(s.y(), desktop::H);
      if (pressed)
        robot.touch(true, x, y);
      else if (follow)
        robot.engine.gaze((x - 160) / 160.f * 0.8f, (y - 120) / 120.f * 0.8f);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
      if (e->button() != Qt::LeftButton || !pressed) return;
      pressed = false;
      const QPointF s = toScreen(e->position());
      robot.touch(false, clampTo(s.x(), desktop::W), clampTo(s.y(), desktop::H));
    }
    bool focusNextPrevChild(bool) override { return false; }
    void keyPressEvent(QKeyEvent* e) override {
      robot.engine.notePresence();
      if (e->isAutoRepeat()) return;
      if (onKey && onKey(e)) return;
      const bool shift = (e->modifiers() & Qt::ShiftModifier) != 0;
      if (e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        QWidget::keyPressEvent(e);
        return;
      }
      int k = -1;
      if (e->key() == Qt::Key_Tab || e->key() == Qt::Key_Backtab)
        k = '\t';
      else if (e->key() == Qt::Key_Backspace)
        k = 8;
      else if (e->key() >= 0x20 && e->key() < 0x7f)
        k = shift || e->key() < 'A' || e->key() > 'Z' ? e->key() : e->key() + 32;
      std::string msg;
      if (k >= 0 && applyKey(robot.engine, robot.keys, k, shift || e->key() == Qt::Key_Backtab, msg))
        robot.say(QString::fromStdString(msg));
      else
        QWidget::keyPressEvent(e);
    }
};

class PipWindow : public FaceView {
  public:
    PipWindow(Robot& r, bool nearest, bool follow) : FaceView(r, nearest, follow) {
      setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
      setAttribute(Qt::WA_MacAlwaysShowToolWindow);
      setWindowTitle("TeeVee");
      setMinimumSize(MIN_W, MIN_W * 3 / 4);
    }
    std::function<void()> onBack, onSettings, onChanged, onQuit;
    bool quitting = false;
    static constexpr int MIN_W = 160, MAX_W = 1280;

    void sizeTo(int w) {
      w = w < MIN_W ? MIN_W : w > MAX_W ? MAX_W : w;
      resize(w, w * 3 / 4);
      keepOnScreen();
      if (onChanged) onChanged();
    }
    void keepOnScreen() {
      QScreen* s = QGuiApplication::screenAt(frameGeometry().center());
      if (!s) s = QGuiApplication::primaryScreen();
      if (!s) return;
      const QRect a = s->availableGeometry();
      int x = pos().x(), y = pos().y();
      x = x + width() > a.right() + 1 ? a.right() + 1 - width() : x;
      y = y + height() > a.bottom() + 1 ? a.bottom() + 1 - height() : y;
      x = x < a.left() ? a.left() : x;
      y = y < a.top() ? a.top() : y;
      move(x, y);
    }

  protected:
    QRect backButton() const { return QRect(width() - 36, 8, 28, 28); }
    bool hovering = false, rightDown = false, moved = false;
    QPoint rightAt, windowAt;

    void enterEvent(QEnterEvent*) override {
      hovering = true;
      update();
    }
    void leaveEvent(QEvent*) override {
      hovering = false;
      update();
    }
    void paintEvent(QPaintEvent* e) override {
      FaceView::paintEvent(e);
      if (!hovering) return;
      QPainter p(this);
      p.setRenderHint(QPainter::Antialiasing);
      const QRect b = backButton();
      QColor bg = C_RAISED;
      bg.setAlpha(230);
      p.setBrush(bg);
      p.setPen(QPen(C_LINE, 1));
      p.drawRoundedRect(b, 6, 6);
      p.setPen(QPen(C_TEXT, 1.6));
      p.setBrush(Qt::NoBrush);
      p.drawRect(b.adjusted(8, 8, -8, -8));
      p.setBrush(C_TEXT);
      p.setPen(Qt::NoPen);
      p.drawRect(QRect(b.left() + 8, b.top() + 8, 12, 3));
    }
    void mousePressEvent(QMouseEvent* e) override {
      if (e->button() == Qt::LeftButton && backButton().contains(e->position().toPoint())) {
        if (onBack) onBack();
        return;
      }
      if (e->button() == Qt::RightButton) {
        rightDown = true, moved = false;
        rightAt = e->globalPosition().toPoint(), windowAt = pos();
        return;
      }
      FaceView::mousePressEvent(e);
    }
    void mouseMoveEvent(QMouseEvent* e) override {
      if (rightDown) {
        const QPoint d = e->globalPosition().toPoint() - rightAt;
        if (!moved && d.manhattanLength() < 5) return;
        if (!moved && windowHandle() && windowHandle()->startSystemMove()) {
          rightDown = false;
          return;
        }
        moved = true;
        move(windowAt + d);
        return;
      }
      FaceView::mouseMoveEvent(e);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
      if (e->button() == Qt::RightButton) {
        const bool click = rightDown && !moved;
        rightDown = false;
        if (click) menu(e->globalPosition().toPoint());
        return;
      }
      FaceView::mouseReleaseEvent(e);
    }
    void moveEvent(QMoveEvent*) override {
      if (onChanged) onChanged();
    }
    void closeEvent(QCloseEvent* e)
        override {  // the desktop closing it (Alt+F4) must not leave a program with no window; a refused close also stops a quit
      if (quitting) return;
      e->ignore();
      if (onBack) onBack();
    }
    void wheelEvent(QWheelEvent* e) override {
      const int d = e->angleDelta().y();
      if (d) sizeTo((int)(width() * (d > 0 ? 1.15f : 1 / 1.15f)));
    }
    void menu(const QPoint& at) {
      QMenu m(this);
      m.addAction("Back to the full window", [this] {
        if (onBack) onBack();
      });
      m.addAction("Look and settings", [this] {
        if (onSettings) onSettings();
      });
      m.addSeparator();
      m.addAction(strcmp(robot.engine.mind(), "asleep") == 0 ? "Wake" : "Sleep",
                  [this] { robot.engine.toggleSleep(); });
      QMenu* size = m.addMenu("Size");
      for (int w : {240, 320, 480, 640})
        size->addAction(QString("%1 x %2").arg(w).arg(w * 3 / 4), [this, w] { sizeTo(w); });
      m.addSeparator();
      m.addAction("Quit", [this] {
        if (onQuit) onQuit();
      });
      m.exec(at);
    }
};

class MindView : public QWidget {
  public:
    explicit MindView(Robot& r) : robot(r) { setMinimumHeight(26 + mind::OUT * 22 + 30); }

  protected:
    Robot& robot;
    void paintEvent(QPaintEvent*) override {
      QPainter p(this);
      p.fillRect(rect(), C_PANEL);
      const MindNet& net = robot.engine.net();
      const QString head = QString("%1 learned · wears %2 %3")
                               .arg(net.weights().updates)
                               .arg(robot.engine.lookName())
                               .arg(robot.engine.lookScore(), 0, 'f', 2);
      p.setPen(C_DIM);
      p.drawText(QRect(8, 0, width() - 16, 24), Qt::AlignVCenter, head);
      char liked[200];
      robot.engine.tastes().summary(liked, sizeof liked);
      p.setPen(C_DIM);
      p.drawText(QRect(8, 26 + mind::OUT * 22, width() - 16, 22), Qt::AlignVCenter,
                 p.fontMetrics().elidedText(liked, Qt::ElideRight, width() - 16));
      const int nameW = 86, barW = qMax(48, (width() - nameW - 24) / 3), barX = 8 + nameW,
                reasonX = barX + barW + 8;
      for (int u = 0; u < mind::OUT; u++) {
        const int y = 26 + u * 22;
        p.setPen(C_TEXT);
        p.drawText(QRect(8, y, nameW - 4, 20), Qt::AlignVCenter, mind::URGE_NAMES[u]);
        p.fillRect(barX, y + 8, barW, 4, C_LINE);
        const float v = net.urge(u);
        const int w = (int)(fabsf(v) * barW / 2);
        p.fillRect(v >= 0 ? barX + barW / 2 : barX + barW / 2 - w, y + 7, w, 6, v >= 0 ? C_ACCENT : C_DIM);
        MindNet::Reason why[1], lr[1];
        net.explain(u, why, 1);
        const bool learned = net.learned(u, lr, 1) && fabsf(lr[0].amount) >= 0.1f;
        p.setPen(learned ? C_TEXT : C_DIM);
        const QString reason = learned ? QString("+ %1").arg(mindReasonName(lr[0].input))
                                       : QString(mindReasonName(why[0].input));
        p.drawText(QRect(reasonX, y, width() - reasonX - 8, 20), Qt::AlignVCenter,
                   p.fontMetrics().elidedText(reason, Qt::ElideRight, width() - reasonX - 8));
      }
    }
};

// Combo boxes and sliders in a scrolling list must not change when the wheel passes over them on its way through the list.
class WheelGuard : public QObject {
  public:
    using QObject::QObject;
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() == QEvent::Wheel && !static_cast<QWidget*>(o)->hasFocus()) {
        e->ignore();
        return true;
      }
      return false;
    }
};

class LookPanel : public QScrollArea {
  public:
    LookPanel(Robot& r, QWidget* faceToFocus) : robot(r), face(faceToFocus), guard(new WheelGuard(this)) {
      setWidgetResizable(true);
      setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
      auto* page = new QWidget;
      grid = new QGridLayout(page);
      grid->setContentsMargins(12, 4, 12, 12);
      grid->setHorizontalSpacing(8);
      grid->setVerticalSpacing(6);
      grid->setColumnStretch(1, 1);
      build();
      grid->setRowStretch(rowAt, 1);
      setWidget(page);
    }
    void refresh() {
      for (auto& s : syncs) s();
    }

  private:
    Robot& robot;
    QWidget* face;
    WheelGuard* guard;
    QGridLayout* grid;
    int rowAt = 0;
    std::vector<std::function<void()>> syncs;

    void section(const QString& name) {
      auto* l = new QLabel(name.toUpper());
      l->setProperty("role", "section");
      grid->addWidget(l, rowAt++, 0, 1, 3);
    }
    void row(const QString& label, QWidget* control, QWidget* value = nullptr) {
      grid->addWidget(new QLabel(label), rowAt, 0);
      grid->addWidget(control, rowAt, 1, 1, value ? 1 : 2);
      if (value) grid->addWidget(value, rowAt, 2);
      rowAt++;
      control->installEventFilter(guard);
      control->setFocusPolicy(Qt::StrongFocus);
    }
    void slider(const QString& label, float lo, float hi, int steps, std::function<float()> get,
                std::function<void(float)> set, std::function<QString(float)> show) {
      auto* s = new QSlider(Qt::Horizontal);
      s->setRange(0, steps);
      s->setPageStep(qMax(1, steps / 10));
      auto* v = new QLabel;
      v->setProperty("role", "value");
      v->setMinimumWidth(48);
      v->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      row(label, s, v);
      connect(s, &QSlider::valueChanged, this, [=](int i) {
        const float x = lo + (hi - lo) * i / steps;
        set(x);
        v->setText(show(x));
      });
      connect(s, &QSlider::sliderReleased, this, [this] { face->setFocus(); });
      syncs.push_back([=] {
        const float x = get();
        if (s->isSliderDown()) return;
        s->blockSignals(true);
        s->setValue((int)lround((x - lo) / (hi - lo) * steps));
        s->blockSignals(false);
        v->setText(show(x));
      });
    }
    void toggle(const QString& label, std::function<bool()> get, std::function<void(bool)> set) {
      auto* c = new QCheckBox;
      row(label, c);
      connect(c, &QCheckBox::clicked, this, [=](bool on) {
        set(on);
        face->setFocus();
      });
      syncs.push_back([=] { c->setChecked(get()); });
    }
    void choice(const QString& label, const QStringList& items, std::function<int()> get,
                std::function<void(int)> set) {
      auto* c = new QComboBox;
      c->addItems(items);
      c->setMaxVisibleItems(16);
      row(label, c);
      connect(c, &QComboBox::activated, this, [=](int i) {
        set(i);
        face->setFocus();
      });
      syncs.push_back([=] {
        if (c->view() && c->view()->isVisible()) return;
        c->setCurrentIndex(get());
      });
    }
    const uint8_t* field(const StyleDef& d) const { return (const uint8_t*)&robot.engine.style() + d.offset; }
    void lookText(const char* key, const QString& value) {
      robot.engine.styleText(QString("%1 %2").arg(key, value).toUtf8().constData());
    }

    void build() {
      QStringList presets;
      for (int i = 0; i < STYLE_PRESET_COUNT; i++) presets << STYLE_PRESETS[i].name;
      choice(
          "Look", presets, [this] { return robot.engine.styleIndex(); },
          [this](int i) { robot.engine.setStyle(i); });
      auto* random = new QWidget;
      auto* rl = new QHBoxLayout(random);
      rl->setContentsMargins(0, 0, 0, 0);
      auto *part = new QPushButton("Randomize a part"), *all = new QPushButton("Randomize everything");
      rl->addWidget(part);
      rl->addWidget(all);
      grid->addWidget(random, rowAt++, 0, 1, 3);
      connect(part, &QPushButton::clicked, this, [this] {
        robot.engine.randomLook(false);
        face->setFocus();
      });
      connect(all, &QPushButton::clicked, this, [this] {
        robot.engine.randomLook(true);
        face->setFocus();
      });
      auto* season = new QPushButton("Look of the season");
      grid->addWidget(season, rowAt++, 0, 1, 3);
      connect(season, &QPushButton::clicked, this, [this] {
        robot.say(robot.engine.seasonLook());
        face->setFocus();
      });

      const char* shown = nullptr;
      char name[48];
      for (int i = 0; i < STYLE_DEF_COUNT; i++) {
        const char* sec = settingsSection(i);
        if (sec != shown) section(sec), shown = sec;
        settingsLabel(i, name, sizeof name);
        const StyleDef& d = STYLE_DEFS[rowToDef(i)];
        const char* key = d.key;
        if (d.type == SF_FLOAT) {
          const float lo = d.min, hi = d.max, range = hi - lo;
          const int steps = range >= 8 ? (int)range : 100;
          slider(
              name, lo, hi, steps, [this, &d] { return *(const float*)field(d); },
              [this, key](float v) { lookText(key, QString::number(v, 'g', 4)); },
              [range](float v) { return QString::number(v, 'f', range >= 8 ? 0 : 2); });
        } else if (d.type == SF_BOOL) {
          toggle(
              name, [this, &d] { return *field(d) != 0; },
              [this, key](bool on) { lookText(key, on ? "1" : "0"); });
        } else if (d.type == SF_ENUM) {
          QStringList items;
          char item[32];
          for (int k = 0; styleEnumName(d.options, k, item, sizeof item); k++) items << item;
          choice(
              name, items, [this, &d] { return (int)*field(d); },
              [this, key, items](int k) { lookText(key, items[k]); });
        } else if (d.type == SF_RGB) {
          auto* b = new QPushButton;
          row(name, b);
          connect(b, &QPushButton::clicked, this, [this, b, &d, key] {
            const uint8_t* c = field(d);
            const QColor now = QColorDialog::getColor(QColor(c[0], c[1], c[2]), this, "Background");
            if (now.isValid())
              lookText(key, QString("%1 %2 %3").arg(now.red()).arg(now.green()).arg(now.blue()));
            face->setFocus();
          });
          syncs.push_back([this, b, &d] {
            const uint8_t* c = field(d);
            const QString text = QColor(c[0], c[1], c[2]).name();
            if (b->text() == text) return;
            b->setText(text);
            b->setStyleSheet(QString("background: %1; color: %2;")
                                 .arg(text, c[0] + c[1] + c[2] > 380 ? "#100d14" : "#f4ecf1"));
          });
        }
      }

      section("View");
      slider(
          "Head turn", 0, 35, 35, [this] { return robot.engine.headTurnDeg(); },
          [this](float v) { robot.engine.setHeadTurn(v); },
          [](float v) { return v > 0 ? QString("%1°").arg((int)lround(v)) : QString("off"); });
      slider(
          "Fisheye lens", 0, 1, 20, [this] { return robot.engine.fisheyeMax(); },
          [this](float v) { robot.engine.setFisheye(v); },
          [](float v) { return v > 0 ? QString("%1%").arg((int)lround(v * 100)) : QString("off"); });

      section("Character");
      QStringList who;
      for (const char* n : PERSONALITY_NAMES) who << n;
      choice(
          "Personality", who, [this] { return robot.engine.personality() % 5; },
          [this](int i) { robot.engine.setPersonality(i); });
      toggle(
          "Own look changes", [this] { return robot.engine.wardrobe(); },
          [this](bool on) { robot.engine.setWardrobe(on); });
      auto percent = [](float v) { return QString("%1%").arg((int)lround(v * 100)); };
      slider(
          "Chance of changing look", 0, 1, 20, [this] { return robot.engine.lookChance(); },
          [this](float v) { robot.engine.setLookChange(v, robot.engine.lookWhole()); }, percent);
      slider(
          "Whole look (or a part)", 0, 1, 20, [this] { return robot.engine.lookWhole(); },
          [this](float v) { robot.engine.setLookChange(robot.engine.lookChance(), v); }, percent);
      toggle(
          "Look at the screen", [this] { return robot.engine.screenSense(); },
          [this](bool on) { robot.engine.setScreenSense(on); });
    }
};

QWidget* makePlayTab(Robot& r, QWidget* face, QObject* guardParent) {
  auto* page = new QWidget;
  auto* v = new QVBoxLayout(page);
  v->setContentsMargins(12, 4, 12, 12);
  v->setSpacing(8);
  auto section = [&](const char* name) {
    auto* l = new QLabel(QString(name).toUpper());
    l->setProperty("role", "section");
    v->addWidget(l);
  };
  section("Do something");
  struct Ev {
      const char* name;
      uint8_t type;
  };
  static const Ev EVENTS[] = {{"Pet", EV_PET},     {"Poke", EV_POKE},       {"Praise", EV_PRAISE},
                              {"Scold", EV_SCOLD}, {"Startle", EV_STARTLE}, {"Win", EV_WIN},
                              {"Fail", EV_FAIL},   {"Greet", EV_GREET},     {"Tired", EV_TIRED},
                              {"Annoy", EV_ANNOY}, {"Love", EV_LOVE},       {"Confuse", EV_CONFUSE},
                              {"Bore", EV_BORE}};
  auto* g = new QGridLayout;
  g->setSpacing(6);
  int n = 0;
  for (const Ev& e : EVENTS) {
    auto* b = new QPushButton(e.name);
    QObject::connect(b, &QPushButton::clicked, page, [&r, face, type = e.type, name = e.name] {
      r.engine.event(type);
      r.say(QString(name).toLower());
      face->setFocus();
    });
    g->addWidget(b, n / 3, n % 3);
    n++;
  }
  v->addLayout(g);
  auto* sleep = new QPushButton("Sleep / wake");
  QObject::connect(sleep, &QPushButton::clicked, page, [&r, face] {
    r.engine.toggleSleep();
    face->setFocus();
  });
  v->addWidget(sleep);

  auto* guard = new WheelGuard(guardParent);
  section("Emote");
  auto* emotes = new QComboBox;
  emotes->installEventFilter(guard);
  emotes->setMaxVisibleItems(16);
  emotes->setToolTip("Plays when picked");
  for (int i = 1; i < EM_COUNT; i++) emotes->addItem(EMOTE_NAMES[i]);
  auto* play = new QPushButton("Play");
  auto* el = new QHBoxLayout;
  el->addWidget(emotes, 1);
  el->addWidget(play);
  v->addLayout(el);
  QObject::connect(play, &QPushButton::clicked, page, [&r, face, emotes] {
    r.engine.playEmote(emotes->currentIndex() + 1);
    face->setFocus();
  });
  QObject::connect(emotes, &QComboBox::activated, page, [&r, face](int i) {
    r.engine.playEmote(i + 1);
    face->setFocus();
  });

  section("Face");
  auto* faces = new QComboBox;
  faces->installEventFilter(guard);
  faces->setMaxVisibleItems(16);
  faces->addItem("follow the mood");
  for (int i = 0; i < A_COUNT; i++) faces->addItem(ANCHOR_NAMES[i]);
  v->addWidget(faces);
  QObject::connect(faces, &QComboBox::activated, page, [&r, face](int i) {
    r.keys.heldFace = i - 1;
    if (i == 0)
      r.engine.autoFace();
    else
      r.engine.showFace(i - 1);
    face->setFocus();
  });
  auto* sync = new QTimer(page);
  QObject::connect(sync, &QTimer::timeout, faces, [&r, faces] {
    if (!faces->view()->isVisible()) faces->setCurrentIndex(r.engine.heldFace() + 1);
  });
  sync->start(250);
  v->addStretch(1);
  return page;
}

class WinFilter : public QObject {
  public:
    QWidget* face;
    bool keepInTray = false;
    explicit WinFilter(QWidget* f) : face(f) {}
    bool eventFilter(QObject* o, QEvent* e) override {
      if (e->type() == QEvent::Close) {
        if (keepInTray) {
          static_cast<QWidget*>(o)->hide();
          e->ignore();
          return true;
        }
        QApplication::quit();
      }
      if (e->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
        face->setFocus();
        return true;
      }
      return false;
    }
};

QString moodLine(Robot& r) {
  return QString("%1  ·  %2  ·  %3")
      .arg(r.engine.mind(), STYLE_PRESETS[r.engine.styleIndex()].name,
           PERSONALITY_NAMES[r.engine.personality() % 5]);
}
}  // namespace

int main(int argc, char** argv) {
  Options opt;
  bool wantHelp = false;
  if (!parse(argc, argv, opt, wantHelp)) return wantHelp ? 0 : 2;
#if defined(__linux__)
  if (!getenv("QT_QPA_PLATFORM")) setenv("QT_QPA_PLATFORM", "xcb;wayland", 1);
#endif
  QApplication app(argc, argv);
  QApplication::setApplicationName("TeeVee");
  QApplication::setStyle(QStyleFactory::create("Fusion"));
  QApplication::setQuitOnLastWindowClosed(false);
  signal(SIGINT, askStop);
  signal(SIGTERM, askStop);

  Robot& robot = theRobot();
  Engine& engine = robot.engine;
  robot.renderer.begin(&robot.lcd);
  const uint32_t seed = opt.seed ? opt.seed : (uint32_t)time(nullptr) * 2654435761u;
  const std::string dir = prefDir();
  engine.begin(seed, (uint32_t)SDL_GetTicks(), opt.wake);
  const std::string cfg = dir.empty() ? std::string() : dir + "/teevee.cfg";
  FILE* saved = cfg.empty() ? nullptr : fopen(cfg.c_str(), "rb");
  const bool firstStart = !saved;
  if (saved) fclose(saved);
  if (opt.reset || firstStart) engine.newRobot(seed);
  if (!opt.reset && !dir.empty()) engine.load(dir.c_str());
  if (opt.headTurn >= 0) engine.setHeadTurn(opt.headTurn);
  if (opt.fisheye >= 0) engine.setFisheye(opt.fisheye);
  robot.tick(SDL_GetTicks());

  //==============================
  // the full window
  //==============================
  QMainWindow win;
  win.setWindowTitle("TeeVee");
  auto* face = new FaceView(robot, opt.nearest, opt.follow);
  auto* tabs = new QTabWidget;
  auto* look = new LookPanel(robot, face);
  auto* mindScroll = new QScrollArea;
  auto* mindView = new MindView(robot);
  mindScroll->setWidgetResizable(true);
  mindScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  mindScroll->setWidget(mindView);
  auto* keysLabel = new QLabel(KEYS_TEXT);
  keysLabel->setContentsMargins(12, 12, 12, 12);
  keysLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  keysLabel->setWordWrap(true);
  auto* keysScroll = new QScrollArea;
  keysScroll->setWidgetResizable(true);
  keysScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  keysScroll->setWidget(keysLabel);
  tabs->addTab(look, "Look");
  tabs->addTab(makePlayTab(robot, face, &win), "Play");
  tabs->addTab(mindScroll, "Mind");
  tabs->addTab(keysScroll, "Keys");
  tabs->setMinimumWidth(340);
  auto* split = new QSplitter;
  split->addWidget(face);
  split->addWidget(tabs);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 0);
  split->setChildrenCollapsible(false);
  win.setCentralWidget(split);
  auto* status = new QLabel;
  status->setProperty("role", "value");
  status->setContentsMargins(8, 2, 8, 2);
  auto* bar = new QToolBar;
  bar->setMovable(false);
  bar->setFloatable(false);
  bar->setContextMenuPolicy(Qt::PreventContextMenu);
  win.addToolBar(Qt::TopToolBarArea, bar);
  win.resize(desktop::W * opt.scale + 380, qMax(desktop::H * opt.scale + 44, 560));
  face->setFocus();

  //==============================
  // picture in picture
  //==============================
  PipPrefs prefs;
  prefs.load(dir);
  if (opt.light) prefs.light = 1;
  app.setStyleSheet(themedSheet(prefs.light != 0));
  PipWindow pip(robot, opt.nearest, opt.follow);
  pip.resize(prefs.w, prefs.w * 3 / 4);
  bool pipReady =
      false;  // (the window's own first placement is not the owner's choice: nothing is saved until it is up)
  bool pipEverShown = false;
  auto savePip = [&] {
    if (!pipReady || !pip.isVisible()) return;
    prefs.x = pip.x(), prefs.y = pip.y(), prefs.w = pip.width();
    prefs.save(dir);
  };
  pip.onChanged = savePip;
  QAction* pipAction = nullptr;
  auto showPip = [&](bool on) {
    if (pipAction) pipAction->setChecked(on);
    if (on == pip.isVisible()) return;
    if (on) {
      pipReady = false;
      if (prefs.placed || pipEverShown) {
        pip.move(prefs.x, prefs.y);
      } else if (QScreen* s = QGuiApplication::primaryScreen()) {
        const QRect a = s->availableGeometry();
        pip.move(a.right() - pip.width() - 16, a.bottom() - pip.height() - 16);
      }
      pip.show();
      pip.keepOnScreen();
      pip.raise();
      pip.activateWindow();
      pip.setFocus();
      win.hide();
      pipEverShown = true;
      pipReady = true;
    } else {
      savePip();
      win.show();
      win.raise();
      win.activateWindow();
      pip.hide();
      face->setFocus();
    }
  };
  pip.onBack = [&] { showPip(false); };
  pip.onSettings = [&] {
    showPip(false);
    tabs->setCurrentIndex(0);
  };
  WinFilter* filterPtr = nullptr;
  auto quit = [&] {
    pip.quitting = true;
    if (filterPtr) filterPtr->keepInTray = false;
    QApplication::quit();
  };
  pip.onQuit = quit;

  //==============================
  // the bar and the window's keys
  //==============================
  pipAction = bar->addAction("Picture in picture");
  pipAction->setCheckable(true);
  pipAction->setToolTip("A small window on top of everything (F3)");
  QObject::connect(pipAction, &QAction::triggered, &win, [&](bool on) { showPip(on); });
  QAction* topAction = bar->addAction("On top");
  topAction->setCheckable(true);
  topAction->setToolTip("Keep this window on top (T)");
  auto setTop = [&](bool on) {
    const bool wasVisible = win.isVisible();
    topAction->setChecked(on);
    win.setWindowFlag(Qt::WindowStaysOnTopHint, on);
    if (wasVisible) win.show();
  };
  QObject::connect(topAction, &QAction::triggered, &win, [&](bool on) {
    setTop(on);
    robot.say(on ? "always on top" : "normal window");
  });
  QAction* fullAction = bar->addAction("Fullscreen");
  fullAction->setCheckable(true);
  fullAction->setToolTip("Only the face, on the whole screen (F11, Esc leaves)");
  auto setFull = [&](bool on) {
    fullAction->setChecked(on);
    if (on) {
      tabs->hide();
      bar->hide();
      win.showFullScreen();
    } else {
      tabs->show();
      bar->show();
      win.showNormal();
    }
    face->setFocus();
  };
  QObject::connect(fullAction, &QAction::triggered, &win, [&](bool on) { setFull(on); });
  QAction* panelAction = bar->addAction("Controls");
  panelAction->setCheckable(true);
  panelAction->setChecked(true);
  panelAction->setToolTip("Show or hide the controls beside the face (F2)");
  QObject::connect(panelAction, &QAction::triggered, &win, [&](bool on) {
    tabs->setVisible(on);
    face->setFocus();
  });
  QAction* lightAction = bar->addAction("Light");
  lightAction->setCheckable(true);
  lightAction->setChecked(prefs.light != 0);
  lightAction->setToolTip("A warm light theme, or back to the dark one");
  QObject::connect(lightAction, &QAction::triggered, &win, [&](bool on) {
    prefs.light = on;
    app.setStyleSheet(themedSheet(on));
    if (!prefs.placed) {
      if (QScreen* sc = QGuiApplication::primaryScreen())
        prefs.x = sc->availableGeometry().right() - pip.width() - 16,
        prefs.y = sc->availableGeometry().bottom() - pip.height() - 16;
    }
    prefs.save(dir);
    face->setFocus();
  });
  QAction* packAction = bar->addAction("Packs");
  packAction->setToolTip("Save or install a pack, or save a full backup");
  QObject::connect(packAction, &QAction::triggered, &win, [&] {
    QMenu m(&win);
    const auto save = [&](bool all, const char* suggested) {
      const QString path =
          QFileDialog::getSaveFileName(&win, all ? "Save a full backup (keep it private)" : "Save a pack",
                                       suggested, "TeeVee packs (*.tvpack)");
      if (path.isEmpty()) return;
      if (!packio::exportPack(engine, dir, path.toStdString(), all))
        QMessageBox::warning(&win, "TeeVee", "Could not write the file.");
    };
    m.addAction("Save a pack", [&] { save(false, "teevee.tvpack"); });
    m.addAction("Save a full backup", [&] { save(true, "teevee-backup.tvpack"); });
    m.addAction("Install a pack", [&] {
      const QString path =
          QFileDialog::getOpenFileName(&win, "Install a pack", QString(), "TeeVee packs (*.tvpack)");
      if (path.isEmpty()) return;
      const bool all =
          QMessageBox::question(
              &win, "TeeVee",
              "Also replace what it has learned, if this file holds that? This cannot be undone.",
              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
      std::string report;
      packio::importPack(engine, dir, path.toStdString(), all, report);
      QMessageBox::information(&win, "TeeVee", QString::fromStdString(report));
    });
    m.exec(QCursor::pos());
    face->setFocus();
  });
  auto* spacer = new QWidget;
  spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  bar->addWidget(spacer);
  bar->addWidget(status);

  auto windowKeys = [&](QKeyEvent* e) {
    const int k = e->key();
    const bool plain = !(e->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    auto showTab = [&](int i) {
      if (pip.isVisible()) showPip(false);
      panelAction->setChecked(true);
      tabs->setVisible(true);
      tabs->setCurrentIndex(i);
    };
    if (k == Qt::Key_F3) {
      showPip(!pip.isVisible());
      return true;
    }
    if (k == Qt::Key_F11 ||
        ((k == Qt::Key_Return || k == Qt::Key_Enter) && (e->modifiers() & Qt::AltModifier))) {
      if (!pip.isVisible()) setFull(!win.isFullScreen());
      return true;
    }
    if (k == Qt::Key_Escape) {
      if (pip.isVisible())
        showPip(false);
      else if (win.isFullScreen())
        setFull(false);
      else
        quit();
      return true;
    }
    if (k == Qt::Key_F2) {
      const bool on = pip.isVisible() || !tabs->isVisible();
      if (on)
        showTab(0);
      else {
        tabs->hide();
        panelAction->setChecked(false);
      }
      return true;
    }
    if (k == Qt::Key_F1 || (plain && k == Qt::Key_H)) {
      showTab(3);
      return true;
    }
    if (plain && k == Qt::Key_I) {
      showTab(2);
      return true;
    }
    if (plain && k == Qt::Key_T && !pip.isVisible()) {
      setTop(!topAction->isChecked());
      robot.say(topAction->isChecked() ? "always on top" : "normal window");
      return true;
    }
    return false;
  };
  face->onKey = windowKeys;
  pip.onKey = windowKeys;

  WinFilter winFilter(face);
  win.installEventFilter(&winFilter);
  filterPtr = &winFilter;

  //==============================
  // the tray (--tray; where the desktop has one)
  //==============================
  QSystemTrayIcon tray;
  QMenu trayMenu;
  if (opt.tray && QSystemTrayIcon::isSystemTrayAvailable()) {
    winFilter.keepInTray = true;
    tray.setToolTip("TeeVee");
    trayMenu.addAction("Show the window", &win, [&] {
      showPip(false);
      win.show();
      win.raise();
      win.activateWindow();
    });
    trayMenu.addAction("Picture in picture", &win, [&] { showPip(true); });
    if (pet::autostartSupported()) {
      QAction* start = trayMenu.addAction("Start with the computer");
      start->setCheckable(true);
      start->setChecked(pet::autostartOn());
      QObject::connect(start, &QAction::triggered, &win, [&, start](bool on) {
        if (!pet::setAutostart(on))
          start->setChecked(!on), robot.say("could not change starting with the computer");
      });
    }
    trayMenu.addSeparator();
    trayMenu.addAction("Quit", &win, [&] { quit(); });
    tray.setContextMenu(&trayMenu);
    tray.setIcon(QIcon(QPixmap::fromImage(robot.frame.scaled(64, 48))));
    QObject::connect(&tray, &QSystemTrayIcon::activated, &win, [&](QSystemTrayIcon::ActivationReason why) {
      if (why == QSystemTrayIcon::Trigger) {
        win.show();
        win.raise();
        win.activateWindow();
      }
    });
    tray.show();
  } else if (opt.tray) {
    fprintf(stderr, "no system tray on this desktop: closing the window quits\n");
  }

  QObject::connect(&app, &QApplication::aboutToQuit, [&] {
    if (!dir.empty()) engine.save(dir.c_str());
  });

  //==============================
  // the clock
  //==============================
  uint64_t lastSave = SDL_GetTicks(), lookAt = 0, screenAt = 0, powerAt = 0;
  float pmx = -1, pmy = -1, activity = 0;
  static uint8_t thumb[16 * 9], lastThumb[16 * 9];
  bool haveLast = false;
  int frame = 0;
  QTimer clock;
  clock.setTimerType(Qt::PreciseTimer);
  QObject::connect(&clock, &QTimer::timeout, &win, [&] {
    const uint64_t now = SDL_GetTicks();
    if (stopAsked) {
      quit();
      return;
    }
    const QPoint gp = QCursor::pos();
    const float mx = (float)gp.x(), my = (float)gp.y();
    if (pmx >= 0 && (mx != pmx || my != pmy)) {
      engine.notePresence();
      if (pip.isVisible() && opt.follow && !pip.underMouse()) {
        const QPoint c = pip.mapToGlobal(pip.rect().center());
        auto clamp1 = [](float v) { return v < -1 ? -1.f : v > 1 ? 1.f : v; };
        engine.gaze(clamp1((mx - c.x()) / 500.f) * 0.8f, clamp1((my - c.y()) / 350.f) * 0.8f);
      }
    }
    pmx = mx, pmy = my;
    float seen = 0;
    if (pip.isVisible())
      seen = pip.underMouse() ? 1.f : 0.3f;
    else if (win.isVisible() && !win.isMinimized())
      seen = face->underMouse() ? 1.f : win.isActiveWindow() ? 0.5f : 0.15f;
    engine.setWatched(seen);
    if (engine.screenSense() && now - screenAt > 2000) {
      screenAt = now;
      if (pet::screenThumb(thumb, 16, 9)) {
        unsigned sum = 0, diff = 0;
        for (int i = 0; i < 16 * 9; i++)
          sum += thumb[i], diff += haveLast ? (unsigned)abs(thumb[i] - lastThumb[i]) : 0;
        activity = 0.5f * activity + 0.5f * fminf(1.f, diff / (16.f * 9.f) / 24.f);
        memcpy(lastThumb, thumb, sizeof thumb);
        haveLast = true;
        engine.setScreen(sum / (16.f * 9.f * 255.f), activity);
      } else {
        engine.setScreen(-1, 0);
      }
    } else if (!engine.screenSense()) {
      haveLast = false, activity = 0;
    }
    if (!powerAt || now - powerAt > 30000) {
      powerAt = now;
      int pct = -1;
      const SDL_PowerState ps = SDL_GetPowerInfo(nullptr, &pct);
      engine.setBatteryLow(ps == SDL_POWERSTATE_ON_BATTERY && pct >= 0 ? (30 - pct) / 25.f : 0.f);
    }
    robot.tick(now);
    if (pip.isVisible())
      pip.update();
    else if (win.isVisible())
      face->update();
    if (now - lookAt > 250) {
      lookAt = now;
      if (tabs->isVisible()) {
        if (tabs->currentWidget() == look) look->refresh();
        status->setText(moodLine(robot));
      }
    }
    if (tabs->isVisible() && tabs->currentWidget() == mindScroll && frame % 12 == 0) mindView->update();
    if (engine.dirty() && now - lastSave > 30000 && !dir.empty()) {
      engine.save(dir.c_str());
      engine.clearDirty();
      lastSave = now;
    }
    frame++;
    if (opt.frames > 0 && frame >= opt.frames) {
      if (!opt.shot.empty()) {
        look->refresh();
        status->setText(moodLine(robot));
        QWidget* w = pip.isVisible() ? (QWidget*)&pip : (QWidget*)&win;
        if (!w->grab().save(QString::fromStdString(opt.shot)))
          fprintf(stderr, "could not write %s\n", opt.shot.c_str());
      }
      quit();
    }
  });

  look->refresh();
  status->setText(moodLine(robot));
  if (opt.tab == "play")
    tabs->setCurrentIndex(1);
  else if (opt.tab == "mind")
    tabs->setCurrentIndex(2);
  else if (opt.tab == "keys")
    tabs->setCurrentIndex(3);
  if (opt.top) setTop(true);
  if (opt.pip)
    showPip(true);
  else if (opt.fullscreen)
    setFull(true);
  else
    win.show();
  clock.start(16);
  return app.exec();
}
