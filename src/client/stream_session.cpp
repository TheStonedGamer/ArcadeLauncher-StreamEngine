// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#ifdef ASE_HAVE_MOONLIGHT
#include "client/stream_session.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

#include <Limelight.h>

#if defined(ASE_HAVE_SDL) && defined(ASE_HAVE_FFMPEG)
#define ASE_HAVE_RENDERER 1
#include <SDL.h>

#include "client/stream_window.h"
#include "client/video_renderer.h"
#endif

// Audio rides with the renderer build (needs SDL for output); wired only inside the renderer path.
#if defined(ASE_HAVE_OPUS) && defined(ASE_HAVE_SDL)
#define ASE_HAVE_AUDIO 1
#include "client/audio_renderer.h"
#endif

namespace ase::client {

namespace {

// moonlight-common-c's connection-listener / renderer callbacks carry no context pointer and the
// library is single-connection, so the active session's sink + renderer live here. Set under
// start(), cleared in stop(). All listener callbacks run on moonlight's internal threads.
StateSink g_sink;

void emit(const std::string& phase, const std::string& reason) {
  if (g_sink) g_sink(StateEvent{phase, reason, ""});
}

// --- Connection listener: translate moonlight stages into stream.state phases ------------------
void cl_stage_starting(int stage) {
  emit("connecting", LiGetStageName(stage));
}
void cl_stage_failed(int stage, int errorCode) {
  emit("error", std::string("stage '") + LiGetStageName(stage) +
                    "' failed (code " + std::to_string(errorCode) + ")");
}
void cl_connection_started(void) {
  emit("streaming", "");
}
void cl_connection_terminated(int errorCode) {
  if (errorCode == ML_ERROR_GRACEFUL_TERMINATION) {
    emit("ended", "host ended the session");
  } else {
    emit("error", "connection terminated (code " + std::to_string(errorCode) + ")");
  }
}
void cl_log_message(const char* format, ...) {
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
}

#ifdef ASE_HAVE_RENDERER
// The active renderer for the video callbacks (single-connection, like g_sink).
VideoRenderer* g_video = nullptr;

int vr_setup(int videoFormat, int width, int height, int /*redrawRate*/, void* /*ctx*/,
             int /*flags*/) {
  return g_video ? g_video->setup(videoFormat, width, height) : -1;
}
void vr_noop(void) {}
int vr_submit(PDECODE_UNIT du) { return g_video ? g_video->submit(du) : DR_OK; }
#else
// --- Null video sink: accept and discard frames -------------------------------------------------
int vr_setup(int, int, int, int, void*, int) { return 0; }
void vr_noop(void) {}
int vr_submit(PDECODE_UNIT) { return DR_OK; }
#endif

#ifdef ASE_HAVE_AUDIO
// The active audio renderer for the audio callbacks (single-connection, like g_video).
AudioRenderer* g_audio = nullptr;

int ar_init(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION opusConfig, void* /*ctx*/,
            int /*flags*/) {
  return g_audio ? g_audio->init(audioConfiguration, opusConfig) : 0;
}
void ar_noop(void) {}
void ar_play(char* sampleData, int sampleLength) {
  if (g_audio) g_audio->decode_and_play(sampleData, sampleLength);
}
#else
// --- Null audio sink: accept and discard samples (built without Opus/SDL) -----------------------
int ar_init(int /*audioConfiguration*/, const POPUS_MULTISTREAM_CONFIGURATION /*opusConfig*/,
            void* /*ctx*/, int /*flags*/) {
  return 0;
}
void ar_noop(void) {}
void ar_play(char* /*sampleData*/, int /*sampleLength*/) {}
#endif

}  // namespace

StreamSession::StreamSession(StateSink onState, StatsSink onStats)
    : onState_(std::move(onState)), onStats_(std::move(onStats)) {}

StreamSession::~StreamSession() { stop(); }

bool StreamSession::start(const StartInfo& info, std::string& err) {
  if (running_) {
    err = "a stream is already running";
    return false;
  }
  if (info.riKey.size() != 16) {
    err = "remote-input AES key must be 16 bytes";
    return false;
  }
  if (info.rtspSessionUrl.empty()) {
    err = "missing RTSP session URL (launch/resume did not complete)";
    return false;
  }
  g_sink = onState_;
  running_ = true;
  stopRequested_ = false;
  worker_ = std::thread(&StreamSession::run, this, info);
  return true;
}

void StreamSession::run(StartInfo info) {
  STREAM_CONFIGURATION cfg;
  LiInitializeStreamConfiguration(&cfg);
  cfg.width = info.settings.width;
  cfg.height = info.settings.height;
  cfg.fps = info.settings.fps;
  cfg.bitrate = info.settings.bitrateKbps;
  cfg.packetSize = 1024;
  cfg.streamingRemotely = STREAM_CFG_AUTO;
  cfg.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
  // Advertise H.264 only: it is universally supported and our decoder targets it. The renderer
  // milestone can widen this once HEVC/AV1 decode paths are validated.
  cfg.supportedVideoFormats = VIDEO_FORMAT_H264;
  cfg.colorSpace = COLORSPACE_REC_709;
  cfg.colorRange = COLOR_RANGE_LIMITED;
  cfg.encryptionFlags = ENCFLG_AUDIO;

  // Remote-input AES key/id — MUST equal the rikey/rikeyid sent in the launch request. The id is
  // stored big-endian in the first 4 bytes of the IV (moonlight reads it back big-endian).
  std::memcpy(cfg.remoteInputAesKey, info.riKey.data(), 16);
  std::memset(cfg.remoteInputAesIv, 0, sizeof(cfg.remoteInputAesIv));
  cfg.remoteInputAesIv[0] = static_cast<char>((info.riKeyId >> 24) & 0xff);
  cfg.remoteInputAesIv[1] = static_cast<char>((info.riKeyId >> 16) & 0xff);
  cfg.remoteInputAesIv[2] = static_cast<char>((info.riKeyId >> 8) & 0xff);
  cfg.remoteInputAesIv[3] = static_cast<char>(info.riKeyId & 0xff);

  SERVER_INFORMATION serverInfo;
  LiInitializeServerInformation(&serverInfo);
  serverInfo.address = info.host.c_str();
  serverInfo.serverInfoAppVersion = info.serverAppVersion.c_str();
  serverInfo.serverInfoGfeVersion = info.gfeVersion.empty() ? nullptr : info.gfeVersion.c_str();
  serverInfo.rtspSessionUrl = info.rtspSessionUrl.c_str();
  serverInfo.serverCodecModeSupport = info.serverCodecModeSupport;

  CONNECTION_LISTENER_CALLBACKS cl;
  LiInitializeConnectionCallbacks(&cl);
  cl.stageStarting = cl_stage_starting;
  cl.stageFailed = cl_stage_failed;
  cl.connectionStarted = cl_connection_started;
  cl.connectionTerminated = cl_connection_terminated;
  cl.logMessage = cl_log_message;

  DECODER_RENDERER_CALLBACKS vr;
  LiInitializeVideoCallbacks(&vr);
  vr.setup = vr_setup;
  vr.start = vr_noop;
  vr.stop = vr_noop;
  vr.cleanup = vr_noop;
  vr.submitDecodeUnit = vr_submit;
  vr.capabilities = CAPABILITY_DIRECT_SUBMIT;

  AUDIO_RENDERER_CALLBACKS ar;
  LiInitializeAudioCallbacks(&ar);
  ar.init = ar_init;
  ar.start = ar_noop;
  ar.stop = ar_noop;
  ar.cleanup = ar_noop;
  ar.decodeAndPlaySample = ar_play;
  ar.capabilities = CAPABILITY_DIRECT_SUBMIT;

#ifdef ASE_HAVE_RENDERER
  // Stand up the window + decoder before connecting so the reparent handle exists and the decode
  // callbacks have a live renderer.
  StreamWindow window;
  VideoRenderer renderer;
  std::string werr;
  const bool haveWindow = window.create(info.settings.width, info.settings.height,
                                        "ArcadeLauncher Stream", werr);
  if (haveWindow) {
    g_video = &renderer;
    if (onState_) onState_(StateEvent{"window", "", window.native_handle()});
    if (!info.embedWindow) window.show();
  } else {
    // Fall back to a headless connection if the window can't be created — still useful for tests.
    std::fprintf(stderr, "stream window unavailable (%s); running headless\n", werr.c_str());
  }

#ifdef ASE_HAVE_AUDIO
  // The audio renderer sets itself up from the negotiated Opus config in the init callback; it just
  // needs to be reachable when that callback fires during LiStartConnection.
  AudioRenderer audio;
  g_audio = &audio;
#endif

  const int rc = LiStartConnection(&serverInfo, &cfg, &cl, &vr, &ar, nullptr, 0, nullptr, 0);
  if (rc != 0) {
    running_ = false;
    g_video = nullptr;
#ifdef ASE_HAVE_AUDIO
    g_audio = nullptr;
#endif
    emit("error", "LiStartConnection failed (code " + std::to_string(rc) + ")");
    return;
  }

  // Present / event / stats loop on this thread until the launcher stops us or the window closes.
  uint32_t lastStats = SDL_GetTicks();
  bool userClosed = false;
  while (!stopRequested_) {
    if (haveWindow) {
      if (!window.pump()) {  // user closed the window
        userClosed = true;
        break;
      }
      renderer.present(window.renderer());
    }
    const uint32_t now = SDL_GetTicks();
    if (onStats_ && now - lastStats >= 1000) {
      lastStats = now;
      const VideoStats vs = renderer.stats();
      StreamStats s;
      s.framesDecoded = vs.framesDecoded;
      s.framesPresented = vs.framesPresented;
      s.decodeErrors = vs.decodeErrors;
      s.hardware = vs.usingHardware;
      s.width = vs.width;
      s.height = vs.height;
      LiGetEstimatedRttInfo(&s.rttMs, &s.rttVarMs);
      onStats_(s);
    }
    if (!haveWindow) SDL_Delay(50);  // headless: nothing to present, just idle
  }

  LiStopConnection();  // blocks until moonlight's threads (incl. audio) have stopped
  g_video = nullptr;
  renderer.cleanup();
#ifdef ASE_HAVE_AUDIO
  g_audio = nullptr;
  audio.cleanup();
#endif
  window.destroy();
  running_ = false;
  if (userClosed) emit("ended", "window closed");
#else
  // No renderer linked: just establish the connection with null sinks (headless). LiStartConnection
  // returns once streaming; the library keeps the stream alive on its own threads until stop().
  const int rc = LiStartConnection(&serverInfo, &cfg, &cl, &vr, &ar, nullptr, 0, nullptr, 0);
  if (rc != 0) {
    running_ = false;
    emit("error", "LiStartConnection failed (code " + std::to_string(rc) + ")");
  }
#endif
}

void StreamSession::stop() {
  stopRequested_ = true;
  if (worker_.joinable()) {
    LiInterruptConnection();  // unblock a pending LiStartConnection setup
    worker_.join();
  }
  if (running_) {
#ifndef ASE_HAVE_RENDERER
    LiStopConnection();  // the renderer path stops inside run(); headless path stops here
#endif
    running_ = false;
  }
  g_sink = nullptr;
}

}  // namespace ase::client
#endif  // ASE_HAVE_MOONLIGHT
