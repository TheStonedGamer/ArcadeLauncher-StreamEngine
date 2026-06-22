// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The video pipeline for a live stream: FFmpeg H.264 decode (hardware via d3d11va/vaapi when
// available, software fallback) feeding an SDL2 texture present. Decode runs on moonlight-common-c's
// submit thread (submit()); presentation runs on the session worker thread (present()); a single
// decoded frame is handed across under a mutex. Replaces stream_session's null video sink. Built
// only when both FFmpeg and SDL are linked (ASE_HAVE_FFMPEG && ASE_HAVE_SDL).
#pragma once
#if defined(ASE_HAVE_FFMPEG) && defined(ASE_HAVE_SDL)
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

struct AVCodecContext;
struct AVFrame;
struct AVBufferRef;
struct SDL_Renderer;
struct SDL_Texture;

#ifdef ASE_HAVE_MOONLIGHT
struct _DECODE_UNIT;
typedef struct _DECODE_UNIT* PDECODE_UNIT;
#endif

namespace ase::client {

// Per-stream decode statistics for the stream.stats HUD feed.
struct VideoStats {
  uint64_t framesDecoded = 0;
  uint64_t framesPresented = 0;
  uint64_t decodeErrors = 0;
  bool usingHardware = false;
  int width = 0;
  int height = 0;
};

class VideoRenderer {
 public:
  VideoRenderer();
  ~VideoRenderer();

  VideoRenderer(const VideoRenderer&) = delete;
  VideoRenderer& operator=(const VideoRenderer&) = delete;

  // Initialize the decoder for the negotiated format/size (called from the video setup callback).
  // Tries hardware decode first, falls back to software. Returns 0 on success, -1 on failure.
  int setup(int videoFormat, int width, int height);

#ifdef ASE_HAVE_MOONLIGHT
  // Decode one frame (moonlight submit thread). Returns DR_OK / DR_NEED_IDR.
  int submit(PDECODE_UNIT du);
#endif

  // Present the most recently decoded frame, if any (worker thread). No-op until the first frame.
  void present(SDL_Renderer* renderer);

  void cleanup();

  VideoStats stats() const;

 private:
  bool ensure_texture(SDL_Renderer* renderer, int fmt, int w, int h);

  AVCodecContext* codecCtx_ = nullptr;
  AVBufferRef* hwDeviceCtx_ = nullptr;
  int hwPixFmt_ = -1;  // AV_PIX_FMT of the HW surface, or -1 for software

  // Frame handed from the decode thread to the present thread.
  std::mutex frameMu_;
  AVFrame* pendingFrame_ = nullptr;  // owned; the latest decoded+CPU-side frame awaiting present

  // Scratch on the present side.
  SDL_Texture* texture_ = nullptr;
  int texFmt_ = 0;
  int texW_ = 0;
  int texH_ = 0;

  std::atomic<uint64_t> framesDecoded_{0};
  std::atomic<uint64_t> framesPresented_{0};
  std::atomic<uint64_t> decodeErrors_{0};
  std::atomic<bool> usingHardware_{false};
  int width_ = 0;
  int height_ = 0;
};

}  // namespace ase::client
#endif  // ASE_HAVE_FFMPEG && ASE_HAVE_SDL
