// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The live GameStream connection driver: builds moonlight-common-c's STREAM_CONFIGURATION +
// SERVER_INFORMATION from a resolved start request and runs LiStartConnection() on a worker thread,
// forwarding connection-listener callbacks to the launcher as stream.state events.
//
// When SDL + FFmpeg are linked (ASE_HAVE_SDL && ASE_HAVE_FFMPEG) the worker also owns a borderless
// window (StreamWindow) and a hardware decoder (VideoRenderer): video decodes on moonlight's submit
// thread and presents on the worker's event/present loop, gamepad input is captured, and decode
// stats are emitted as stream.stats. Without those deps it falls back to null sinks (frames accepted
// and discarded) so the connection still works headless.
//
// moonlight-common-c supports exactly one connection at a time (LiStartConnection is documented as
// not thread-safe and keeps global state), so this is a process-singleton driver. Built whenever the
// moonlight fork is linked (ASE_HAVE_MOONLIGHT).
#pragma once
#ifdef ASE_HAVE_MOONLIGHT
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

#include "client/stream_config.h"

namespace ase::client {

// Resolved inputs for one stream — everything the engine learned from serverinfo + the launch
// response, plus the negotiated input key. rtspSessionUrl comes from the completed launch/resume.
struct StartInfo {
  std::string host;                 // host address (SERVER_INFORMATION.address)
  std::string serverAppVersion;     // <appversion> from serverinfo
  std::string gfeVersion;           // <GfeVersion> from serverinfo (may be empty for Sunshine)
  std::string rtspSessionUrl;       // <sessionUrl0> from launch/resume
  int serverCodecModeSupport = 0;   // <ServerCodecModeSupport> from serverinfo
  StreamSettings settings;          // validated client.start settings
  std::string riKey;                // 16 raw bytes — must match the launch rikey
  int riKeyId = 0;                  // must match the launch rikeyid
  bool embedWindow = false;         // keep the window hidden for the launcher to reparent
};

// A stream.state phase change. nativeWindow is set only on the "window" signal (the reparent handle,
// HWND / X11 id as a decimal string); empty otherwise.
struct StateEvent {
  std::string phase;        // connecting | window | streaming | ended | error
  std::string reason;       // "" unless error / graceful end
  std::string nativeWindow; // set only when phase == "window"
};
using StateSink = std::function<void(const StateEvent&)>;

// Periodic decode stats for the HUD (stream.stats). Only emitted when the renderer is active.
struct StreamStats {
  uint64_t framesDecoded = 0;
  uint64_t framesPresented = 0;
  uint64_t decodeErrors = 0;
  bool hardware = false;
  int width = 0;
  int height = 0;
  uint32_t rttMs = 0;
  uint32_t rttVarMs = 0;
};
using StatsSink = std::function<void(const StreamStats&)>;

class StreamSession {
 public:
  StreamSession(StateSink onState, StatsSink onStats);
  ~StreamSession();

  StreamSession(const StreamSession&) = delete;
  StreamSession& operator=(const StreamSession&) = delete;

  // Begin connecting on a worker thread. Returns false + err if a stream is already running or the
  // inputs are unusable; otherwise true, with progress reported through the sinks.
  bool start(const StartInfo& info, std::string& err);

  // Stop the current stream (idempotent): interrupts a pending connect, tears it down, joins.
  void stop();

  bool running() const { return running_; }

 private:
  void run(StartInfo info);

  StateSink onState_;
  StatsSink onStats_;
  std::thread worker_;
  std::atomic<bool> running_{false};
  std::atomic<bool> stopRequested_{false};
};

}  // namespace ase::client
#endif  // ASE_HAVE_MOONLIGHT
