#if defined(SDRJO_HAVE_RTLSDR)

#include "sdrjo/source/rtl_sdr_source.hpp"

#include <rtl-sdr.h>

#include <cstring>
#include <stdexcept>

namespace sdrjo {

std::vector<RtlSdrSource::DeviceDesc> RtlSdrSource::enumerate()
{
    std::vector<DeviceDesc> out;
    uint32_t count = rtlsdr_get_device_count();
    for (uint32_t i = 0; i < count; i++) {
        char manuf[256] = {0}, product[256] = {0}, serial[256] = {0};
        rtlsdr_get_device_usb_strings(i, manuf, product, serial);
        DeviceDesc d;
        d.index = i;
        d.name = std::string(manuf) + " " + product;
        d.serial = serial;
        out.push_back(std::move(d));
    }
    return out;
}

RtlSdrSource::RtlSdrSource(uint32_t deviceIndex)
{
    if (rtlsdr_open(&dev_, deviceIndex) != 0)
        throw std::runtime_error("impossibile aprire il dispositivo RTL-SDR #" +
                                 std::to_string(deviceIndex));
    rtlsdr_set_sample_rate(dev_, uint32_t(rateHz_));
    rtlsdr_set_center_freq(dev_, uint32_t(freqHz_));
    rtlsdr_set_tuner_gain_mode(dev_, 0); // AGC di default
}

RtlSdrSource::~RtlSdrSource()
{
    stop();
    if (dev_) rtlsdr_close(dev_);
}

std::string RtlSdrSource::name() const { return "RTL-SDR"; }

bool RtlSdrSource::setCenterFrequency(double hz)
{
    // Il driver rtl-sdr-blog gestisce internamente l'upconverter della V4
    // per le frequenze sotto ~28 MHz: basta chiedere la frequenza voluta.
    if (rtlsdr_set_center_freq(dev_, uint32_t(hz)) != 0) return false;
    freqHz_ = hz;
    return true;
}

bool RtlSdrSource::setSampleRate(double hz)
{
    if (rtlsdr_set_sample_rate(dev_, uint32_t(hz)) != 0) return false;
    rateHz_ = double(rtlsdr_get_sample_rate(dev_));
    return true;
}

bool RtlSdrSource::setGain(double gainDb)
{
    if (gainDb < 0.0)
        return rtlsdr_set_tuner_gain_mode(dev_, 0) == 0;
    if (rtlsdr_set_tuner_gain_mode(dev_, 1) != 0) return false;
    return rtlsdr_set_tuner_gain(dev_, int(gainDb * 10.0)) == 0;
}

bool RtlSdrSource::setPpmCorrection(int ppm)
{
    return rtlsdr_set_freq_correction(dev_, ppm) == 0;
}

bool RtlSdrSource::setBiasTee(bool on)
{
#if defined(RTLSDR_HAS_BIAS_TEE) || 1
    // rtlsdr_set_bias_tee esiste da librtlsdr 0.6 in poi e nel fork blog.
    return rtlsdr_set_bias_tee(dev_, on ? 1 : 0) == 0;
#else
    (void)on;
    return false;
#endif
}

bool RtlSdrSource::start(IqCallback cb)
{
    if (running_.load()) return false;
    callback_ = std::move(cb);
    rtlsdr_reset_buffer(dev_);
    running_.store(true);
    worker_ = std::thread(&RtlSdrSource::workerLoop, this);
    return true;
}

void RtlSdrSource::stop()
{
    if (!running_.exchange(false)) return;
    rtlsdr_cancel_async(dev_);
    if (worker_.joinable()) worker_.join();
}

void RtlSdrSource::workerLoop()
{
    struct Ctx {
        RtlSdrSource* self;
        std::vector<cfloat> conv;
    } ctx{this, {}};

    auto trampoline = [](unsigned char* buf, uint32_t len, void* vctx) {
        auto* c = static_cast<Ctx*>(vctx);
        if (!c->self->running_.load()) return;
        size_t n = len / 2;
        if (c->conv.size() < n) c->conv.resize(n);
        convertU8Iq(buf, n, c->conv.data());
        if (c->self->callback_) c->self->callback_(c->conv.data(), n);
    };

    // 16 buffer da 64k: latenza contenuta senza perdere campioni.
    rtlsdr_read_async(dev_, trampoline, &ctx, 16, 65536);
    running_.store(false);
}

} // namespace sdrjo

#endif // SDRJO_HAVE_RTLSDR
