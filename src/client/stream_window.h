// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The on-screen surface for a live stream: a borderless SDL2 window the launcher can reparent into
// its own UI, plus the SDL event pump and gamepad capture (forwarded to the host via
// moonlight-common-c's LiSend*ControllerEvent). The window, its renderer, the event pump, and all
// presentation happen on ONE thread (the stream session's worker) — SDL is not safe to drive a
// single window from multiple threads, and on Windows the message loop must run on the window's
// owning thread. Only built when SDL is linked (ASE_HAVE_SDL).
#pragma once
#ifdef ASE_HAVE_SDL
#include <cstdint>
#include <string>

struct SDL_Window;
struct SDL_Renderer;
struct _SDL_GameController;
typedef struct _SDL_GameController SDL_GameController;

namespace ase::client {

// Owns the SDL window + renderer for one stream. Single-threaded: construct, pump, present, and
// destroy all on the same thread.
class StreamWindow {
 public:
  StreamWindow();
  ~StreamWindow();

  StreamWindow(const StreamWindow&) = delete;
  StreamWindow& operator=(const StreamWindow&) = delete;

  // Create the borderless window + accelerated renderer at the given size. false + err on failure.
  bool create(int width, int height, const std::string& title, std::string& err);

  // Native window handle for the launcher to reparent (HWND / X11 window id as a decimal string),
  // or "" before create() or if unavailable.
  std::string native_handle() const { return nativeHandle_; }

  SDL_Renderer* renderer() const { return renderer_; }

  // Make the window visible (for the standalone, non-embedded case — when embedded, the launcher
  // shows it after reparenting).
  void show();

  // Pump SDL events once and forward gamepad state to the host. Returns false when the user has
  // asked to quit (window closed). Call once per present iteration.
  bool pump();

  void destroy();

 private:
  void poll_and_send_gamepads();

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_GameController* pads_[4] = {nullptr, nullptr, nullptr, nullptr};
  std::string nativeHandle_;
  bool sdlInited_ = false;
  bool quit_ = false;
};

}  // namespace ase::client
#endif  // ASE_HAVE_SDL
