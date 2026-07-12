#include "sdrjo/source/rtl_sdr_source.hpp"

#include <cstring>
#include <stdexcept>

#if defined(_WIN32)
  #include <windows.h>
  static void* osOpenLib(const char* n) { return (void*)LoadLibraryA(n); }
  // Carica da un percorso completo risolvendo anche le DLL dipendenti
  // (libusb-1.0.dll ecc.) dalla stessa cartella.
  static void* osOpenLibAt(const std::string& path)
  {
      return (void*)LoadLibraryExA(path.c_str(), nullptr,
                                   LOAD_WITH_ALTERED_SEARCH_PATH);
  }
  static void* osSym(void* h, const char* s) { return (void*)GetProcAddress((HMODULE)h, s); }
  // Cartella dell'eseguibile (per cercare in exe\driver\).
  static std::string exeDirectory()
  {
      char buf[MAX_PATH] = {0};
      DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
      if (n == 0 || n >= MAX_PATH) return {};
      std::string p(buf, n);
      size_t slash = p.find_last_of("\\/");
      return (slash == std::string::npos) ? std::string() : p.substr(0, slash);
  }
#else
  #include <dlfcn.h>
  static void* osOpenLib(const char* n) { return dlopen(n, RTLD_NOW | RTLD_GLOBAL); }
  static void* osSym(void* h, const char* s) { return dlsym(h, s); }
#endif

namespace sdrjo {

// ---------------------------------------------------------------------------
// Tabella delle funzioni di librtlsdr caricate a runtime.
// Firme da rtl-sdr.h (tutte extern "C", convenzione cdecl).
// ---------------------------------------------------------------------------
using ReadAsyncCb = void (*)(unsigned char* buf, uint32_t len, void* ctx);

struct RtlLib {
    void* handle = nullptr;
    std::string loadedName;

    uint32_t (*get_device_count)() = nullptr;
    int (*get_device_usb_strings)(uint32_t, char*, char*, char*) = nullptr;
    int (*open)(rtlsdr_dev**, uint32_t) = nullptr;
    int (*close)(rtlsdr_dev*) = nullptr;
    int (*set_sample_rate)(rtlsdr_dev*, uint32_t) = nullptr;
    uint32_t (*get_sample_rate)(rtlsdr_dev*) = nullptr;
    int (*set_center_freq)(rtlsdr_dev*, uint32_t) = nullptr;
    uint32_t (*get_center_freq)(rtlsdr_dev*) = nullptr;
    int (*set_tuner_gain_mode)(rtlsdr_dev*, int) = nullptr;
    int (*set_tuner_gain)(rtlsdr_dev*, int) = nullptr;
    int (*set_freq_correction)(rtlsdr_dev*, int) = nullptr;
    int (*reset_buffer)(rtlsdr_dev*) = nullptr;
    int (*read_async)(rtlsdr_dev*, ReadAsyncCb, void*, uint32_t, uint32_t) = nullptr;
    int (*cancel_async)(rtlsdr_dev*) = nullptr;
    // Facoltative (tollerate assenti nelle DLL piu' vecchie).
    int (*set_agc_mode)(rtlsdr_dev*, int) = nullptr;
    int (*set_bias_tee)(rtlsdr_dev*, int) = nullptr;

    bool complete() const
    {
        return handle && get_device_count && get_device_usb_strings && open &&
               close && set_sample_rate && get_sample_rate &&
               set_center_freq && set_tuner_gain_mode && set_tuner_gain &&
               set_freq_correction && reset_buffer && read_async &&
               cancel_async;
    }
};

static RtlLib loadRtlLib()
{
    static const char* kNames[] = {
#if defined(_WIN32)
        "rtlsdr.dll", "librtlsdr.dll",
#elif defined(__APPLE__)
        "librtlsdr.dylib", "librtlsdr.2.dylib",
#else
        "librtlsdr.so.2", "librtlsdr.so.0", "librtlsdr.so",
#endif
    };

    RtlLib lib;
#if defined(_WIN32)
    // Prima la cartella "driver" accanto all'eseguibile: basta scompattare
    // li' le DLL del fork rtl-sdr-blog, senza copia-incolla accanto all'exe.
    const std::string exeDir = exeDirectory();
    if (!exeDir.empty()) {
        for (const char* sub : {"\\driver\\", "\\"}) {
            for (const char* name : kNames) {
                std::string full = exeDir + sub + name;
                void* h = osOpenLibAt(full);
                if (!h) continue;
                lib.handle = h;
                lib.loadedName = full;
                break;
            }
            if (lib.handle) break;
        }
    }
#endif
    if (!lib.handle) {
        for (const char* name : kNames) {
            void* h = osOpenLib(name);
            if (!h) continue;
            lib.handle = h;
            lib.loadedName = name;
            break;
        }
    }
    if (!lib.handle) return lib;

    auto sym = [&](const char* n) { return osSym(lib.handle, n); };
    lib.get_device_count = (decltype(lib.get_device_count))sym("rtlsdr_get_device_count");
    lib.get_device_usb_strings = (decltype(lib.get_device_usb_strings))sym("rtlsdr_get_device_usb_strings");
    lib.open = (decltype(lib.open))sym("rtlsdr_open");
    lib.close = (decltype(lib.close))sym("rtlsdr_close");
    lib.set_sample_rate = (decltype(lib.set_sample_rate))sym("rtlsdr_set_sample_rate");
    lib.get_sample_rate = (decltype(lib.get_sample_rate))sym("rtlsdr_get_sample_rate");
    lib.set_center_freq = (decltype(lib.set_center_freq))sym("rtlsdr_set_center_freq");
    lib.get_center_freq = (decltype(lib.get_center_freq))sym("rtlsdr_get_center_freq");
    lib.set_tuner_gain_mode = (decltype(lib.set_tuner_gain_mode))sym("rtlsdr_set_tuner_gain_mode");
    lib.set_tuner_gain = (decltype(lib.set_tuner_gain))sym("rtlsdr_set_tuner_gain");
    lib.set_freq_correction = (decltype(lib.set_freq_correction))sym("rtlsdr_set_freq_correction");
    lib.reset_buffer = (decltype(lib.reset_buffer))sym("rtlsdr_reset_buffer");
    lib.read_async = (decltype(lib.read_async))sym("rtlsdr_read_async");
    lib.cancel_async = (decltype(lib.cancel_async))sym("rtlsdr_cancel_async");
    lib.set_agc_mode = (decltype(lib.set_agc_mode))sym("rtlsdr_set_agc_mode");
    lib.set_bias_tee = (decltype(lib.set_bias_tee))sym("rtlsdr_set_bias_tee");
    return lib;
}

static RtlLib& lib()
{
    static RtlLib l = loadRtlLib();
    return l;
}

// ---------------------------------------------------------------------------

bool RtlSdrSource::available() { return lib().complete(); }

std::string RtlSdrSource::libraryHint()
{
    if (available()) return "librtlsdr caricata (" + lib().loadedName + ")";
#if defined(_WIN32)
    return "rtlsdr.dll non trovata: scarica le DLL del driver rtl-sdr-blog "
           "(x86 o x64 come questa app) da "
           "github.com/rtlsdrblog/rtl-sdr-blog/releases e mettile nella "
           "cartella 'driver' accanto all'eseguibile (vengono caricate da "
           "sole). Serve anche il driver WinUSB (Zadig).";
#else
    return "librtlsdr non trovata: installala (es. 'sudo apt install "
           "librtlsdr0' o compila il fork rtl-sdr-blog).";
#endif
}

std::vector<RtlSdrSource::DeviceDesc> RtlSdrSource::enumerate()
{
    std::vector<DeviceDesc> out;
    if (!available()) return out;
    uint32_t count = lib().get_device_count();
    for (uint32_t i = 0; i < count; i++) {
        char manuf[256] = {0}, product[256] = {0}, serial[256] = {0};
        lib().get_device_usb_strings(i, manuf, product, serial);
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
    if (!available())
        throw std::runtime_error(libraryHint());
    if (lib().open(&dev_, deviceIndex) != 0)
        throw std::runtime_error("impossibile aprire il dispositivo RTL-SDR #" +
                                 std::to_string(deviceIndex) +
                                 " (driver WinUSB installato? chiavetta occupata?)");
    lib().set_sample_rate(dev_, uint32_t(rateHz_));
    lib().set_center_freq(dev_, uint32_t(freqHz_));
    lib().set_tuner_gain_mode(dev_, 0); // AGC di default
}

RtlSdrSource::~RtlSdrSource()
{
    stop();
    if (dev_) lib().close(dev_);
}

std::string RtlSdrSource::name() const { return "RTL-SDR"; }

bool RtlSdrSource::setCenterFrequency(double hz)
{
    // Il driver rtl-sdr-blog gestisce internamente l'upconverter della V4
    // per le frequenze sotto ~28 MHz: basta chiedere la frequenza voluta.
    lastTuneRc_ = lib().set_center_freq(dev_, uint32_t(hz));
    if (lastTuneRc_ != 0) return false;
    freqHz_ = hz;
    return true;
}

double RtlSdrSource::actualCenterFrequency() const
{
    if (dev_ && lib().get_center_freq) return double(lib().get_center_freq(dev_));
    return freqHz_;
}

bool RtlSdrSource::setSampleRate(double hz)
{
    if (lib().set_sample_rate(dev_, uint32_t(hz)) != 0) return false;
    rateHz_ = double(lib().get_sample_rate(dev_));
    return true;
}

bool RtlSdrSource::setGain(double gainDb)
{
    if (gainDb < 0.0)
        return lib().set_tuner_gain_mode(dev_, 0) == 0;
    if (lib().set_tuner_gain_mode(dev_, 1) != 0) return false;
    return lib().set_tuner_gain(dev_, int(gainDb * 10.0)) == 0;
}

bool RtlSdrSource::setPpmCorrection(int ppm)
{
    return lib().set_freq_correction(dev_, ppm) == 0;
}

bool RtlSdrSource::setRtlAgc(bool on)
{
    if (!lib().set_agc_mode) return false; // DLL troppo vecchia
    return lib().set_agc_mode(dev_, on ? 1 : 0) == 0;
}

bool RtlSdrSource::setBiasTee(bool on)
{
    if (!lib().set_bias_tee) return false; // DLL troppo vecchia
    return lib().set_bias_tee(dev_, on ? 1 : 0) == 0;
}

bool RtlSdrSource::start(IqCallback cb)
{
    if (running_.load()) return false;
    callback_ = std::move(cb);
    lib().reset_buffer(dev_);
    running_.store(true);
    worker_ = std::thread(&RtlSdrSource::workerLoop, this);
    return true;
}

void RtlSdrSource::stop()
{
    if (running_.exchange(false)) lib().cancel_async(dev_);
    // Join incondizionato: read_async puo' essere uscita da sola (es.
    // chiavetta scollegata) lasciando il thread joinable.
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
    lib().read_async(dev_, trampoline, &ctx, 16, 65536);
    running_.store(false);
}

} // namespace sdrjo
