// Touch controls for the Android build (see peggle2_touch.h for the gestures).
//
// Three parts:
//   1. SDL finger events (an event watch, on whichever thread SDL delivers them) are mapped into the game's 1280x720
//      frame through the same fit the renderer presents with (whole frame, centered, no trim: present.cpp FitFrame
//      with kMaxTrim 0 on Android) and queued.
//   2. A synthetic pad (merged into player 1 by the input system) drains the queue on the game thread, each time the
//      game reads its pad: it runs the gestures, presses the game's menus through its pointer path, and reports the
//      buttons the gestures hold (fire, speed up, pause, back, the map's left / right).
//   3. Game hooks: the aim update points the cannon at the finger; the pointer path's hover, press and MainWidget
//      lifetime are tracked; the tutorial and the area map say which screen is up.
// Function addresses and offsets are peggle2-mac's (MIT, Copyright (c) macprotips; runtime/game_hooks.cpp).

#include "peggle2_touch.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <jni.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>
#include <rex/logging.h>
#include <rex/logging/api.h>
#include <rex/system/thread_state.h>

REXCVAR_DEFINE_BOOL(peggle2_touch, true, "Peggle2/Touch", "Touch controls (drag to aim, tap to fire)");
REXCVAR_DEFINE_BOOL(peggle2_touch_log, false, "Peggle2/Touch", "Log each touch gesture and what it did");
REXCVAR_DEFINE_DOUBLE(peggle2_touch_aim_k, 0.0011, "Peggle2/Touch",
                      "The ball's arc for aiming at the finger: gravity / launch speed squared, in 1280x720 pixels "
                      "(peggle2-mac's measured value)");
REXCVAR_DECLARE(bool, audio_mute);
REXCVAR_DEFINE_DOUBLE(peggle2_touch_aim_muzzle, 80.0, "Peggle2/Touch",
                      "Distance from the cannon's pivot to where the ball leaves it, in 1280x720 pixels");

namespace {

using rex::X_RESULT;
using rex::X_STATUS;
using rex::input::DeviceId;
using rex::input::DeviceInfo;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;

constexpr DeviceId kTouchDevice = static_cast<DeviceId>(0x50325443);  // "P2TC"
constexpr float kFrameW = 1280.0f, kFrameH = 720.0f;

// Gesture thresholds, in frame pixels and milliseconds.
constexpr float kDragDistance = 22.0f;   // a finger that moved this far is a drag, not a tap
constexpr int64_t kTapMaxMs = 350;       // a tap is shorter than this
constexpr int64_t kHoldAimMs = 220;      // a finger held still this long in a level aims (a slow, precise drag)
constexpr float kSwipeDistance = 150.0f; // a sideways swipe this long on the map changes area
constexpr int64_t kPulseMs = 90;         // how long a tapped button is held (3 frames at 30 fps)

// The cannon's pivot in the frame (peggle2-mac: measured from the gun's clamped aim extremes).
constexpr float kGunPivotX = 741.0f, kGunPivotY = 45.0f;

// Pad buttons (XINPUT_GAMEPAD_*).
constexpr uint16_t kDpadDown = 0x0002, kDpadLeft = 0x0004, kDpadRight = 0x0008, kStart = 0x0010, kButtonA = 0x1000, kButtonB = 0x2000,
                   kButtonX = 0x4000;

int64_t NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
      .count();
}

// Guest memory (big-endian).
uint8_t* g_base = nullptr;
uint32_t Load32(uint8_t* base, uint32_t address) {
  uint32_t v;
  std::memcpy(&v, base + address, 4);
  return __builtin_bswap32(v);
}
float LoadF32(uint8_t* base, uint32_t address) {
  const uint32_t v = Load32(base, address);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}
void StoreF32(uint8_t* base, uint32_t address, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = __builtin_bswap32(v);
  std::memcpy(base + address, &v, 4);
}

// ---------------------------------------------------------------------------------------------------------------
// 1. Finger events.

enum class Kind : uint8_t { kDown, kMove, kUp, kBack };
struct TouchEvent {
  Kind kind;
  uint64_t finger;
  float x, y;  // in the frame (may lie outside it, on the bars)
  int64_t ms;
};

std::mutex g_queue_mutex;
std::deque<TouchEvent> g_queue;

void Push(const TouchEvent& ev) {
  std::lock_guard lock(g_queue_mutex);
  if (g_queue.size() > 512)
    g_queue.pop_front();  // nobody drains (the game is paused in the background): keep the newest
  g_queue.push_back(ev);
}

// A point of the window (0..1 on each axis) in the frame, fitted the way present.cpp shows it.
bool ToFrame(SDL_WindowID window_id, float nx, float ny, float& fx, float& fy) {
  SDL_Window* window = SDL_GetWindowFromID(window_id);
  int w = 0, h = 0;
  if (!window || !SDL_GetWindowSizeInPixels(window, &w, &h) || w <= 0 || h <= 0)
    return false;
  const float scale = std::min(float(w) / kFrameW, float(h) / kFrameH);
  const float ox = (float(w) - kFrameW * scale) / 2, oy = (float(h) - kFrameH * scale) / 2;
  fx = (nx * float(w) - ox) / scale;
  fy = (ny * float(h) - oy) / scale;
  return true;
}

bool SDLCALL EventWatch(void*, SDL_Event* event) {
  switch (event->type) {
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
      float x, y;
      if (!ToFrame(event->tfinger.windowID, event->tfinger.x, event->tfinger.y, x, y))
        break;
      const Kind kind = event->type == SDL_EVENT_FINGER_DOWN     ? Kind::kDown
                        : event->type == SDL_EVENT_FINGER_MOTION ? Kind::kMove
                                                                 : Kind::kUp;
      Push({kind, uint64_t(event->tfinger.fingerID), x, y, NowMs()});
      break;
    }
    case SDL_EVENT_KEY_DOWN:
      if (event->key.scancode == SDL_SCANCODE_AC_BACK && !event->key.repeat)
        Push({Kind::kBack, 0, 0, 0, NowMs()});
      break;
    default:
      break;
  }
  return true;
}

// ---------------------------------------------------------------------------------------------------------------
// 3. What the game is showing (set by the hooks below, on the game thread).

std::atomic<int64_t> g_aim_update_ms{-1000000};  // the local player's aim update last ran (in a level)
std::atomic<int64_t> g_tutorial_ms{-1000000};    // a gamepad tutorial step was last waiting
std::atomic<int> g_tutorial_action{0};           // 0 aim / fire, 1 Start (PauseMenu), 2 X (SpeedUp)
std::atomic<uint32_t> g_main_widget{0};           // MainWidget (its Widget part at +4)
bool g_button_pressed = false;                    // a pointer press reached a button (game thread)

std::mutex g_hovered_mutex;
std::set<uint32_t> g_hovered;  // the SpatialButtonDeviceAdapters the pointer is over
constexpr uint32_t kSpatialButtonAdapterVtable = 0x820DACE4;

bool InLevel() { return NowMs() - g_aim_update_ms.load() < 250; }
bool InTutorial() { return NowMs() - g_tutorial_ms.load() < 250; }

// Adapters can go without a leave (their scene is freed): an entry counts while it is still an enabled adapter.
bool PointerOverButton(uint8_t* base) {
  std::lock_guard lock(g_hovered_mutex);
  for (auto it = g_hovered.begin(); it != g_hovered.end();) {
    if (Load32(base, *it) != kSpatialButtonAdapterVtable) {
      it = g_hovered.erase(it);
      continue;
    }
    if (base[*it + 8] == 1)
      return true;
    ++it;
  }
  return false;
}

// The aim target the gestures set (game thread), and the stick a tutorial step is steered with.
bool g_aim_valid = false;
float g_aim_x = 0, g_aim_y = 0;
std::atomic<int16_t> g_steer_lx{0};

// ---------------------------------------------------------------------------------------------------------------
// Aim along the ball's arc (peggle2-mac's BallisticAim): the ball leaves the muzzle at angle phi below horizontal
// and follows y = x tan(phi) + K x^2 (1 + tan^2 phi) / 2 (frame pixels, +y down). Of the two angles that reach the
// point take the direct one; a point the arc cannot reach is aimed at straight. The muzzle moves with the aim, so
// the solve repeats from the new muzzle.

void ArcDirection(float dx, float dy, float& ux, float& uy) {
  const float X = std::fabs(dx), Y = dy, K = float(REXCVAR_GET(peggle2_touch_aim_k));
  ux = dx;
  uy = dy;
  if (X > 1.0f && K > 0) {
    const float a = K * X * X / 2, disc = X * X - 4 * a * (a - Y);
    if (disc >= 0) {
      ux = std::copysign(1.0f, dx);
      uy = (-X + std::sqrt(disc)) / (2 * a);
    }
  }
  const float length = std::sqrt(ux * ux + uy * uy);
  ux /= length;
  uy /= length;
}

void BallisticAim(float& dx, float& dy) {
  const float muzzle = float(REXCVAR_GET(peggle2_touch_aim_muzzle));
  float ux, uy;
  ArcDirection(dx, dy, ux, uy);
  for (int i = 0; i < 3 && std::sqrt(dx * dx + dy * dy) > muzzle + 10; i++)
    ArcDirection(dx - ux * muzzle, dy - uy * muzzle, ux, uy);
  dx = ux;
  dy = uy;
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// Game hooks. Each is a strong definition of the recompiled function (a weak alias of __imp__sub_*), so every caller
// reaches it; __imp__ is the original.

extern "C" REX_FUNC(__imp__sub_8223BD10);
extern "C" REX_FUNC(__imp__sub_821A7398);
extern "C" REX_FUNC(__imp__sub_8229D2F8);
extern "C" REX_FUNC(__imp__sub_8223FEB8);
extern "C" REX_FUNC(__imp__sub_8223FF78);
extern "C" REX_FUNC(__imp__sub_8223FE88);
extern "C" REX_FUNC(__imp__sub_8226B8E0);
extern "C" REX_FUNC(__imp__sub_8226BB68);

// GamepadTutorialState::Update: a step is waiting while its state (+16) is neither -1 nor 3; +0x30 points to its
// name. Those steps wait for a pad input, so the gestures give the one the step asks for.
REX_HOOK_RAW(sub_8229D2F8) {
  const int32_t state = int32_t(Load32(base, ctx.r3.u32 + 16));
  if (state != -1 && state != 3) {
    g_tutorial_ms = NowMs();
    const uint32_t name = Load32(base, ctx.r3.u32 + 0x30);
    const std::string step = name ? reinterpret_cast<const char*>(base + name) : "";
    g_tutorial_action = step == "PauseMenu" ? 1 : step == "SpeedUp" ? 2 : 0;
  }
  __imp__sub_8229D2F8(ctx, base);
}

// BoardController's aim update for the local player (BoardController* in r3, its Gun* at +204; the gun's direction
// at Gun+536, board space, +y down; its state at +528: 0 idle, 2 loading, 3 ready, 4 firing). Gun::AimAt(Gun*,
// const Vector2*) is sub_821A7398. A loaded gun points along the arc through the finger. During a tutorial step the
// finger steers the stick instead, so the game moves the gun itself and the tutorial sees the input.
REX_HOOK_RAW(sub_8223BD10) {
  const uint32_t controller = ctx.r3.u32;
  __imp__sub_8223BD10(ctx, base);
  if (NowMs() - g_aim_update_ms.load() > 1000 && REXCVAR_GET(peggle2_touch_log))
    REXLOG_INFO("[touch] in a level (the aim update runs)");
  g_aim_update_ms = NowMs();
  if (!REXCVAR_GET(peggle2_touch) || !g_aim_valid) {
    g_steer_lx = 0;
    return;
  }
  const uint32_t gun = Load32(base, controller + 204);
  const uint32_t state = gun ? Load32(base, gun + 528) : 0;
  if (state != 2 && state != 3) {
    g_steer_lx = 0;
    return;
  }
  float dx = g_aim_x - kGunPivotX, dy = g_aim_y - kGunPivotY;
  if (std::sqrt(dx * dx + dy * dy) < 1.0f) {
    g_steer_lx = 0;
    return;
  }
  BallisticAim(dx, dy);
  if (InTutorial()) {
    // Angle from straight down, positive to the right; push the stick by the remaining error.
    const float error = std::atan2(dx, dy) - std::atan2(LoadF32(base, gun + 536), LoadF32(base, gun + 540));
    const float push = std::clamp(error * 4.0f, -1.0f, 1.0f);
    g_steer_lx = std::fabs(error) < 0.01f ? 0 : int16_t(push * 32767.0f);
    return;
  }
  g_steer_lx = 0;
  // The vector lives in the free stack space below the caller's frame.
  const uint32_t vector = ctx.r1.u32 - 0x40;
  StoreF32(base, vector, dx);
  StoreF32(base, vector + 4, dy);
  ctx.r3.u64 = gun;
  ctx.r4.u64 = vector;
  __imp__sub_821A7398(ctx, base);
}

// SpatialButtonDeviceAdapter: the pointer entered / left its button, and pressed it.
REX_HOOK_RAW(sub_8223FEB8) {
  {
    std::lock_guard lock(g_hovered_mutex);
    g_hovered.insert(ctx.r3.u32);
  }
  __imp__sub_8223FEB8(ctx, base);
}
REX_HOOK_RAW(sub_8223FF78) {
  {
    std::lock_guard lock(g_hovered_mutex);
    g_hovered.erase(ctx.r3.u32);
  }
  __imp__sub_8223FF78(ctx, base);
}
REX_HOOK_RAW(sub_8223FE88) {
  g_button_pressed = true;
  __imp__sub_8223FE88(ctx, base);
}

// MainWidget's constructor and destructor: the widget the pointer messages go to.
REX_HOOK_RAW(sub_8226B8E0) {
  const uint32_t self = ctx.r3.u32;
  __imp__sub_8226B8E0(ctx, base);
  g_main_widget = self;
}
REX_HOOK_RAW(sub_8226BB68) {
  uint32_t expected = ctx.r3.u32;
  g_main_widget.compare_exchange_strong(expected, 0);
  __imp__sub_8226BB68(ctx, base);
}

// PopCap kept SexyApp's mouse path: MainWidget's MouseMove / MouseDown / MouseUp(widget, x, y) turn into the UI's
// pointer messages, gated by a byte at app+1402. The Xbox build never calls them; the touches do, on the game thread.
REX_IMPORT(__imp__sub_8226C4A8, MainWidgetMouseMove, void(u32, u32, u32));
REX_IMPORT(__imp__sub_8226C348, MainWidgetMouseDown, void(u32, u32, u32));
REX_IMPORT(__imp__sub_8226C3F8, MainWidgetMouseUp, void(u32, u32, u32));

namespace {

constexpr uint32_t kAppPointer = 0x82D8D748;  // the global holding the SexyApp
constexpr uint32_t kPointerEnabledOffset = 1402;
constexpr uint32_t kBoundPlayerOffset = 4604;  // -1 until the title has bound a player

// ---------------------------------------------------------------------------------------------------------------
// 2. The gestures, on the game thread.

struct Gesture {
  bool active = false;
  uint64_t finger = 0;
  float x0 = 0, y0 = 0, x = 0, y = 0;
  int64_t t0 = 0;
  bool level = false;      // began in a level
  bool dragging = false;
  bool multi = false;      // a second finger came down meanwhile
  bool pointer = false;    // a menu press went down the pointer path
  bool click_is_a = false; // a menu tap over nothing presses A
};

struct PadState {
  Gesture g;
  int fingers = 0;
  bool speed_up = false;  // two fingers down in a level
  int64_t a_until = 0, b_until = 0, start_until = 0, x_until = 0, left_until = 0, right_until = 0;
  // Pad presses the pause menu schedules (one after another).
  struct Press {
    uint16_t buttons;
    int64_t from, to;
  };
  std::vector<Press> presses;
  uint16_t last_buttons = 0;
  int16_t last_lx = 0;
};
PadState g_pad;  // game thread only

void Log(const char* what, const Gesture& g) {
  if (REXCVAR_GET(peggle2_touch_log))
    REXLOG_INFO("[touch] {} at {:.0f},{:.0f} (from {:.0f},{:.0f}, {} ms): level {} drag {} multi {} pointer {} A {}",
                what, g.x, g.y, g.x0, g.y0, NowMs() - g.t0, g.level, g.dragging, g.multi, g.pointer, g.click_is_a);
}

uint32_t App(uint8_t* base) { return Load32(base, kAppPointer); }

uint64_t Coord(float v) { return uint64_t(int64_t(std::lround(v))); }

// The button a tap in a level gives: fire, or what a waiting tutorial step asks for.
void Fire(int64_t now) {
  PadState& p = g_pad;
  if (InTutorial()) {
    const int action = g_tutorial_action.load();
    if (action == 1) {
      p.start_until = now + kPulseMs;
      return;
    }
    if (action == 2) {
      p.x_until = now + kPulseMs * 4;
      return;
    }
  }
  p.a_until = now + kPulseMs;
}

void PointerDown(uint8_t* base, Gesture& g) {
  const uint32_t app = App(base), widget = g_main_widget.load();
  const bool owned = app && int32_t(Load32(base, app + kBoundPlayerOffset)) != -1;
  // Until a menu tap has pressed A once, taps press A: the title screen's PLAY binds the player to the pad that
  // pressed it, and a pointer press there (no pad) asks to sign in instead (the bound-player word above is not -1
  // on this build before that).
  static bool a_pressed_once = false;
  if (!a_pressed_once) {
    a_pressed_once = true;
    g.click_is_a = true;
    return;
  }
  if (!app || !widget || !owned) {
    g.click_is_a = true;  // before the title binds a player a pointer press would ask to sign in
    return;
  }
  base[app + kPointerEnabledOffset] = 1;
  const uint64_t ix = Coord(g.x), iy = Coord(g.y);
  // Hover is worked out only when the pointer moves: a one-pixel nudge catches buttons that became active under a
  // pointer that has not moved since.
  MainWidgetMouseMove(widget + 4, uint32_t(ix + 1), uint32_t(iy));
  MainWidgetMouseMove(widget + 4, uint32_t(ix), uint32_t(iy));
  g_button_pressed = false;
  MainWidgetMouseDown(widget + 4, uint32_t(ix), uint32_t(iy));
  g.pointer = true;
  g.click_is_a = !g_button_pressed && !PointerOverButton(base);
}

void HandleEvent(uint8_t* base, const TouchEvent& ev) {
  PadState& p = g_pad;
  Gesture& g = p.g;
  switch (ev.kind) {
    case Kind::kBack:
      if (InLevel())
        p.start_until = ev.ms + kPulseMs;  // pause
      else
        p.b_until = ev.ms + kPulseMs;
      return;
    case Kind::kDown:
      ++p.fingers;
      if (g.active) {
        g.multi = true;
        if (g.level)
          p.speed_up = true;
        return;
      }
      g = {};
      g.active = true;
      g.finger = ev.finger;
      g.x0 = g.x = ev.x;
      g.y0 = g.y = ev.y;
      g.t0 = ev.ms;
      g.level = InLevel();
      if (!g.level)
        PointerDown(base, g);
      Log("down", g);
      return;
    case Kind::kMove:
      if (!g.active || ev.finger != g.finger)
        return;
      g.x = ev.x;
      g.y = ev.y;
      if (!g.dragging && std::hypot(g.x - g.x0, g.y - g.y0) > kDragDistance)
        g.dragging = true;
      if (g.level && g.dragging && !g.multi) {
        g_aim_valid = true;
        g_aim_x = g.x;
        g_aim_y = g.y;
      }
      if (g.pointer) {
        if (const uint32_t widget = g_main_widget.load())
          MainWidgetMouseMove(widget + 4, uint32_t(Coord(g.x)), uint32_t(Coord(g.y)));
      }
      return;
    case Kind::kUp:
      p.fingers = std::max(0, p.fingers - 1);
      if (p.fingers == 0)
        p.speed_up = false;
      if (!g.active || ev.finger != g.finger)
        return;
      g.x = ev.x;
      g.y = ev.y;
      if (g.level) {
        if (!g.dragging && !g.multi && ev.ms - g.t0 < kTapMaxMs)
          Fire(ev.ms);
      } else {
        if (g.pointer) {
          if (const uint32_t widget = g_main_widget.load())
            MainWidgetMouseUp(widget + 4, uint32_t(Coord(g.x)), uint32_t(Coord(g.y)));
        }
        const float dx = g.x - g.x0, dy = g.y - g.y0;
        if (g.dragging && std::fabs(dx) > kSwipeDistance && std::fabs(dx) > 2 * std::fabs(dy)) {
          // The content follows the finger: a swipe to the left brings the next area (right) in.
          (dx < 0 ? p.right_until : p.left_until) = ev.ms + kPulseMs;
        } else if (!g.dragging && g.click_is_a && !g.multi) {
          p.a_until = ev.ms + kPulseMs;
        }
      }
      Log("up", g);
      g.active = false;
      return;
  }
}

// The activity's commands (Peggle2Activity.nativeMenu, on the UI thread), run on the game thread: its pause and
// sound buttons, the back button / gesture, and leaving the app.
enum MenuCommand { kMenuLeftApp = 0, kMenuMute = 4, kMenuUnmute = 5, kMenuPause = 6, kMenuBack = 7 };
std::mutex g_menu_mutex;
std::vector<int> g_menu_commands;

void Press(int64_t now, uint16_t buttons) { g_pad.presses.push_back({buttons, now, now + kPulseMs}); }

void RunMenuCommand(int command, int64_t now) {
  PadState& p = g_pad;
  switch (command) {
    case kMenuLeftApp:
      // Leaving the app: a level pauses (the game's own pause screen), a finger still down is let go.
      p.g.active = false;
      p.speed_up = false;
      if (InLevel())
        Press(now, kStart);
      break;
    case kMenuPause:
      Press(now, kStart);  // the game's pause screen (Resume, Restart, Help & Options, Quit)
      break;
    case kMenuBack:
      Press(now, InLevel() ? kStart : kButtonB);
      break;
    case kMenuMute:
    case kMenuUnmute:
      REXCVAR_SET(audio_mute, command == kMenuMute);
      REXLOG_INFO("[touch] sound {}", command == kMenuMute ? "off" : "on");
      break;
  }
}

// Drains the queue and returns the pad the gestures hold. Game thread.
void Update(uint8_t* base, uint16_t& buttons, int16_t& lx) {
  {
    std::vector<int> commands;
    {
      std::lock_guard lock(g_menu_mutex);
      commands.swap(g_menu_commands);
    }
    for (int c : commands)
      RunMenuCommand(c, NowMs());
  }
  std::deque<TouchEvent> events;
  {
    std::lock_guard lock(g_queue_mutex);
    events.swap(g_queue);
  }
  for (const TouchEvent& ev : events)
    HandleEvent(base, ev);

  PadState& p = g_pad;
  Gesture& g = p.g;
  const int64_t now = NowMs();
  // A finger held still in a level aims too (fine aim without moving first).
  if (g.active && g.level && !g.dragging && !g.multi && now - g.t0 > kHoldAimMs) {
    g.dragging = true;
    g_aim_valid = true;
    g_aim_x = g.x;
    g_aim_y = g.y;
  }
  // Leaving the level forgets the aim (the next board starts from the game's own).
  if (!InLevel() && !g.active)
    g_aim_valid = false;

  buttons = 0;
  for (const auto& press : p.presses)
    if (now >= press.from && now < press.to) buttons |= press.buttons;
  std::erase_if(p.presses, [now](const PadState::Press& press) { return now >= press.to; });
  if (now < p.a_until) buttons |= kButtonA;
  if (now < p.b_until) buttons |= kButtonB;
  if (now < p.start_until) buttons |= kStart;
  if (now < p.x_until || p.speed_up) buttons |= kButtonX;
  if (now < p.left_until) buttons |= kDpadLeft;
  if (now < p.right_until) buttons |= kDpadRight;
  lx = g_steer_lx.load();
}

class TouchInputDriver final : public rex::input::InputDriver {
 public:
  TouchInputDriver() : InputDriver(nullptr, 0) {}

  X_STATUS Setup() override {
    if (!SDL_AddEventWatch(EventWatch, nullptr)) {
      REXLOG_WARN("[touch] SDL_AddEventWatch failed: {}", SDL_GetError());
      return X_STATUS_UNSUCCESSFUL;
    }
    REXLOG_INFO("[touch] touch controls on: drag to aim, tap to fire, two fingers to speed up, back to pause");
    return X_STATUS_SUCCESS;
  }

  void EnumerateDevices(std::vector<DeviceInfo>& out) override {
    DeviceInfo info;
    info.id = kTouchDevice;
    info.name = "Touch screen";
    info.synthetic = true;
    out.push_back(info);
  }

  X_RESULT GetDeviceState(DeviceId id, X_INPUT_STATE* out_state) override {
    if (id != kTouchDevice)
      return X_ERROR_DEVICE_NOT_CONNECTED;
    uint16_t buttons = 0;
    int16_t lx = 0;
    // The gestures call into the game: only from a guest thread (the game reading its pad).
    if (REXCVAR_GET(peggle2_touch) && rex::runtime::ThreadState::Get()) {
      if (!g_base)
        g_base = rex::system::kernel_state()->memory()->virtual_membase();
      Update(g_base, buttons, lx);
    }
    // XInput's packet number changes whenever the state does.
    if (buttons != g_pad.last_buttons || lx != g_pad.last_lx) {
      packet_.fetch_add(1, std::memory_order_relaxed);
      g_pad.last_buttons = buttons;
      g_pad.last_lx = lx;
    }
    if (out_state) {
      *out_state = {};
      out_state->packet_number = packet_.load(std::memory_order_relaxed);
      out_state->gamepad.buttons = buttons;
      out_state->gamepad.thumb_lx = lx;
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT GetDeviceCapabilities(DeviceId id, uint32_t, X_INPUT_CAPABILITIES* out_caps) override {
    if (id != kTouchDevice)
      return X_ERROR_DEVICE_NOT_CONNECTED;
    if (out_caps) {
      *out_caps = {};
      out_caps->type = 0x01;      // XINPUT_DEVTYPE_GAMEPAD
      out_caps->sub_type = 0x01;  // XINPUT_DEVSUBTYPE_GAMEPAD
      out_caps->gamepad.buttons = 0xFFFF;
      out_caps->gamepad.thumb_lx = int16_t(0x7FFF);
      out_caps->gamepad.thumb_ly = int16_t(0x7FFF);
    }
    return X_ERROR_SUCCESS;
  }

  X_RESULT SetDeviceVibration(DeviceId id, X_INPUT_VIBRATION*) override {
    return id == kTouchDevice ? X_ERROR_SUCCESS : X_ERROR_DEVICE_NOT_CONNECTED;
  }

  X_RESULT GetDeviceKeystroke(DeviceId id, uint32_t, X_INPUT_KEYSTROKE*) override {
    return id == kTouchDevice ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }

 private:
  std::atomic<uint32_t> packet_{0x20000000};
};

}  // namespace

// Peggle2Activity.nativeMenu(int): the pause menu's choices. Mute applies at once (the audio thread reads the cvar);
// the rest runs on the game thread at its next pad read.
extern "C" JNIEXPORT void JNICALL Java_com_ari_peggle2_Peggle2Activity_nativeMenu(JNIEnv*, jclass, jint command) {
  if (command == kMenuMute || command == kMenuUnmute)
    REXCVAR_SET(audio_mute, command == kMenuMute);
  std::lock_guard lock(g_menu_mutex);
  g_menu_commands.push_back(int(command));
}

namespace peggle2::touch {

InputFactory Wrap(InputFactory base) {
  return [base = std::move(base)](bool tool_mode) -> std::unique_ptr<rex::system::IInputSystem> {
    auto system = base(tool_mode);
    if (tool_mode || !system || !REXCVAR_GET(peggle2_touch))
      return system;
    // The default input system is rex::input::InputSystem (CreateDefaultInputSystem).
    // AddDriver takes a driver already set up (as CreateDefaultInputSystem does with its own).
    auto driver = std::make_unique<TouchInputDriver>();
    if (driver->Setup() == X_STATUS_SUCCESS)
      static_cast<rex::input::InputSystem*>(system.get())->AddDriver(std::move(driver));
    return system;
  };
}

}  // namespace peggle2::touch
