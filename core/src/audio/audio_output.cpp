#include "sdrjo/audio/audio_output.hpp"

#include <vector>

#if defined(SDRJO_HAVE_MINIAUDIO)
  #define MA_NO_ENCODING
  #define MA_NO_DECODING
  #define MINIAUDIO_IMPLEMENTATION
  #include <miniaudio.h>
#endif

namespace sdrjo {

struct AudioOutput::Impl {
    RingBuffer<float> ring{1 << 16};
    int channels = 2;
    bool active = false;
#if defined(SDRJO_HAVE_MINIAUDIO)
    ma_device device{};
    bool deviceInit = false;
#endif
};

#if defined(SDRJO_HAVE_MINIAUDIO)
static void maCallback(ma_device* dev, void* out, const void*, ma_uint32 frames)
{
    auto* impl = static_cast<AudioOutput::Impl*>(dev->pUserData);
    float* dst = static_cast<float*>(out);
    size_t want = size_t(frames) * size_t(impl->channels);
    size_t got = impl->ring.read(dst, want);
    for (size_t i = got; i < want; i++) dst[i] = 0.0f; // sottoscorta: silenzio
}
#endif

AudioOutput::AudioOutput() : impl_(std::make_unique<Impl>()) {}

AudioOutput::~AudioOutput() { stop(); }

bool AudioOutput::start(double sampleRate, int channels)
{
    stop();
    impl_->channels = channels;
#if defined(SDRJO_HAVE_MINIAUDIO)
    ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = ma_uint32(channels);
    cfg.sampleRate = ma_uint32(sampleRate);
    cfg.dataCallback = maCallback;
    cfg.pUserData = impl_.get();
    if (ma_device_init(nullptr, &cfg, &impl_->device) != MA_SUCCESS)
        return false;
    if (ma_device_start(&impl_->device) != MA_SUCCESS) {
        ma_device_uninit(&impl_->device);
        return false;
    }
    impl_->deviceInit = true;
    impl_->active = true;
    return true;
#else
    (void)sampleRate;
    return false; // backend nullo: build senza miniaudio
#endif
}

void AudioOutput::stop()
{
#if defined(SDRJO_HAVE_MINIAUDIO)
    if (impl_->deviceInit) {
        ma_device_uninit(&impl_->device);
        impl_->deviceInit = false;
    }
#endif
    impl_->active = false;
}

bool AudioOutput::isActive() const { return impl_->active; }

std::string AudioOutput::backendName() const
{
#if defined(SDRJO_HAVE_MINIAUDIO)
    return "miniaudio";
#else
    return "nullo (compilato senza miniaudio)";
#endif
}

void AudioOutput::write(const float* samples, size_t count)
{
    if (!impl_->active) return;
    impl_->ring.write(samples, count); // se pieno, scarta
}

void AudioOutput::writeMono(const float* samples, size_t count)
{
    if (!impl_->active) return;
    if (impl_->channels == 1) {
        write(samples, count);
        return;
    }
    std::vector<float> stereo(count * 2);
    for (size_t i = 0; i < count; i++) {
        stereo[2 * i] = samples[i];
        stereo[2 * i + 1] = samples[i];
    }
    write(stereo.data(), stereo.size());
}

} // namespace sdrjo
