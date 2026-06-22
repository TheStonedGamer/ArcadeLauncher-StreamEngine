// ArcadeLauncher Stream Engine — GPL-3.0-or-later
#if defined(ASE_HAVE_OPUS) && defined(ASE_HAVE_SDL)
#include "client/audio_renderer.h"

#include <cstdio>

#include <SDL.h>
#include <opus_multistream.h>

#ifdef ASE_HAVE_MOONLIGHT
#include <Limelight.h>
#endif

namespace ase::client {

AudioRenderer::~AudioRenderer() { cleanup(); }

#ifdef ASE_HAVE_MOONLIGHT
int AudioRenderer::init(int /*audioConfiguration*/, POPUS_MULTISTREAM_CONFIGURATION opusConfig) {
  if (!opusConfig) return -1;
  channelCount_ = opusConfig->channelCount;
  samplesPerFrame_ = opusConfig->samplesPerFrame;

  int err = 0;
  decoder_ = opus_multistream_decoder_create(opusConfig->sampleRate, opusConfig->channelCount,
                                             opusConfig->streams, opusConfig->coupledStreams,
                                             opusConfig->mapping, &err);
  if (!decoder_ || err != OPUS_OK) {
    std::fprintf(stderr, "opus decoder create failed: %s\n", opus_strerror(err));
    decoder_ = nullptr;
    return -1;
  }

  if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
    std::fprintf(stderr, "SDL audio init failed: %s\n", SDL_GetError());
    return -1;
  }
  sdlInited_ = true;

  SDL_AudioSpec want;
  SDL_zero(want);
  want.freq = opusConfig->sampleRate;
  want.format = AUDIO_S16SYS;
  want.channels = static_cast<Uint8>(opusConfig->channelCount);
  // One Opus frame per channel is the natural queue granularity; round up to a power of two.
  int bufSamples = 1024;
  while (bufSamples < samplesPerFrame_) bufSamples <<= 1;
  want.samples = static_cast<Uint16>(bufSamples);
  want.callback = nullptr;  // we push with SDL_QueueAudio

  SDL_AudioSpec have;
  audioDevice_ = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (audioDevice_ == 0) {
    std::fprintf(stderr, "SDL_OpenAudioDevice failed: %s\n", SDL_GetError());
    return -1;
  }

  pcm_.assign(static_cast<size_t>(samplesPerFrame_) * channelCount_, 0);
  SDL_PauseAudioDevice(audioDevice_, 0);  // start playback
  return 0;
}
#endif  // ASE_HAVE_MOONLIGHT

void AudioRenderer::decode_and_play(const char* sampleData, int sampleLength) {
  if (!decoder_ || audioDevice_ == 0 || pcm_.empty()) return;
  OpusMSDecoder* dec = static_cast<OpusMSDecoder*>(decoder_);
  const int decoded = opus_multistream_decode(
      dec, reinterpret_cast<const unsigned char*>(sampleData), sampleLength, pcm_.data(),
      samplesPerFrame_, 0);
  if (decoded <= 0) return;  // negative = error, 0 = nothing; drop the packet
  const int bytes = decoded * channelCount_ * static_cast<int>(sizeof(int16_t));
  if (SDL_QueueAudio(audioDevice_, pcm_.data(), static_cast<Uint32>(bytes)) == 0) {
    ++framesPlayed_;
  }
}

void AudioRenderer::cleanup() {
  if (audioDevice_ != 0) {
    SDL_CloseAudioDevice(audioDevice_);
    audioDevice_ = 0;
  }
  if (decoder_) {
    opus_multistream_decoder_destroy(static_cast<OpusMSDecoder*>(decoder_));
    decoder_ = nullptr;
  }
  if (sdlInited_) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    sdlInited_ = false;
  }
}

}  // namespace ase::client
#endif  // ASE_HAVE_OPUS && ASE_HAVE_SDL
