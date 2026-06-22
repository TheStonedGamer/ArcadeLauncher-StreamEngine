// ArcadeLauncher Stream Engine — GPL-3.0-or-later
// The audio pipeline for a live stream: Opus (multistream) decode feeding an SDL2 audio device via
// the queue API. moonlight-common-c hands us encoded Opus on its audio thread (decode_and_play,
// CAPABILITY_DIRECT_SUBMIT); we decode to interleaved S16 and queue it for SDL to play. Replaces
// stream_session's null audio sink. Built only when both Opus and SDL are linked
// (ASE_HAVE_OPUS && ASE_HAVE_SDL).
#pragma once
#if defined(ASE_HAVE_OPUS) && defined(ASE_HAVE_SDL)
#include <atomic>
#include <cstdint>
#include <vector>

#ifdef ASE_HAVE_MOONLIGHT
struct _OPUS_MULTISTREAM_CONFIGURATION;
typedef struct _OPUS_MULTISTREAM_CONFIGURATION* POPUS_MULTISTREAM_CONFIGURATION;
#endif

namespace ase::client {

class AudioRenderer {
 public:
  AudioRenderer() = default;
  ~AudioRenderer();

  AudioRenderer(const AudioRenderer&) = delete;
  AudioRenderer& operator=(const AudioRenderer&) = delete;

#ifdef ASE_HAVE_MOONLIGHT
  // Initialize the Opus decoder + SDL audio device for the negotiated config (audio init callback).
  // Returns 0 on success, non-zero on failure (moonlight then proceeds without audio).
  int init(int audioConfiguration, POPUS_MULTISTREAM_CONFIGURATION opusConfig);
#endif

  // Decode one Opus packet and queue it for playback (moonlight audio thread).
  void decode_and_play(const char* sampleData, int sampleLength);

  void cleanup();

  uint64_t framesPlayed() const { return framesPlayed_.load(); }

 private:
  void* decoder_ = nullptr;        // OpusMSDecoder* (opaque to keep opus out of the header)
  uint32_t audioDevice_ = 0;       // SDL_AudioDeviceID
  int channelCount_ = 0;
  int samplesPerFrame_ = 0;
  bool sdlInited_ = false;
  std::vector<int16_t> pcm_;       // reused decode scratch (samplesPerFrame * channelCount)
  std::atomic<uint64_t> framesPlayed_{0};
};

}  // namespace ase::client
#endif  // ASE_HAVE_OPUS && ASE_HAVE_SDL
