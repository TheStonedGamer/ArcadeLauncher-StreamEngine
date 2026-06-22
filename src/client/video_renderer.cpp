// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#if defined(ASE_HAVE_FFMPEG) && defined(ASE_HAVE_SDL)
#include "client/video_renderer.h"

#include <cstring>
#include <vector>

#include <SDL.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixfmt.h>
}

#ifdef ASE_HAVE_MOONLIGHT
#include <Limelight.h>
#endif

namespace ase::client {

namespace {

// Platform HW decode target: D3D11VA on Windows, VAAPI on Linux. Header-only at build time — no GPU
// is needed to compile; the device simply fails to create at runtime on a GPU-less box (we then
// fall back to software).
#if defined(_WIN32)
constexpr AVHWDeviceType kHwType = AV_HWDEVICE_TYPE_D3D11VA;
constexpr AVPixelFormat kHwPixFmt = AV_PIX_FMT_D3D11;
#else
constexpr AVHWDeviceType kHwType = AV_HWDEVICE_TYPE_VAAPI;
constexpr AVPixelFormat kHwPixFmt = AV_PIX_FMT_VAAPI;
#endif

// get_format: pick the HW surface format if the decoder offers it, else the first software format.
AVPixelFormat pick_format(AVCodecContext* ctx, const AVPixelFormat* fmts) {
  const int want = ctx->opaque ? *static_cast<int*>(ctx->opaque) : -1;
  for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
    if (static_cast<int>(*p) == want) return *p;
  }
  return fmts[0];  // software fallback
}

}  // namespace

VideoRenderer::VideoRenderer() = default;
VideoRenderer::~VideoRenderer() { cleanup(); }

int VideoRenderer::setup(int /*videoFormat*/, int width, int height) {
  width_ = width;
  height_ = height;

  // We advertise H.264 only (see stream_session), so a single decoder covers every stream.
  const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_H264);
  if (!codec) return -1;

  // Attempt 1: hardware decode.
  if (av_hwdevice_ctx_create(&hwDeviceCtx_, kHwType, nullptr, nullptr, 0) == 0) {
    codecCtx_ = avcodec_alloc_context3(codec);
    if (codecCtx_) {
      codecCtx_->width = width;
      codecCtx_->height = height;
      codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
      hwPixFmt_ = static_cast<int>(kHwPixFmt);
      codecCtx_->opaque = &hwPixFmt_;
      codecCtx_->get_format = pick_format;
      if (avcodec_open2(codecCtx_, codec, nullptr) == 0) {
        usingHardware_ = true;
        return 0;
      }
      avcodec_free_context(&codecCtx_);
    }
    av_buffer_unref(&hwDeviceCtx_);
    hwDeviceCtx_ = nullptr;
    hwPixFmt_ = -1;
  }

  // Attempt 2: software decode.
  codecCtx_ = avcodec_alloc_context3(codec);
  if (!codecCtx_) return -1;
  codecCtx_->width = width;
  codecCtx_->height = height;
  if (avcodec_open2(codecCtx_, codec, nullptr) != 0) {
    avcodec_free_context(&codecCtx_);
    return -1;
  }
  usingHardware_ = false;
  return 0;
}

#ifdef ASE_HAVE_MOONLIGHT
int VideoRenderer::submit(PDECODE_UNIT du) {
  if (!codecCtx_) return DR_NEED_IDR;

  // Concatenate the buffer chain into one contiguous, padded packet buffer (reused across frames).
  static thread_local std::vector<uint8_t> buf;
  buf.clear();
  buf.reserve(static_cast<size_t>(du->fullLength) + AV_INPUT_BUFFER_PADDING_SIZE);
  for (PLENTRY e = du->bufferList; e != nullptr; e = e->next) {
    buf.insert(buf.end(), e->data, e->data + e->length);
  }
  buf.resize(buf.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);

  AVPacket* pkt = av_packet_alloc();
  if (!pkt) return DR_NEED_IDR;
  pkt->data = buf.data();
  pkt->size = du->fullLength;

  int ret = avcodec_send_packet(codecCtx_, pkt);
  av_packet_free(&pkt);
  if (ret < 0) {
    ++decodeErrors_;
    return DR_NEED_IDR;  // ask the host for a fresh IDR to recover the decoder
  }

  AVFrame* frame = av_frame_alloc();
  for (;;) {
    ret = avcodec_receive_frame(codecCtx_, frame);
    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
    if (ret < 0) {
      ++decodeErrors_;
      break;
    }

    // Move the frame to system memory: transfer HW surfaces (forced to NV12), clone SW frames.
    AVFrame* cpu = av_frame_alloc();
    if (hwPixFmt_ >= 0 && frame->format == hwPixFmt_) {
      cpu->format = AV_PIX_FMT_NV12;
      if (av_hwframe_transfer_data(cpu, frame, 0) < 0) {
        av_frame_free(&cpu);
        ++decodeErrors_;
        av_frame_unref(frame);
        continue;
      }
    } else {
      av_frame_ref(cpu, frame);
    }

    {
      std::lock_guard<std::mutex> lock(frameMu_);
      if (pendingFrame_) av_frame_free(&pendingFrame_);
      pendingFrame_ = cpu;  // ownership transferred to the present side
    }
    ++framesDecoded_;
    av_frame_unref(frame);
  }
  av_frame_free(&frame);
  return DR_OK;
}
#endif  // ASE_HAVE_MOONLIGHT

bool VideoRenderer::ensure_texture(SDL_Renderer* renderer, int fmt, int w, int h) {
  if (texture_ && texFmt_ == fmt && texW_ == w && texH_ == h) return true;
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  texture_ = SDL_CreateTexture(renderer, static_cast<Uint32>(fmt), SDL_TEXTUREACCESS_STREAMING, w, h);
  if (!texture_) return false;
  texFmt_ = fmt;
  texW_ = w;
  texH_ = h;
  return true;
}

void VideoRenderer::present(SDL_Renderer* renderer) {
  if (!renderer) return;

  AVFrame* frame = nullptr;
  {
    std::lock_guard<std::mutex> lock(frameMu_);
    frame = pendingFrame_;
    pendingFrame_ = nullptr;
  }
  if (!frame) return;  // nothing new to show

  bool ok = false;
  if (frame->format == AV_PIX_FMT_NV12) {
    if (ensure_texture(renderer, SDL_PIXELFORMAT_NV12, frame->width, frame->height)) {
      ok = SDL_UpdateNVTexture(texture_, nullptr, frame->data[0], frame->linesize[0],
                               frame->data[1], frame->linesize[1]) == 0;
    }
  } else if (frame->format == AV_PIX_FMT_YUV420P) {
    if (ensure_texture(renderer, SDL_PIXELFORMAT_IYUV, frame->width, frame->height)) {
      ok = SDL_UpdateYUVTexture(texture_, nullptr, frame->data[0], frame->linesize[0],
                                frame->data[1], frame->linesize[1], frame->data[2],
                                frame->linesize[2]) == 0;
    }
  }

  if (ok) {
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer);
    ++framesPresented_;
  }
  av_frame_free(&frame);
}

void VideoRenderer::cleanup() {
  {
    std::lock_guard<std::mutex> lock(frameMu_);
    if (pendingFrame_) av_frame_free(&pendingFrame_);
  }
  if (texture_) {
    SDL_DestroyTexture(texture_);
    texture_ = nullptr;
  }
  if (codecCtx_) avcodec_free_context(&codecCtx_);
  if (hwDeviceCtx_) av_buffer_unref(&hwDeviceCtx_);
  hwPixFmt_ = -1;
}

VideoStats VideoRenderer::stats() const {
  VideoStats s;
  s.framesDecoded = framesDecoded_.load();
  s.framesPresented = framesPresented_.load();
  s.decodeErrors = decodeErrors_.load();
  s.usingHardware = usingHardware_.load();
  s.width = width_;
  s.height = height_;
  return s;
}

}  // namespace ase::client
#endif  // ASE_HAVE_FFMPEG && ASE_HAVE_SDL
