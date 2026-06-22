// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#ifdef ASE_HAVE_SDL
#include "client/stream_window.h"

#include <cstdio>

#include <SDL.h>
#include <SDL_syswm.h>

#ifdef ASE_HAVE_MOONLIGHT
#include <Limelight.h>
#endif

namespace ase::client {

namespace {

// Map an SDL game-controller button to its moonlight button flag (0 if unmapped).
int sdl_button_flag(int button) {
#ifdef ASE_HAVE_MOONLIGHT
  switch (button) {
    case SDL_CONTROLLER_BUTTON_A: return A_FLAG;
    case SDL_CONTROLLER_BUTTON_B: return B_FLAG;
    case SDL_CONTROLLER_BUTTON_X: return X_FLAG;
    case SDL_CONTROLLER_BUTTON_Y: return Y_FLAG;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return UP_FLAG;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return DOWN_FLAG;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return LEFT_FLAG;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return RIGHT_FLAG;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return LB_FLAG;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return RB_FLAG;
    case SDL_CONTROLLER_BUTTON_START: return PLAY_FLAG;
    case SDL_CONTROLLER_BUTTON_BACK: return BACK_FLAG;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK: return LS_CLK_FLAG;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return RS_CLK_FLAG;
    case SDL_CONTROLLER_BUTTON_GUIDE: return SPECIAL_FLAG;
    default: return 0;
  }
#else
  (void)button;
  return 0;
#endif
}

}  // namespace

StreamWindow::StreamWindow() = default;
StreamWindow::~StreamWindow() { destroy(); }

bool StreamWindow::create(int width, int height, const std::string& title, std::string& err) {
  // We compile with SDL_MAIN_HANDLED (the engine owns main()), so signal SDL it can initialize.
  SDL_SetMainReady();
  if (SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
    err = std::string("SDL_InitSubSystem failed: ") + SDL_GetError();
    return false;
  }
  sdlInited_ = true;

  // Borderless so the launcher can reparent it cleanly into its own chrome. Not shown until the
  // launcher places it (SDL_WINDOW_HIDDEN) to avoid a flash at the desktop origin.
  window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width,
                             height, SDL_WINDOW_BORDERLESS | SDL_WINDOW_HIDDEN);
  if (!window_) {
    err = std::string("SDL_CreateWindow failed: ") + SDL_GetError();
    return false;
  }
  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer_) {
    // Fall back to a software renderer rather than failing the whole stream.
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
  }
  if (!renderer_) {
    err = std::string("SDL_CreateRenderer failed: ") + SDL_GetError();
    return false;
  }

  // Native handle for the launcher to reparent.
  SDL_SysWMinfo wm;
  SDL_VERSION(&wm.version);
  if (SDL_GetWindowWMInfo(window_, &wm)) {
#if defined(SDL_VIDEO_DRIVER_WINDOWS)
    nativeHandle_ = std::to_string(reinterpret_cast<uintptr_t>(wm.info.win.window));
#elif defined(SDL_VIDEO_DRIVER_X11)
    nativeHandle_ = std::to_string(static_cast<uintptr_t>(wm.info.x11.window));
#endif
  }
  return true;
}

void StreamWindow::show() {
  if (window_) SDL_ShowWindow(window_);
}

void StreamWindow::poll_and_send_gamepads() {
#ifdef ASE_HAVE_MOONLIGHT
  short activeMask = 0;
  for (int i = 0; i < 4; ++i) {
    if (pads_[i]) activeMask |= (1 << i);
  }
  for (int i = 0; i < 4; ++i) {
    SDL_GameController* gc = pads_[i];
    if (!gc) continue;
    int buttons = 0;
    for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
      if (SDL_GameControllerGetButton(gc, static_cast<SDL_GameControllerButton>(b))) {
        buttons |= sdl_button_flag(b);
      }
    }
    const unsigned char lt = static_cast<unsigned char>(
        SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
    const unsigned char rt = static_cast<unsigned char>(
        SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);
    const short lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
    const short ly = static_cast<short>(-SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY) - 1);
    const short rx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTX);
    const short ry = static_cast<short>(-SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_RIGHTY) - 1);
    LiSendMultiControllerEvent(static_cast<short>(i), activeMask, buttons, lt, rt, lx, ly, rx, ry);
  }
#endif
}

bool StreamWindow::pump() {
  SDL_Event ev;
  while (SDL_PollEvent(&ev)) {
    switch (ev.type) {
      case SDL_QUIT:
        quit_ = true;
        break;
      case SDL_CONTROLLERDEVICEADDED: {
        const int idx = ev.cdevice.which;
        if (idx >= 0 && idx < 4 && !pads_[idx]) pads_[idx] = SDL_GameControllerOpen(idx);
        break;
      }
      case SDL_CONTROLLERDEVICEREMOVED:
        for (int i = 0; i < 4; ++i) {
          if (pads_[i] &&
              SDL_GameControllerGetJoystick(pads_[i]) ==
                  SDL_JoystickFromInstanceID(ev.cdevice.which)) {
            SDL_GameControllerClose(pads_[i]);
            pads_[i] = nullptr;
          }
        }
        break;
      default:
        break;
    }
  }
  poll_and_send_gamepads();
  return !quit_;
}

void StreamWindow::destroy() {
  for (int i = 0; i < 4; ++i) {
    if (pads_[i]) {
      SDL_GameControllerClose(pads_[i]);
      pads_[i] = nullptr;
    }
  }
  if (renderer_) {
    SDL_DestroyRenderer(renderer_);
    renderer_ = nullptr;
  }
  if (window_) {
    SDL_DestroyWindow(window_);
    window_ = nullptr;
  }
  if (sdlInited_) {
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER);
    sdlInited_ = false;
  }
}

}  // namespace ase::client
#endif  // ASE_HAVE_SDL
