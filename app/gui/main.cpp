//
// SdrJo GUI: finestra principale con spettro, waterfall, controllo del
// dispositivo e gestione dei moduli plugin.
//
// NOTA: questa GUI e' pensata per Windows (build con MSVC o MSYS2) ma
// compila anche su Linux/macOS. Su questo host di sviluppo non e' stata
// eseguita: vedi README per le istruzioni di build su Windows.
//
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "theme.hpp"

#include <sdrjo/audio/audio_output.hpp>
#include <sdrjo/dsp/correction.hpp>
#include <sdrjo/dsp/demod.hpp>
#include <sdrjo/dsp/ssb.hpp>
#include <sdrjo/util/band_plan.hpp>
#include <sdrjo/dsp/fft.hpp>
#include <sdrjo/dsp/vfo.hpp>
#include <sdrjo/dsp/wfm_stereo.hpp>
#include <sdrjo/module/module_loader.hpp>
#include <sdrjo/dsp/audio_filters.hpp>
#include <sdrjo/util/frequency_store.hpp>
#include <sdrjo/util/iq_recorder.hpp>
#include <sdrjo/web/cockpit_server.hpp>
#include <sdrjo/source/sample_source.hpp>
#include <sdrjo/source/file_source.hpp>
#include <sdrjo/source/rtl_sdr_source.hpp>
#include <sdrjo/util/ring_buffer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr size_t kChunkSize = 4096;   // blocco DSP per moduli/ascolto
constexpr int kWfWidth = 4096;        // colonne della texture waterfall
constexpr int kWaterfallRows = 256;

const char* kListenModeNames[] = {"Spento", "WFM stereo", "NFM",
                                  "AM", "USB", "LSB"};

// Font caricati all'avvio (nullptr = fallback al font di default).
ImFont* gFontUi = nullptr;
ImFont* gFontMonoBig = nullptr;   // frequenzimetro
ImFont* gFontMonoSmall = nullptr; // etichette S-meter

struct AppState : public sdrjo::IModuleHost {
    std::unique_ptr<sdrjo::ISampleSource> source;
    sdrjo::RingBuffer<sdrjo::cfloat> iqRing{1 << 20};

    double freqMHz = 100.0;
    double sampleRate = 2.4e6;
    float gainDb = -1.0f; // <0 = AGC

    // Spettro ad alta risoluzione: FFT selezionabile, calcolata ~30 volte
    // al secondo sugli ultimi campioni catturati (non per ogni chunk DSP).
    int fftSize = 16384;
    std::vector<sdrjo::cfloat> captureBuf;
    size_t capturePos = 0;
    size_t captureFilled = 0;
    size_t samplesSinceFft = 0;
    std::vector<float> spectrum = std::vector<float>(16384, -120.0f);

    std::vector<float> waterfall =
        std::vector<float>(size_t(kWfWidth) * kWaterfallRows, -120.0f);
    int waterfallHead = 0;
    GLuint waterfallTex = 0;

    // Regolazioni di spettro e waterfall.
    float rangeMinDb = -110.0f;      // fondo scala (luminosita')
    float rangeMaxDb = -10.0f;       // tetto scala (contrasto)
    int wfRows = kWaterfallRows;     // memoria del waterfall (righe)
    int wfSpeedDiv = 4;              // 1 riga ogni N FFT (velocita')
    int wfSpeedCounter = 0;
    int wfPalette = 0;               // 0 classica, 1 grigi, 2 fuoco
    float wfSplit = 0.42f;           // quota di altezza dello spettro

    std::vector<sdrjo::LoadedModule> modules;
    std::vector<std::string> logLines;
    std::mutex logMutex;

    sdrjo::CockpitServer cockpit;
    std::mutex spectrumMutex; // lo spettro e' letto anche dal thread HTTP

    // Canalizzazione: un VFO per ogni modulo che chiede un rate diverso
    // da quello dell'hardware (piu' moduli in ascolto in parallelo).
    struct ModuleChannel {
        std::unique_ptr<sdrjo::dsp::Vfo> vfo; // nullo = passthrough
        std::vector<sdrjo::cfloat> buf;
    };
    std::vector<ModuleChannel> channels;

    sdrjo::dsp::DcBlocker dcBlocker;
    sdrjo::AudioOutput audio;
    sdrjo::IqRecorder recorder;

    // Ricevitore d'ascolto: VFO spostabile con un click sullo spettro
    // + demodulatore selezionabile (indipendente dai moduli decoder).
    enum class ListenMode { Off, WfmStereo, Nfm, Am, Usb, Lsb };
    ListenMode listenMode = ListenMode::Off;
    double listenOffsetHz = 0.0;
    float volume = 0.8f;
    std::unique_ptr<sdrjo::dsp::Vfo> listenVfo;
    std::unique_ptr<sdrjo::dsp::WfmStereoDemodulator> wfmDemod;
    std::unique_ptr<sdrjo::dsp::FmDemodulator> nfmDemod;
    std::unique_ptr<sdrjo::dsp::AmDemodulator> amDemod;
    std::unique_ptr<sdrjo::dsp::SsbDemodulator> ssbDemod;
    sdrjo::dsp::Agc listenAgc;
    std::vector<float> audioL, audioR, audioInterleaved, audioMono;

    // Impostazioni audio (pannello Audio).
    int audioDeviceIndex = -1;              // -1 = predefinito
    int audioRateHz = 48000;
    std::vector<std::string> audioDevices = sdrjo::AudioOutput::listDevices();

    // Vista tipata sulla sorgente quando e' una RTL-SDR (per bias-T/PPM).
    sdrjo::RtlSdrSource* rtl = nullptr;
    bool tunerAgc = true;
    bool biasTee = false;
    int ppmCorrection = 0;

    // Zoom dello spettro: frazioni dello span pieno dell'hardware.
    double viewSpanFrac = 1.0;
    double viewCenterFrac = 0.5;

    // Passo di sintonia (rotellina/click) e larghezza canale regolabile.
    double snapHz = 12500.0;
    double listenBwHz = 0.0;

    // Squelch sul canale di ascolto.
    bool squelchOn = false;
    float squelchDb = -50.0f;
    float chanLevelDb = -120.0f;
    bool squelchOpen = true;

    // Filtri audio (passa-alto/passa-basso).
    float audioHighPassHz = 0.0f;
    float audioLowPassHz = 0.0f;
    sdrjo::dsp::AudioFilterChain filterL, filterR;

    // Filtro di canale con la larghezza scelta dal grafico (NFM/AM).
    std::unique_ptr<sdrjo::dsp::FirFilter> chanFilter;

    // Frequency manager (persistente su file).
    sdrjo::FrequencyStore freqStore;
    std::string freqStorePath;

    // Posizione della stazione (per mappa ADS-B, distanze, satelliti),
    // salvata in sdrjo.cfg accanto all'eseguibile.
    double stationLat = 0.0;
    double stationLon = 0.0;
    std::string configPath;

    // Comandi arrivati dal Cockpit web (thread HTTP): applicati nel loop
    // principale per non toccare lo stato da thread diversi.
    std::mutex remoteMutex;
    double pendingTuneHz = -1.0;
    std::string pendingMode;

    void ensureAudio()
    {
        if (!audio.isActive())
            audio.start(double(audioRateHz), 2, audioDeviceIndex);
    }

    // ---- IModuleHost ----
    void log(const std::string& mod, const std::string& text) override
    {
        std::lock_guard<std::mutex> lk(logMutex);
        logLines.push_back("[" + mod + "] " + text);
        if (logLines.size() > 500)
            logLines.erase(logLines.begin(), logLines.begin() + 100);
    }

    void playAudio(const float* samples, size_t n, double) override
    {
        ensureAudio();
        audio.writeMono(samples, n);
    }

    bool requestTune(double freqHz, double rateHz) override
    {
        if (!source) return false;
        source->setCenterFrequency(freqHz);
        source->setSampleRate(rateHz);
        freqMHz = freqHz / 1e6;
        if (rateHz != sampleRate) {
            sampleRate = rateHz;
            channelsDirty = true; // i VFO vanno riprogettati
        }
        return true;
    }

    bool channelsDirty = false;
};

// (Ri)costruisce i VFO dei moduli per il sample rate corrente.
void rebuildChannels(AppState& app)
{
    app.channels.clear();
    for (auto& lm : app.modules) {
        AppState::ModuleChannel ch;
        double want = lm.module()->info().requiredSampleRateHz;
        if (want > 0 && want < app.sampleRate * 0.999) {
            ch.vfo = std::make_unique<sdrjo::dsp::Vfo>(app.sampleRate, want,
                                                       0.0);
        }
        app.channels.push_back(std::move(ch));
    }
}

// Config minimale (chiave=valore) accanto all'eseguibile.
void loadConfig(AppState& app)
{
    FILE* f = std::fopen(app.configPath.c_str(), "r");
    if (!f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        double v = 0;
        if (std::sscanf(line, "station_lat=%lf", &v) == 1) app.stationLat = v;
        else if (std::sscanf(line, "station_lon=%lf", &v) == 1)
            app.stationLon = v;
    }
    std::fclose(f);
}

void saveConfig(AppState& app)
{
    FILE* f = std::fopen(app.configPath.c_str(), "w");
    if (!f) return;
    std::fprintf(f, "station_lat=%.6f\nstation_lon=%.6f\n", app.stationLat,
                 app.stationLon);
    std::fclose(f);
}

void applyStationToModules(AppState& app)
{
    for (auto& lm : app.modules)
        lm.module()->setStationLocation(app.stationLat, app.stationLon);
}

// Larghezza di banda mostrata/usata per ogni modalita' di ascolto.
double listenBandwidthHz(AppState::ListenMode m)
{
    switch (m) {
    case AppState::ListenMode::WfmStereo: return 200000.0;
    case AppState::ListenMode::Nfm: return 12500.0;
    case AppState::ListenMode::Am: return 9000.0;
    case AppState::ListenMode::Usb:
    case AppState::ListenMode::Lsb: return 2700.0;
    default: return 0.0;
    }
}

// Ricostruisce il filtro di canale/SSB per la larghezza corrente.
void rebuildChanFilter(AppState& app)
{
    app.chanFilter.reset();
    if (app.listenMode == AppState::ListenMode::Off ||
        app.listenMode == AppState::ListenMode::WfmStereo)
        return;
    double bw = std::clamp(app.listenBwHz, 500.0, 46000.0);
    if (app.ssbDemod) {
        app.ssbDemod = std::make_unique<sdrjo::dsp::SsbDemodulator>(
            48000.0, app.listenMode == AppState::ListenMode::Usb, bw);
    } else {
        app.chanFilter = std::make_unique<sdrjo::dsp::FirFilter>(
            sdrjo::dsp::designLowPass(48000.0, bw / 2.0, 129));
    }
}

// (Ri)costruisce la catena di ascolto per modalita'/rate correnti.
void rebuildListener(AppState& app)
{
    app.listenVfo.reset();
    app.wfmDemod.reset();
    app.nfmDemod.reset();
    app.amDemod.reset();
    app.ssbDemod.reset();
    if (app.listenMode == AppState::ListenMode::Off) return;

    double chanRate =
        (app.listenMode == AppState::ListenMode::WfmStereo) ? 240000.0
                                                            : 48000.0;
    if (chanRate > app.sampleRate) chanRate = app.sampleRate;
    app.listenVfo = std::make_unique<sdrjo::dsp::Vfo>(app.sampleRate, chanRate,
                                                      app.listenOffsetHz);
    switch (app.listenMode) {
    case AppState::ListenMode::WfmStereo:
        app.wfmDemod = std::make_unique<sdrjo::dsp::WfmStereoDemodulator>(
            240000.0, 48000.0);
        break;
    case AppState::ListenMode::Nfm:
        app.nfmDemod =
            std::make_unique<sdrjo::dsp::FmDemodulator>(48000.0, 3000.0);
        break;
    case AppState::ListenMode::Am:
        app.amDemod = std::make_unique<sdrjo::dsp::AmDemodulator>();
        break;
    case AppState::ListenMode::Usb:
    case AppState::ListenMode::Lsb:
        app.ssbDemod = std::make_unique<sdrjo::dsp::SsbDemodulator>(
            48000.0, app.listenMode == AppState::ListenMode::Usb);
        break;
    default:
        break;
    }
    app.listenBwHz = listenBandwidthHz(app.listenMode);
    rebuildChanFilter(app);
    app.filterL.reset();
    app.filterR.reset();
}

void updateSpectrum(AppState& app)
{
    static std::vector<sdrjo::cfloat> chunk(kChunkSize);
    while (app.iqRing.available() >= kChunkSize) {
        app.iqRing.read(chunk.data(), kChunkSize);
        app.dcBlocker.processInPlace(chunk.data(), kChunkSize);
        if (app.recorder.isRecording())
            app.recorder.write(chunk.data(), kChunkSize);

        // Cattura circolare per lo spettro (FFT calcolata piu' sotto,
        // a frequenza fissa: molto piu' leggero di una FFT per chunk).
        if (app.captureBuf.size() != size_t(app.fftSize)) {
            app.captureBuf.assign(size_t(app.fftSize), sdrjo::cfloat(0, 0));
            app.capturePos = 0;
            app.captureFilled = 0;
        }
        for (size_t i = 0; i < kChunkSize; i++) {
            app.captureBuf[app.capturePos] = chunk[i];
            app.capturePos = (app.capturePos + 1) % app.captureBuf.size();
        }
        app.captureFilled =
            std::min(app.captureFilled + kChunkSize, app.captureBuf.size());
        app.samplesSinceFft += kChunkSize;

        // Distribuisci a ogni modulo il SUO canale (VFO dedicato).
        for (size_t m = 0; m < app.modules.size(); m++) {
            auto& ch = app.channels[m];
            if (ch.vfo) {
                ch.buf.clear();
                ch.vfo->process(chunk.data(), kChunkSize, ch.buf);
                if (!ch.buf.empty())
                    app.modules[m].module()->processIq(ch.buf.data(),
                                                       ch.buf.size());
            } else {
                app.modules[m].module()->processIq(chunk.data(), kChunkSize);
            }
        }

        // Catena di ascolto.
        if (app.listenMode != AppState::ListenMode::Off && app.listenVfo) {
            static std::vector<sdrjo::cfloat> lchan;
            lchan.clear();
            app.listenVfo->process(chunk.data(), kChunkSize, lchan);
            if (lchan.empty()) continue;

            // Filtro di canale (larghezza scelta trascinando i bordi).
            if (app.chanFilter) {
                static std::vector<sdrjo::cfloat> filt;
                filt.resize(lchan.size());
                app.chanFilter->process(lchan.data(), lchan.size(),
                                        filt.data());
                lchan.swap(filt);
            }

            // Livello del canale (per squelch e indicatore).
            double pw = 0;
            for (const auto& c : lchan) pw += std::norm(c);
            float level = 10.0f * std::log10(float(pw / double(lchan.size())) +
                                             1e-12f);
            app.chanLevelDb += 0.3f * (level - app.chanLevelDb);
            if (app.squelchOn) {
                // Isteresi di 2 dB attorno alla soglia.
                app.squelchOpen = app.squelchOpen
                                      ? (app.chanLevelDb > app.squelchDb - 2)
                                      : (app.chanLevelDb > app.squelchDb + 2);
            } else {
                app.squelchOpen = true;
            }

            app.ensureAudio();
            if (app.wfmDemod) {
                app.audioL.clear();
                app.audioR.clear();
                app.wfmDemod->process(lchan.data(), lchan.size(), app.audioL,
                                      app.audioR);
                app.filterL.process(app.audioL.data(), app.audioL.size());
                app.filterR.process(app.audioR.data(), app.audioR.size());
                float g = app.squelchOpen ? app.volume : 0.0f;
                app.audioInterleaved.resize(app.audioL.size() * 2);
                for (size_t i = 0; i < app.audioL.size(); i++) {
                    app.audioInterleaved[2 * i] = g * app.audioL[i];
                    app.audioInterleaved[2 * i + 1] = g * app.audioR[i];
                }
                app.audio.write(app.audioInterleaved.data(),
                                app.audioInterleaved.size());
                // Streaming web: mix mono (L+R)/2 gia' regolato.
                static std::vector<float> mix;
                mix.resize(app.audioL.size());
                for (size_t i = 0; i < mix.size(); i++)
                    mix[i] = 0.5f * (app.audioInterleaved[2 * i] +
                                     app.audioInterleaved[2 * i + 1]);
                app.cockpit.pushAudio(mix.data(), mix.size());
            } else {
                app.audioMono.clear();
                if (app.nfmDemod) {
                    app.audioMono.resize(lchan.size());
                    app.nfmDemod->process(lchan.data(), lchan.size(),
                                          app.audioMono.data());
                } else if (app.amDemod) {
                    app.audioMono.resize(lchan.size());
                    app.amDemod->process(lchan.data(), lchan.size(),
                                         app.audioMono.data());
                    app.listenAgc.process(app.audioMono.data(),
                                          app.audioMono.size());
                } else if (app.ssbDemod) {
                    app.ssbDemod->process(lchan.data(), lchan.size(),
                                          app.audioMono);
                    app.listenAgc.process(app.audioMono.data(),
                                          app.audioMono.size());
                }
                app.filterL.process(app.audioMono.data(),
                                    app.audioMono.size());
                float g = app.squelchOpen ? app.volume : 0.0f;
                for (auto& v : app.audioMono) v *= g;
                app.audio.writeMono(app.audioMono.data(),
                                    app.audioMono.size());
                app.cockpit.pushAudio(app.audioMono.data(),
                                      app.audioMono.size());
            }
        }
    }

    // FFT ad alta risoluzione a ~30 Hz sugli ultimi fftSize campioni.
    if (app.captureFilled >= size_t(app.fftSize) &&
        app.samplesSinceFft >= size_t(app.sampleRate / 30.0)) {
        app.samplesSinceFft = 0;

        static std::vector<sdrjo::cfloat> lin;
        lin.resize(app.captureBuf.size());
        std::copy(app.captureBuf.begin() + ptrdiff_t(app.capturePos),
                  app.captureBuf.end(), lin.begin());
        std::copy(app.captureBuf.begin(),
                  app.captureBuf.begin() + ptrdiff_t(app.capturePos),
                  lin.begin() +
                      ptrdiff_t(app.captureBuf.size() - app.capturePos));

        {
            std::lock_guard<std::mutex> lk(app.spectrumMutex);
            app.spectrum.resize(lin.size());
            sdrjo::dsp::powerSpectrumDb(lin.data(), lin.size(),
                                        app.spectrum.data());
        }

        // Riga del waterfall (max-decimata a kWfWidth colonne).
        if (++app.wfSpeedCounter >= app.wfSpeedDiv) {
            app.wfSpeedCounter = 0;
            float* row = &app.waterfall[size_t(app.waterfallHead) * kWfWidth];
            const size_t n = app.spectrum.size();
            for (int x = 0; x < kWfWidth; x++) {
                size_t b0 = size_t(x) * n / kWfWidth;
                size_t b1 = std::max(b0 + 1, size_t(x + 1) * n / kWfWidth);
                float v = -160.0f;
                for (size_t b = b0; b < b1 && b < n; b++)
                    v = std::max(v, app.spectrum[b]);
                row[x] = v;
            }
            app.waterfallHead = (app.waterfallHead + 1) % app.wfRows;
        }
    }
}

// Cambia il numero di righe di storia del waterfall.
void resizeWaterfall(AppState& app, int rows)
{
    app.wfRows = rows;
    app.waterfall.assign(size_t(kWfWidth) * size_t(rows), -120.0f);
    app.waterfallHead = 0;
}

void uploadWaterfallTexture(AppState& app)
{
    static std::vector<uint32_t> pixels;
    pixels.resize(size_t(kWfWidth) * size_t(app.wfRows));

    const float lo = app.rangeMinDb;
    const float span = std::max(1.0f, app.rangeMaxDb - app.rangeMinDb);
    const int palette = app.wfPalette;
    auto colorize = [&](float db) -> uint32_t {
        float t = std::clamp((db - lo) / span, 0.0f, 1.0f);
        uint8_t r, g, b;
        switch (palette) {
        case 1: // grigi
            r = g = b = uint8_t(255.0f * t);
            break;
        case 2: // fuoco: nero -> rosso -> giallo -> bianco
            r = uint8_t(255.0f * std::clamp(t * 3.0f, 0.0f, 1.0f));
            g = uint8_t(255.0f * std::clamp(t * 3.0f - 1.0f, 0.0f, 1.0f));
            b = uint8_t(255.0f * std::clamp(t * 3.0f - 2.0f, 0.0f, 1.0f));
            break;
        default: // classica: blu -> ciano -> giallo
            r = uint8_t(255.0f * std::clamp(t * 2.5f - 1.2f, 0.0f, 1.0f));
            g = uint8_t(255.0f * std::clamp(t * 2.0f - 0.5f, 0.0f, 1.0f));
            b = uint8_t(255.0f * std::clamp(t * 3.0f, 0.0f, 1.0f) *
                        (1.0f - 0.5f * t));
            break;
        }
        return 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
    };

    // Riga piu' recente in alto: il waterfall scende verso il basso.
    for (int row = 0; row < app.wfRows; row++) {
        int src = (app.waterfallHead + app.wfRows - 1 - row) % app.wfRows;
        for (int x = 0; x < kWfWidth; x++)
            pixels[size_t(row) * kWfWidth + size_t(x)] =
                colorize(app.waterfall[size_t(src) * kWfWidth + size_t(x)]);
    }
    if (!app.waterfallTex) {
        glGenTextures(1, &app.waterfallTex);
        glBindTexture(GL_TEXTURE_2D, app.waterfallTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, app.waterfallTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kWfWidth, app.wfRows, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}

void drawDeviceSection(AppState& app)
{

    if (!app.source) {
        if (!sdrjo::RtlSdrSource::available()) {
            // librtlsdr caricata a runtime: se manca, spiega cosa fare.
            ImGui::TextWrapped("%s", sdrjo::RtlSdrSource::libraryHint().c_str());
        } else {
            auto devices = sdrjo::RtlSdrSource::enumerate();
            ImGui::TextDisabled("%s", sdrjo::RtlSdrSource::libraryHint().c_str());
            ImGui::Text("RTL-SDR trovate: %zu", devices.size());
            if (devices.empty()) {
#if defined(_WIN32)
                ImGui::TextWrapped(
                    "La libreria e' caricata ma nessuna chiavetta risponde. "
                    "Su Windows libusb vede la chiavetta SOLO con il driver "
                    "WinUSB:\n"
                    "1. collega la chiavetta;\n"
                    "2. apri Zadig (zadig.akeo.ie) come amministratore;\n"
                    "3. Options > List All Devices;\n"
                    "4. seleziona 'Bulk-In, Interface (Interface 0)' "
                    "(o RTL2838UHIDIR);\n"
                    "5. driver di destinazione WinUSB > Replace Driver;\n"
                    "6. scollega e ricollega la chiavetta.\n"
                    "L'elenco qui si aggiorna da solo.");
#else
                ImGui::TextWrapped("Nessuna chiavetta rilevata: controlla il "
                                   "cavo USB e i permessi udev.");
#endif
            }
            for (auto& d : devices) {
                ImGui::BulletText("#%u %s (%s)", d.index, d.name.c_str(),
                                  d.serial.c_str());
            }
            if (!devices.empty() && ImGui::Button("Avvia")) {
                try {
                    auto src = std::make_unique<sdrjo::RtlSdrSource>(devices[0].index);
                    src->setCenterFrequency(app.freqMHz * 1e6);
                    src->setSampleRate(app.sampleRate);
                    src->start([&app](const sdrjo::cfloat* s, size_t n) {
                        app.iqRing.write(s, n);
                    });
                    app.rtl = src.get();
                    app.tunerAgc = true;
                    app.source = std::move(src);
                    rebuildListener(app);
                } catch (const std::exception& e) {
                    app.log("Dispositivo", e.what());
                }
            }
        }
    } else {
        ImGui::Text("%s attivo", app.source->name().c_str());
        double freq = app.freqMHz;
        if (ImGui::InputDouble("Frequenza (MHz)", &freq, 0.1, 1.0, "%.4f")) {
            app.freqMHz = freq;
            app.source->setCenterFrequency(freq * 1e6);
        }

        // Sample rate: preset tipici delle RTL2832U.
        static const double kRates[] = {250000, 1024000, 1800000, 1920000,
                                        2048000, 2400000, 2560000, 3200000};
        static const char* kRateNames[] = {"0.25 MS/s", "1.024 MS/s",
                                           "1.8 MS/s",  "1.92 MS/s",
                                           "2.048 MS/s", "2.4 MS/s",
                                           "2.56 MS/s", "3.2 MS/s"};
        int rateIdx = 5;
        for (int i = 0; i < 8; i++)
            if (std::fabs(kRates[i] - app.sampleRate) < 1000) rateIdx = i;
        if (ImGui::Combo("Sample rate", &rateIdx, kRateNames, 8)) {
            app.source->setSampleRate(kRates[rateIdx]);
            app.sampleRate = app.source->sampleRate();
            app.channelsDirty = true;
            rebuildListener(app);
        }

        if (ImGui::Button("Ferma")) {
            app.source->stop();
            app.source.reset();
            app.rtl = nullptr;
        }

        // Controlli della chiavetta (stile SDR++).
        if (app.rtl) {
            ImGui::SeparatorText("Chiavetta");
            if (ImGui::Checkbox("AGC del tuner", &app.tunerAgc)) {
                app.rtl->setGain(app.tunerAgc ? -1.0 : double(app.gainDb));
                if (app.tunerAgc) app.gainDb = -1.0f;
                else if (app.gainDb < 0) app.gainDb = 28.0f;
            }
            if (!app.tunerAgc) {
                if (ImGui::SliderFloat("Guadagno (dB)", &app.gainDb, 0.0f,
                                       49.6f, "%.1f"))
                    app.rtl->setGain(double(app.gainDb));
            }
            if (ImGui::Checkbox("Bias-T (alimenta LNA)", &app.biasTee)) {
                if (!app.rtl->setBiasTee(app.biasTee)) {
                    app.log("Dispositivo",
                            "bias-T non supportato da questa rtlsdr.dll");
                    app.biasTee = false;
                }
            }
            if (ImGui::InputInt("Correzione PPM", &app.ppmCorrection))
                app.rtl->setPpmCorrection(app.ppmCorrection);
        }


        ImGui::SeparatorText("Registrazione IQ");
        if (!app.recorder.isRecording()) {
            if (ImGui::Button("Registra")) {
                char name[64];
                std::snprintf(name, sizeof(name), "sdrjo_%.4fMHz.bin",
                              app.freqMHz);
                app.recorder.start(name, app.freqMHz * 1e6, app.sampleRate);
            }
        } else {
            if (ImGui::Button("Stop registrazione")) app.recorder.stop();
            ImGui::SameLine();
            ImGui::Text("%s  %.1f s (%.1f MB)", app.recorder.path().c_str(),
                        app.recorder.secondsWritten(),
                        double(app.recorder.bytesWritten()) / 1e6);
        }
    }

    // Replay di una registrazione (disponibile anche senza hardware).
    if (!app.source) {
        ImGui::SeparatorText("Riproduci registrazione");
        static char replayPath[260] = "";
        ImGui::InputText("file IQ", replayPath, sizeof(replayPath));
        if (ImGui::Button("Riproduci") && replayPath[0]) {
            auto src = std::make_unique<sdrjo::FileSource>(replayPath,
                                                           app.sampleRate);
            src->setCenterFrequency(app.freqMHz * 1e6);
            src->start([&app](const sdrjo::cfloat* s, size_t n) {
                app.iqRing.write(s, n);
            });
            app.source = std::move(src);
        }
    }

    // Posizione dell'antenna: serve alla mappa ADS-B (marker + cerchi di
    // portata + distanze) e in futuro ai passaggi satellite.
    ImGui::SeparatorText("Posizione antenna");
    bool posChanged = false;
    posChanged |= ImGui::InputDouble("Latitudine", &app.stationLat, 0, 0,
                                     "%.5f");
    posChanged |= ImGui::InputDouble("Longitudine", &app.stationLon, 0, 0,
                                     "%.5f");
    if (posChanged) {
        saveConfig(app);
        applyStationToModules(app);
    }
    if (app.stationLat == 0.0 && app.stationLon == 0.0)
        ImGui::TextDisabled("(imposta lat/lon per vederti sulla mappa ADS-B)");
}

// Con lo zoom attivo tiene la frequenza sintonizzata al centro della
// vista (cosi' seguendo la sintonia lo spettro "scorre" sotto il VFO).
void centerViewOnTuned(AppState& app)
{
    if (app.viewSpanFrac >= 0.999) return;
    double tuned = app.freqMHz * 1e6 + app.listenOffsetHz;
    double f0 = app.freqMHz * 1e6 - app.sampleRate / 2.0;
    app.viewCenterFrac = std::clamp((tuned - f0) / app.sampleRate,
                                    app.viewSpanFrac / 2.0,
                                    1.0 - app.viewSpanFrac / 2.0);
}

// Applica la frequenza sintonizzata: VFO se dentro lo span, altrimenti
// risintonizza l'hardware (mostrando cosi' le frequenze successive);
// in entrambi i casi la vista zoomata resta centrata sulla sintonia.
void applyTunedFrequency(AppState& app, double f)
{
    f = std::clamp(f, 0.0, 1.999e9);
    double center = app.freqMHz * 1e6;
    if (app.source && std::fabs(f - center) < app.sampleRate * 0.45) {
        app.listenOffsetHz = f - center;
    } else {
        app.freqMHz = f / 1e6;
        if (app.source) app.source->setCenterFrequency(f);
        app.listenOffsetHz = 0.0;
    }
    if (app.listenVfo) app.listenVfo->setOffset(app.listenOffsetHz);
    centerViewOnTuned(app);
}

// Frequenzimetro a cifre stile SDR Console: rotellina su una cifra per
// incrementarla/decrementarla, click sulla meta' alta = +, bassa = -.
void drawFrequencyDial(AppState& app)
{
    ImFont* font = gFontMonoBig ? gFontMonoBig : ImGui::GetFont();
    const float fh = gFontMonoBig ? gFontMonoBig->FontSize
                                  : ImGui::GetFontSize() * 1.8f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 origin = ImGui::GetCursorScreenPos();

    int64_t f = int64_t(std::llround(app.freqMHz * 1e6 + app.listenOffsetHz));
    f = std::clamp<int64_t>(f, 0, 1999999999);

    const float charW = font->CalcTextSizeA(fh, FLT_MAX, 0, "0").x;
    const float sepW = charW * 0.5f;
    const ImU32 colLit = ImGui::GetColorU32(ImVec4(0.22f, 0.71f, 1.0f, 1.0f));
    const ImU32 colDim = ImGui::GetColorU32(ImVec4(0.25f, 0.32f, 0.40f, 1.0f));
    const ImU32 colHov = ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 1.0f));

    // Centra il visore nella finestra.
    float totalW = 10 * charW + 3 * sepW + charW * 2.2f;
    float x = origin.x +
              std::max(0.0f, (ImGui::GetContentRegionAvail().x - totalW) / 2);
    float y = origin.y;

    bool seenNonZero = false;
    int64_t place = 1000000000;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    for (int i = 0; i < 10; i++) {
        int digit = int((f / place) % 10);
        if (digit != 0) seenNonZero = true;

        bool hov = mouse.x >= x && mouse.x < x + charW && mouse.y >= y &&
                   mouse.y < y + fh && ImGui::IsWindowHovered();
        char c = char('0' + digit);
        dl->AddText(font, fh, ImVec2(x, y),
                    hov ? colHov : (seenNonZero ? colLit : colDim), &c,
                    &c + 1);
        if (hov) {
            dl->AddLine(ImVec2(x, y + fh + 1), ImVec2(x + charW, y + fh + 1),
                        colHov, 2.0f);
            float wheel = ImGui::GetIO().MouseWheel;
            int64_t delta = 0;
            if (wheel != 0.0f) delta = int64_t(wheel) * place;
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                delta = (mouse.y < y + fh / 2) ? place : -place;
            if (delta != 0)
                applyTunedFrequency(app, double(f + delta));
        }
        x += charW;
        if (i == 0 || i == 3 || i == 6) {
            dl->AddText(font, fh, ImVec2(x, y), colDim, ".");
            x += sepW;
        }
        place /= 10;
    }
    dl->AddText(nullptr, 0, ImVec2(x + 6, y + fh * 0.45f),
                ImGui::GetColorU32(ImVec4(0.46f, 0.52f, 0.60f, 1.0f)), "Hz");

    ImGui::Dummy(ImVec2(totalW, fh + 6.0f));
}

// S-meter analogico "d'epoca": quadrante crema, zona rossa oltre S9,
// lancetta smorzata pilotata dal livello del canale di ascolto.
void drawSMeter(AppState& app)
{
    static float needle = 0.0f;
    float target = std::clamp((app.chanLevelDb + 120.0f) / 120.0f, 0.0f, 1.0f);
    needle += 0.12f * (target - needle);

    const float w = ImGui::GetContentRegionAvail().x;
    const float h = 120.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Cornice scura e quadrante crema.
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                      IM_COL32(28, 24, 20, 255), 8.0f);
    dl->AddRectFilled(ImVec2(p.x + 5, p.y + 5),
                      ImVec2(p.x + w - 5, p.y + h - 5),
                      IM_COL32(238, 229, 203, 255), 6.0f);

    const ImVec2 pivot(p.x + w * 0.5f, p.y + h - 16.0f);
    const float radius = std::min(w * 0.44f, h - 42.0f);
    auto tip = [&](float t, float r) {
        float ang = (-50.0f + 100.0f * t) * 3.14159265f / 180.0f;
        return ImVec2(pivot.x + r * std::sin(ang), pivot.y - r * std::cos(ang));
    };

    // Arco della scala: nero fino a S9 (t=0.55), rosso oltre.
    for (int seg = 0; seg < 40; seg++) {
        float t0 = seg / 40.0f, t1 = (seg + 1) / 40.0f;
        ImU32 col = (t0 >= 0.55f) ? IM_COL32(178, 34, 34, 255)
                                  : IM_COL32(30, 26, 22, 255);
        dl->AddLine(tip(t0, radius), tip(t1, radius), col,
                    t0 >= 0.55f ? 3.5f : 2.0f);
    }

    // Tacche e numeri: S1..S9, poi +20/+40/+60 dB.
    ImFont* small = gFontMonoSmall ? gFontMonoSmall : ImGui::GetFont();
    float smallSize = gFontMonoSmall ? gFontMonoSmall->FontSize : 12.0f;
    for (int k = 1; k <= 9; k += 2) {
        float t = 0.55f * float(k) / 9.0f;
        dl->AddLine(tip(t, radius - 5), tip(t, radius + 3),
                    IM_COL32(30, 26, 22, 255), 2.0f);
        char lbl[4];
        std::snprintf(lbl, sizeof(lbl), "%d", k);
        ImVec2 lp = tip(t, radius - 15); // etichette dentro l'arco
        dl->AddText(small, smallSize, ImVec2(lp.x - 4, lp.y - 6),
                    IM_COL32(30, 26, 22, 255), lbl);
    }
    const float tOver[3] = {0.7f, 0.85f, 1.0f};
    const char* lblOver[3] = {"+20", "+40", "+60"};
    for (int k = 0; k < 3; k++) {
        dl->AddLine(tip(tOver[k], radius - 5), tip(tOver[k], radius + 3),
                    IM_COL32(178, 34, 34, 255), 2.0f);
        ImVec2 lp = tip(tOver[k], radius - 17);
        dl->AddText(small, smallSize, ImVec2(lp.x - 9, lp.y - 6),
                    IM_COL32(178, 34, 34, 255), lblOver[k]);
    }

    // Scritte del quadrante e lancetta.
    dl->AddText(small, smallSize, ImVec2(p.x + 14, p.y + h - 24),
                IM_COL32(30, 26, 22, 200), "S-METER");
    char dbLbl[24];
    std::snprintf(dbLbl, sizeof(dbLbl), "%.0f dB", double(app.chanLevelDb));
    dl->AddText(small, smallSize, ImVec2(p.x + w - 60, p.y + h - 24),
                IM_COL32(30, 26, 22, 200), dbLbl);

    dl->AddLine(pivot, tip(needle, radius - 4), IM_COL32(150, 20, 20, 255),
                2.4f);
    dl->AddCircleFilled(pivot, 5.5f, IM_COL32(30, 26, 22, 255));

    ImGui::Dummy(ImVec2(w, h + 4.0f));
}

// Passo "bello" per le tacche di frequenza (1/2/5 * 10^k).
double niceStep(double raw)
{
    double mag = std::pow(10.0, std::floor(std::log10(raw)));
    double n = raw / mag;
    if (n < 1.5) return mag;
    if (n < 3.5) return 2.0 * mag;
    if (n < 7.5) return 5.0 * mag;
    return 10.0 * mag;
}

ImU32 bandColor(const char* cat, float alpha)
{
    struct { const char* cat; ImVec4 c; } table[] = {
        {"ham", {0.24f, 0.86f, 0.59f, 1}},  {"bc", {0.22f, 0.71f, 1.0f, 1}},
        {"aero", {1.0f, 0.71f, 0.33f, 1}},  {"sat", {0.78f, 0.57f, 0.92f, 1}},
        {"marine", {0.30f, 0.82f, 0.88f, 1}}, {"ism", {0.96f, 0.44f, 0.40f, 1}},
        {"cb", {1.0f, 0.82f, 0.40f, 1}},    {"pmr", {0.96f, 0.44f, 0.40f, 1}},
        {"nav", {0.62f, 0.62f, 0.62f, 1}},
    };
    for (auto& e : table)
        if (std::strcmp(e.cat, cat) == 0)
            return ImGui::GetColorU32(ImVec4(e.c.x, e.c.y, e.c.z, alpha));
    return ImGui::GetColorU32(ImVec4(0.5f, 0.5f, 0.5f, alpha));
}

// Spettro interattivo: guide di banda, scala in MHz, click per spostare
// il VFO di ascolto, doppio click per risintonizzare l'hardware.
void drawSpectrumPanel(AppState& app)
{
    // Layout proporzionale: segue il ridimensionamento della finestra.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sideW = std::clamp(vp->WorkSize.x * 0.24f, 300.0f, 420.0f);
    const float specHWin = std::max(300.0f, (vp->WorkSize.y - 30) * 0.62f);
    ImGui::SetNextWindowPos(ImVec2(sideW + 20, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - sideW - 30, specHWin),
                             ImGuiCond_Always);
    ImGui::Begin("Spettro", nullptr, ImGuiWindowFlags_NoMove);

    // Frequenza sintonizzata: cifre cliccabili e scrollabili.
    drawFrequencyDial(app);

    // Leve di regolazione (contrasto, velocita', memoria, palette).
    if (ImGui::CollapsingHeader("Regolazioni waterfall",
                                ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SetNextItemWidth(220);
        ImGui::DragFloatRange2("Range dB", &app.rangeMinDb, &app.rangeMaxDb,
                               1.0f, -140.0f, 0.0f, "min %.0f", "max %.0f");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140);
        double rowsPerSec = 30.0 / double(app.wfSpeedDiv);
        char speedLbl[32];
        std::snprintf(speedLbl, sizeof(speedLbl), "%.0f righe/s", rowsPerSec);
        ImGui::SliderInt("Velocita'", &app.wfSpeedDiv, 1, 100, speedLbl,
                         ImGuiSliderFlags_Logarithmic);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        static const char* kRowNames[] = {"256", "512", "1024", "2048"};
        static const int kRowVals[] = {256, 512, 1024, 2048};
        int rowIdx = 0;
        for (int i = 0; i < 4; i++)
            if (kRowVals[i] == app.wfRows) rowIdx = i;
        if (ImGui::Combo("Memoria", &rowIdx, kRowNames, 4))
            resizeWaterfall(app, kRowVals[rowIdx]);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        static const char* kPalNames[] = {"Classica", "Grigi", "Fuoco"};
        ImGui::Combo("Palette", &app.wfPalette, kPalNames, 3);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90);
        static const char* kFftNames[] = {"4096", "8192", "16384", "32768",
                                          "65536"};
        static const int kFftVals[] = {4096, 8192, 16384, 32768, 65536};
        int fftIdx = 2;
        for (int i = 0; i < 5; i++)
            if (kFftVals[i] == app.fftSize) fftIdx = i;
        if (ImGui::Combo("FFT", &fftIdx, kFftNames, 5))
            app.fftSize = kFftVals[fftIdx]; // la cattura si adegua da sola
        ImGui::Spacing();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bandH = 20.0f;
    // Altezza dello spettro regolabile con il divisore trascinabile.
    float specH = std::clamp(avail.y * app.wfSplit, 90.0f, avail.y - 90.0f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = avail.x;

    const double centerHz = app.freqMHz * 1e6;
    const double f0 = centerHz - app.sampleRate / 2.0;
    const double f1 = centerHz + app.sampleRate / 2.0;

    // Finestra di vista (zoom): sottoinsieme dello span pieno.
    app.viewSpanFrac = std::clamp(app.viewSpanFrac, 0.005, 1.0);
    app.viewCenterFrac = std::clamp(app.viewCenterFrac, app.viewSpanFrac / 2,
                                    1.0 - app.viewSpanFrac / 2);
    const double span = (f1 - f0) * app.viewSpanFrac;
    const double v0 = f0 + app.viewCenterFrac * (f1 - f0) - span / 2.0;
    const double v1 = v0 + span;
    auto xOf = [&](double f) {
        return p0.x + float((f - v0) / (v1 - v0)) * w;
    };
    auto freqAt = [&](float x) {
        return v0 + double((x - p0.x) / w) * (v1 - v0);
    };

    // --- Fascia delle bande (cosa si sta ascoltando) ---
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + bandH),
                      ImGui::GetColorU32(ImVec4(0, 0, 0, 0.35f)), 4.0f);
    for (const auto& b : sdrjo::bandsInRange(v0, v1)) {
        float x0 = std::max(p0.x, xOf(b.lowHz));
        float x1 = std::min(p0.x + w, xOf(b.highHz));
        dl->AddRectFilled(ImVec2(x0, p0.y + 2), ImVec2(x1, p0.y + bandH - 2),
                          bandColor(b.category, 0.28f));
        dl->AddRectFilled(ImVec2(x0, p0.y + 2), ImVec2(x0 + 2, p0.y + bandH - 2),
                          bandColor(b.category, 0.9f));
        if (x1 - x0 > 70.0f)
            dl->AddText(ImVec2(x0 + 6, p0.y + 3),
                        bandColor(b.category, 1.0f), b.name);
    }

    // --- Area dello spettro (interattiva) ---
    ImVec2 s0(p0.x, p0.y + bandH);
    ImVec2 s1(p0.x + w, p0.y + bandH + specH);
    dl->AddRectFilled(s0, s1, ImGui::GetColorU32(ImVec4(0.03f, 0.04f, 0.06f, 1)));

    ImGui::InvisibleButton("##specarea", ImVec2(w, bandH + specH));
    bool hovered = ImGui::IsItemHovered();

    const float dbMin = app.rangeMinDb, dbMax = app.rangeMaxDb;
    auto yOf = [&](float db) {
        float t = (std::clamp(db, dbMin, dbMax) - dbMin) /
                  std::max(1.0f, dbMax - dbMin);
        return s1.y - t * specH;
    };

    // Griglia di frequenza con etichette in MHz.
    double step = niceStep((v1 - v0) / 8.0);
    ImU32 gridCol = ImGui::GetColorU32(ImVec4(0.3f, 0.42f, 0.53f, 0.18f));
    ImU32 textCol = ImGui::GetColorU32(ImVec4(0.55f, 0.65f, 0.75f, 0.9f));
    // Decimali adattivi: con lo zoom spinto servono piu' cifre.
    int freqDecimals = std::clamp(
        -int(std::floor(std::log10(step / 1e6))), 0, 6);
    for (double f = std::ceil(v0 / step) * step; f < v1; f += step) {
        float x = xOf(f);
        dl->AddLine(ImVec2(x, s0.y), ImVec2(x, s1.y), gridCol);
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "%.*f MHz", freqDecimals, f / 1e6);
        dl->AddText(ImVec2(x + 4, s1.y - 16), textCol, lbl);
    }
    // ~10 righe orizzontali con passo "bello" sul range dB corrente.
    {
        double dbStep = niceStep(double(dbMax - dbMin) / 10.0);
        for (double db = std::ceil(dbMin / dbStep) * dbStep; db < dbMax;
             db += dbStep) {
            dl->AddLine(ImVec2(s0.x, yOf(float(db))),
                        ImVec2(s1.x, yOf(float(db))), gridCol);
            // Niente etichetta dove si sovrapporrebbe alla riga dei MHz.
            if (yOf(float(db)) < s1.y - 26.0f) {
                char dbLbl[16];
                std::snprintf(dbLbl, sizeof(dbLbl), "%.0f dB", db);
                dl->AddText(ImVec2(s0.x + 4, yOf(float(db)) - 14), textCol,
                            dbLbl);
            }
        }
    }

    // Traccia dello spettro (max dei bin per colonna di pixel).
    {
        std::lock_guard<std::mutex> lk(app.spectrumMutex);
        const size_t n = app.spectrum.size();
        // Mappa i pixel sui bin della finestra di vista (zoom incluso).
        const double startBin = (v0 - f0) / (f1 - f0) * double(n);
        const double binsPerPx = (v1 - v0) / (f1 - f0) * double(n) / w;
        ImU32 line = ImGui::GetColorU32(ImVec4(0.22f, 0.71f, 1.0f, 1.0f));
        ImU32 fill = ImGui::GetColorU32(ImVec4(0.22f, 0.71f, 1.0f, 0.18f));
        float prevY = 0;
        for (int px = 0; px < int(w); px++) {
            float v = -160.0f;
            if (binsPerPx >= 1.0) {
                // Vista larga: massimo dei bin che cadono nel pixel.
                size_t b0 = size_t(std::max(0.0, startBin + px * binsPerPx));
                size_t b1 = std::max(
                    b0 + 1,
                    size_t(std::max(0.0, startBin + (px + 1) * binsPerPx)));
                for (size_t b = b0; b < b1 && b < n; b++)
                    v = std::max(v, app.spectrum[b]);
            } else {
                // Zoom spinto: interpolazione lineare tra bin adiacenti,
                // cosi' la traccia resta una linea liscia (niente gradini).
                double bp = startBin + (double(px) + 0.5) * binsPerPx - 0.5;
                bp = std::clamp(bp, 0.0, double(n - 1));
                size_t b = size_t(bp);
                double fr = bp - double(b);
                float a = app.spectrum[b];
                float c = app.spectrum[std::min(b + 1, n - 1)];
                v = float(a * (1.0 - fr) + c * fr);
            }
            float y = yOf(v);
            dl->AddLine(ImVec2(s0.x + px, y), ImVec2(s0.x + px, s1.y), fill);
            if (px > 0)
                dl->AddLine(ImVec2(s0.x + px - 1, prevY),
                            ImVec2(s0.x + px, y), line, 1.4f);
            prevY = y;
        }
    }

    // Marker del VFO di ascolto: banda evidenziata, bordi trascinabili.
    static bool draggingBw = false;
    bool nearEdge = false;
    float mx = ImGui::GetIO().MousePos.x;
    if (app.listenMode != AppState::ListenMode::Off) {
        double bw = app.listenBwHz;
        double vfoHz = centerHz + app.listenOffsetHz;
        float xc = xOf(vfoHz);
        float xb0 = xOf(vfoHz - bw / 2);
        float xb1 = xOf(vfoHz + bw / 2);
        dl->AddRectFilled(ImVec2(xb0, s0.y), ImVec2(xb1, s1.y),
                          ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.15f)));
        dl->AddLine(ImVec2(xc, s0.y), ImVec2(xc, s1.y),
                    ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.9f)), 1.5f);
        dl->AddLine(ImVec2(xb0, s0.y), ImVec2(xb0, s1.y),
                    ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.5f)));
        dl->AddLine(ImVec2(xb1, s0.y), ImVec2(xb1, s1.y),
                    ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.5f)));

        // Trascinamento dei bordi = cambia la larghezza del canale.
        nearEdge = hovered && (std::fabs(mx - xb0) < 6.0f ||
                               std::fabs(mx - xb1) < 6.0f);
        if (nearEdge || draggingBw)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (nearEdge && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            draggingBw = true;
        if (draggingBw) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                double maxBw =
                    (app.listenMode == AppState::ListenMode::WfmStereo)
                        ? 220000.0 : 46000.0;
                app.listenBwHz = std::clamp(
                    2.0 * std::fabs(freqAt(mx) - vfoHz), 500.0, maxBw);
            } else {
                draggingBw = false;
                rebuildChanFilter(app); // applica la nuova larghezza
            }
        }
    }

    // Interazione: click = sintonizza (snap), trascina = sintonia continua,
    // rotellina = passi di snap (oltre il bordo scorre da solo),
    // Ctrl+rotellina = zoom, doppio click = centra l'hardware qui.
    static bool draggingTune = false;
    static bool dragMoved = false;
    static float dragStartX = 0.0f;
    static double dragStartFreq = 0.0;

    if (hovered && !draggingBw) {
        double f = freqAt(mx);
        dl->AddLine(ImVec2(mx, s0.y), ImVec2(mx, s1.y),
                    ImGui::GetColorU32(ImVec4(1, 1, 1, 0.25f)));
        if (!draggingTune)
            ImGui::SetTooltip(
                "%.4f MHz\nclick: sintonizza (snap %.4g kHz)\n"
                "trascina: sintonia continua  rotellina: passi di snap\n"
                "Ctrl+rotellina: zoom  doppio click: centra qui",
                f / 1e6, app.snapHz / 1e3);

        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyCtrl) {
                // Zoom mantenendo ferma la frequenza sotto il cursore.
                double frac = double((mx - p0.x) / w);
                app.viewSpanFrac =
                    std::clamp(app.viewSpanFrac * std::pow(0.8, double(wheel)),
                               0.005, 1.0);
                double newSpan = (f1 - f0) * app.viewSpanFrac;
                double newV0 = f - frac * newSpan;
                app.viewCenterFrac =
                    (newV0 + newSpan / 2.0 - f0) / (f1 - f0);
            } else {
                // Oltre il 45%% dello span l'hardware si risintonizza da
                // solo: continuando con la rotellina si "scorre" la banda.
                double cur = centerHz + app.listenOffsetHz;
                double next = std::round((cur + double(wheel) * app.snapHz) /
                                         app.snapHz) * app.snapHz;
                applyTunedFrequency(app, next);
            }
        }

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            draggingTune = false;
            app.freqMHz = f / 1e6;
            if (app.source) app.source->setCenterFrequency(f);
            app.listenOffsetHz = 0.0;
            if (app.listenVfo) app.listenVfo->setOffset(0.0);
            centerViewOnTuned(app);
        } else if (!nearEdge && !draggingTune &&
                   ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            draggingTune = true;
            dragMoved = false;
            dragStartX = mx;
            dragStartFreq = centerHz + app.listenOffsetHz;
        }
    }

    // Trascinamento attivo: continua anche se il cursore esce dall'area.
    if (draggingTune) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float dx = mx - dragStartX;
            if (std::fabs(dx) > 4.0f) dragMoved = true;
            if (dragMoved) {
                // Trascini lo spettro: mouse a destra = frequenze piu' basse.
                double hzPerPx = (v1 - v0) / double(w);
                applyTunedFrequency(app, dragStartFreq - double(dx) * hzPerPx);
            }
        } else {
            if (!dragMoved && hovered) {
                // Click secco: sintonizza con lo snap.
                double snapped =
                    std::round(freqAt(mx) / app.snapHz) * app.snapHz;
                applyTunedFrequency(app, snapped);
            }
            draggingTune = false;
        }
    }

    // Divisore trascinabile: regola l'altezza spettro/waterfall.
    ImGui::InvisibleButton("##split", ImVec2(w, 7.0f));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive() && avail.y > 1.0f)
        app.wfSplit = std::clamp(
            app.wfSplit + ImGui::GetIO().MouseDelta.y / avail.y, 0.12f, 0.85f);
    {
        ImVec2 sp0 = ImGui::GetItemRectMin();
        ImVec2 sp1 = ImGui::GetItemRectMax();
        float cy = (sp0.y + sp1.y) * 0.5f;
        dl->AddLine(ImVec2(sp0.x, cy), ImVec2(sp1.x, cy),
                    ImGui::GetColorU32(ImVec4(0.3f, 0.42f, 0.53f,
                                              ImGui::IsItemHovered() ? 0.8f
                                                                     : 0.3f)),
                    2.0f);
    }

    // Waterfall nel resto dello spazio (ritagliato sulla vista/zoom).
    if (app.waterfallTex) {
        float u0 = float((v0 - f0) / (f1 - f0));
        float u1 = float((v1 - f0) / (f1 - f0));
        ImGui::Image((ImTextureID)(intptr_t)app.waterfallTex,
                     ImVec2(ImGui::GetContentRegionAvail().x,
                            ImGui::GetContentRegionAvail().y),
                     ImVec2(u0, 0.0f), ImVec2(u1, 1.0f));
    }
    ImGui::End();
}

void drawReceiverSection(AppState& app)
{
    drawSMeter(app);

    int mode = int(app.listenMode);
    if (ImGui::Combo("Demodulatore", &mode, kListenModeNames, 6)) {
        app.listenMode = AppState::ListenMode(mode);
        rebuildListener(app);
    }
    if (app.listenMode != AppState::ListenMode::Off) {
        ImGui::Text("VFO %.4f MHz  banda %.1f kHz",
                    (app.freqMHz * 1e6 + app.listenOffsetHz) / 1e6,
                    app.listenBwHz / 1e3);
        if (app.wfmDemod) {
            ImGui::SameLine();
            ImGui::TextDisabled(app.wfmDemod->stereoLocked() ? "STEREO"
                                                             : "mono");
        }
    }
    ImGui::SliderFloat("Volume", &app.volume, 0.0f, 1.0f, "%.2f");

    // Passo di sintonia per click e rotellina.
    static const double kSnaps[] = {1000, 5000, 9000, 10000,
                                    12500, 25000, 50000, 100000};
    static const char* kSnapNames[] = {"1 kHz",    "5 kHz",  "9 kHz (OM)",
                                       "10 kHz",   "12.5 kHz", "25 kHz",
                                       "50 kHz",   "100 kHz"};
    int snapIdx = 4;
    for (int i = 0; i < 8; i++)
        if (std::fabs(kSnaps[i] - app.snapHz) < 1) snapIdx = i;
    if (ImGui::Combo("Snap", &snapIdx, kSnapNames, 8))
        app.snapHz = kSnaps[snapIdx];

    // Zoom (equivalente a Ctrl+rotellina sullo spettro).
    float zoom = float(1.0 / app.viewSpanFrac);
    if (ImGui::SliderFloat("Zoom", &zoom, 1.0f, 100.0f, "%.0fx",
                           ImGuiSliderFlags_Logarithmic))
        app.viewSpanFrac = 1.0 / double(zoom);
    ImGui::SameLine();
    if (ImGui::SmallButton("1x")) {
        app.viewSpanFrac = 1.0;
        app.viewCenterFrac = 0.5;
    }

    ImGui::SeparatorText("Squelch");
    ImGui::Checkbox("Attivo##sq", &app.squelchOn);
    ImGui::SameLine();
    ImGui::TextDisabled(app.squelchOn ? (app.squelchOpen ? "APERTO" : "chiuso")
                                      : "");
    ImGui::SliderFloat("Soglia (dB)", &app.squelchDb, -100.0f, 0.0f, "%.0f");
    float lvl = std::clamp((app.chanLevelDb + 120.0f) / 120.0f, 0.0f, 1.0f);
    char lvlTxt[32];
    std::snprintf(lvlTxt, sizeof(lvlTxt), "%.0f dB", double(app.chanLevelDb));
    ImGui::ProgressBar(lvl, ImVec2(-1, 0), lvlTxt);

    ImGui::SeparatorText("Filtri audio");
    bool changed = false;
    changed |= ImGui::SliderFloat("Passa-alto (Hz)", &app.audioHighPassHz,
                                  0.0f, 500.0f, app.audioHighPassHz < 1
                                                    ? "spento" : "%.0f");
    changed |= ImGui::SliderFloat("Passa-basso (Hz)", &app.audioLowPassHz,
                                  0.0f, 12000.0f, app.audioLowPassHz < 1
                                                      ? "spento" : "%.0f");
    if (changed) {
        app.filterL.configure(48000.0, double(app.audioHighPassHz),
                              double(app.audioLowPassHz));
        app.filterR.configure(48000.0, double(app.audioHighPassHz),
                              double(app.audioLowPassHz));
    }
}

// Frequency manager: memorie salvate su file, riordinate per frequenza.
void drawAudioSection(AppState& app); // definita piu' avanti

// Sidebar unica: tutte le sezioni di controllo in una colonna, apribili
// e richiudibili come menu a tendina.
void drawSidebar(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sideW = std::clamp(vp->WorkSize.x * 0.24f, 300.0f, 420.0f);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sideW, vp->WorkSize.y - 20),
                             ImGuiCond_Always);
    ImGui::Begin("Controlli", nullptr, ImGuiWindowFlags_NoMove);

    if (ImGui::CollapsingHeader("Dispositivo", ImGuiTreeNodeFlags_DefaultOpen))
        drawDeviceSection(app);
    if (ImGui::CollapsingHeader("Ricevitore", ImGuiTreeNodeFlags_DefaultOpen))
        drawReceiverSection(app);
    if (ImGui::CollapsingHeader("Audio"))
        drawAudioSection(app);

    ImGui::End();
}

void drawFrequenciesPanel(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sideW = std::clamp(vp->WorkSize.x * 0.24f, 300.0f, 420.0f);
    const float rowY = 10 + std::max(300.0f, (vp->WorkSize.y - 30) * 0.62f) + 10;
    const float rowH = vp->WorkSize.y - rowY - 10;
    const float rowW = vp->WorkSize.x - sideW - 30;
    ImGui::SetNextWindowPos(ImVec2(sideW + 20, rowY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rowW * 0.38f, rowH), ImGuiCond_Always);
    ImGui::Begin("Frequenze", nullptr, ImGuiWindowFlags_NoMove);

    static char nameBuf[64] = "";
    ImGui::SetNextItemWidth(180);
    ImGui::InputTextWithHint("##nome", "nome memoria", nameBuf,
                             sizeof(nameBuf));
    ImGui::SameLine();
    if (ImGui::Button("Salva corrente")) {
        sdrjo::FavoriteFrequency ff;
        ff.freqHz = app.freqMHz * 1e6 + app.listenOffsetHz;
        ff.mode = kListenModeNames[int(app.listenMode)];
        ff.bandwidthHz = app.listenBwHz;
        if (nameBuf[0]) {
            ff.name = nameBuf;
        } else {
            char def[48];
            std::snprintf(def, sizeof(def), "%.4f MHz", ff.freqHz / 1e6);
            ff.name = def;
        }
        app.freqStore.add(std::move(ff));
        app.freqStore.save(app.freqStorePath);
        nameBuf[0] = '\0';
    }

    if (ImGui::BeginTable("freqs", 4,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
        ImGui::TableSetupColumn("Nome", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("MHz", ImGuiTableColumnFlags_WidthFixed, 90);
        ImGui::TableSetupColumn("Modo", ImGuiTableColumnFlags_WidthFixed, 60);
        ImGui::TableSetupColumn("##az", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableHeadersRow();

        int removeIdx = -1;
        auto& items = app.freqStore.items();
        for (int i = 0; i < int(items.size()); i++) {
            const auto& ff = items[size_t(i)];
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ff.name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.4f", ff.freqHz / 1e6);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(ff.mode.c_str());
            ImGui::TableNextColumn();
            ImGui::PushID(i);
            if (ImGui::SmallButton("Vai")) {
                for (int m = 0; m < 6; m++)
                    if (ff.mode == kListenModeNames[m])
                        app.listenMode = AppState::ListenMode(m);
                rebuildListener(app);
                if (ff.bandwidthHz > 0) {
                    app.listenBwHz = ff.bandwidthHz;
                    rebuildChanFilter(app);
                }
                applyTunedFrequency(app, ff.freqHz);
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) removeIdx = i;
            ImGui::PopID();
        }
        if (removeIdx >= 0) {
            app.freqStore.remove(size_t(removeIdx));
            app.freqStore.save(app.freqStorePath);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void drawAudioSection(AppState& app)
{

    ImGui::TextDisabled("backend: %s", app.audio.backendName().c_str());

    // Scheda di uscita.
    std::string current = (app.audioDeviceIndex < 0 ||
                           app.audioDeviceIndex >= int(app.audioDevices.size()))
                              ? "Predefinita di sistema"
                              : app.audioDevices[size_t(app.audioDeviceIndex)];
    if (ImGui::BeginCombo("Scheda", current.c_str())) {
        if (ImGui::Selectable("Predefinita di sistema",
                              app.audioDeviceIndex < 0)) {
            app.audioDeviceIndex = -1;
            app.audio.stop(); // riparte con la nuova scheda al prossimo audio
        }
        for (int i = 0; i < int(app.audioDevices.size()); i++) {
            if (ImGui::Selectable(app.audioDevices[size_t(i)].c_str(),
                                  app.audioDeviceIndex == i)) {
                app.audioDeviceIndex = i;
                app.audio.stop();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Aggiorna"))
        app.audioDevices = sdrjo::AudioOutput::listDevices();

    // Frequenza di campionamento dell'uscita.
    static const int kAudioRates[] = {44100, 48000, 96000};
    static const char* kAudioRateNames[] = {"44100 Hz", "48000 Hz", "96000 Hz"};
    int aIdx = (app.audioRateHz == 44100) ? 0 : (app.audioRateHz == 96000 ? 2 : 1);
    if (ImGui::Combo("Freq. audio", &aIdx, kAudioRateNames, 3)) {
        app.audioRateHz = kAudioRates[aIdx];
        app.audio.stop();
    }

    // ---- Accesso remoto al Cockpit (LAN, con password) ----
    ImGui::SeparatorText("Accesso remoto");
    static bool lanEnabled = false;
    static char lanPass[64] = "";
    ImGui::Checkbox("Esponi il Cockpit in LAN", &lanEnabled);
    ImGui::InputText("Password", lanPass, sizeof(lanPass),
                     ImGuiInputTextFlags_Password);
    if (lanEnabled && lanPass[0] == '\0') {
        ImGui::TextColored(ImVec4(1.0f, 0.71f, 0.33f, 1.0f),
                           "Serve una password per esporre in LAN.");
    }
    if (ImGui::Button("Applica##remoto")) {
        if (lanEnabled && lanPass[0] == '\0') {
            app.log("Cockpit", "password obbligatoria per la LAN: non applicato");
        } else {
            app.cockpit.stop();
            app.cockpit.setPassword(lanPass);
            bool ok = app.cockpit.start(sdrjo::CockpitServer::kDefaultPort,
                                        lanEnabled);
            app.log("Cockpit",
                    !ok ? std::string("errore nel riavvio del server")
                        : (lanEnabled
                               ? "esposto in LAN sulla porta 8750 (utente sdrjo)"
                               : "raggiungibile solo da questo PC"));
        }
    }
    ImGui::TextDisabled("utente: sdrjo - da internet usa una VPN (README)");
}

void drawModulesPanel(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sideW = std::clamp(vp->WorkSize.x * 0.24f, 300.0f, 420.0f);
    const float rowY = 10 + std::max(300.0f, (vp->WorkSize.y - 30) * 0.62f) + 10;
    const float rowH = vp->WorkSize.y - rowY - 10;
    const float rowW = vp->WorkSize.x - sideW - 30;
    ImGui::SetNextWindowPos(ImVec2(sideW + 20 + rowW * 0.38f + 10, rowY),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(rowW * 0.30f - 10, rowH),
                             ImGuiCond_Always);
    ImGui::Begin("Moduli", nullptr, ImGuiWindowFlags_NoMove);
    if (app.modules.empty()) {
        ImGui::TextWrapped("Nessun modulo caricato. Copia i moduli (*.dll) "
                           "nella cartella 'modules' accanto all'eseguibile.");
    }
    for (auto& lm : app.modules) {
        auto info = lm.module()->info();
        if (ImGui::CollapsingHeader(info.name.c_str(),
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextDisabled("%s", info.description.c_str());
            uint16_t port = lm.module()->webPort();
            if (port) ImGui::Text("Interfaccia web: http://localhost:%u", port);
            if (info.preferredFreqHz > 0) {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "Sintonizza (%.3f MHz)##%s",
                              info.preferredFreqHz / 1e6, info.name.c_str());
                // L'hardware e' uno solo: il modulo riceve davvero il suo
                // segnale solo quando la chiavetta e' sulla sua frequenza.
                if (ImGui::SmallButton(lbl)) {
                    app.freqMHz = info.preferredFreqHz / 1e6;
                    if (app.source)
                        app.source->setCenterFrequency(info.preferredFreqHz);
                    app.listenOffsetHz = 0.0;
                    if (app.listenVfo) app.listenVfo->setOffset(0.0);
                    centerViewOnTuned(app);
                }
            }
            lm.module()->drawUi();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("Cockpit completo: http://localhost:%u",
                        app.cockpit.port());
    ImGui::End();
}

void drawLogPanel(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sideW = std::clamp(vp->WorkSize.x * 0.24f, 300.0f, 420.0f);
    const float rowY = 10 + std::max(300.0f, (vp->WorkSize.y - 30) * 0.62f) + 10;
    const float rowH = vp->WorkSize.y - rowY - 10;
    const float rowW = vp->WorkSize.x - sideW - 30;
    const float logX = sideW + 20 + rowW * 0.68f + 10;
    ImGui::SetNextWindowPos(ImVec2(logX, rowY), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(vp->WorkSize.x - logX - 10, rowH), ImGuiCond_Always);
    ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoMove);
    std::lock_guard<std::mutex> lk(app.logMutex);
    for (auto& line : app.logLines)
        ImGui::TextUnformatted(line.c_str());
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
    ImGui::End();
}

} // namespace

int main(int argc, char** argv)
{
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    GLFWwindow* window =
        glfwCreateWindow(1280, 800, "SdrJo", nullptr, nullptr);
    if (!window) return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    sdrjo::gui::applyTheme();

    // Font moderni dalla cartella fonts/ accanto all'eseguibile.
    {
        ImGuiIO& io = ImGui::GetIO();
        namespace fs = std::filesystem;
        fs::path fontDir =
            fs::path(sdrjo::ModuleLoader::defaultModulesDir()).parent_path() /
            "fonts";
        fs::path roboto = fontDir / "Roboto-Medium.ttf";
        fs::path cousine = fontDir / "Cousine-Regular.ttf";
        if (fs::exists(roboto))
            gFontUi = io.Fonts->AddFontFromFileTTF(roboto.string().c_str(),
                                                   17.0f);
        if (fs::exists(cousine)) {
            gFontMonoBig = io.Fonts->AddFontFromFileTTF(
                cousine.string().c_str(), 34.0f);
            gFontMonoSmall = io.Fonts->AddFontFromFileTTF(
                cousine.string().c_str(), 13.0f);
        }
        if (gFontUi) io.FontDefault = gFontUi;
    }

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 130");

    AppState app;

    // Carica i moduli dalla cartella "modules" accanto all'eseguibile.
    std::vector<std::string> loadErrors;
    app.modules = sdrjo::ModuleLoader::loadDirectory(
        sdrjo::ModuleLoader::defaultModulesDir(), &loadErrors);
    for (auto& e : loadErrors) app.log("Loader", e);
    for (auto& lm : app.modules) lm.module()->start(app);
    rebuildChannels(app);

    // Frequency manager: memorie accanto all'eseguibile.
    app.freqStorePath =
        (std::filesystem::path(sdrjo::ModuleLoader::defaultModulesDir())
             .parent_path() /
         "frequenze.csv")
            .string();
    app.freqStore.load(app.freqStorePath);
    app.configPath =
        (std::filesystem::path(sdrjo::ModuleLoader::defaultModulesDir())
             .parent_path() /
         "sdrjo.cfg")
            .string();
    loadConfig(app);
    applyStationToModules(app);

    // Cockpit web: la plancia di SdrJo, anche per tablet/telefono in LAN.
    app.cockpit.setStatusProvider([&app] {
        std::vector<sdrjo::CockpitServer::ModuleStatus> out;
        for (auto& lm : app.modules) {
            auto info = lm.module()->info();
            out.push_back({info.name, info.description,
                           lm.module()->statusJson(),
                           lm.module()->webPort()});
        }
        return out;
    });
    app.cockpit.setSpectrumProvider([&app] {
        std::lock_guard<std::mutex> lk(app.spectrumMutex);
        if (!app.source) return std::vector<float>{};
        // Decimazione (max) a 2048 bin: il JSON resta leggero anche
        // con la FFT a 65536 punti.
        const auto& sp = app.spectrum;
        const size_t n = sp.size();
        if (n <= 2048) return sp;
        std::vector<float> out(2048);
        for (size_t x = 0; x < 2048; x++) {
            size_t b0 = x * n / 2048;
            size_t b1 = std::max(b0 + 1, (x + 1) * n / 2048);
            float v = -160.0f;
            for (size_t b = b0; b < b1; b++) v = std::max(v, sp[b]);
            out[x] = v;
        }
        return out;
    });
    app.cockpit.setTuneHandler([&app](double freqHz) {
        std::lock_guard<std::mutex> lk(app.remoteMutex);
        app.pendingTuneHz = freqHz;
        return true;
    });
    app.cockpit.setModeHandler([&app](const std::string& mode) {
        std::lock_guard<std::mutex> lk(app.remoteMutex);
        app.pendingMode = mode;
        return true;
    });
    if (app.cockpit.start())
        app.log("Cockpit", "http://localhost:" +
                               std::to_string(app.cockpit.port()));

    // Avvio con replay da riga di comando: sdrjo <file.bin> [rate_MSps] [MHz]
    if (argc >= 2) {
        double rate = (argc >= 3) ? std::atof(argv[2]) * 1e6 : app.sampleRate;
        if (argc >= 4) app.freqMHz = std::atof(argv[3]);
        app.sampleRate = rate;
        rebuildChannels(app);
        auto src = std::make_unique<sdrjo::FileSource>(argv[1], rate);
        src->setCenterFrequency(app.freqMHz * 1e6);
        src->start([&app](const sdrjo::cfloat* s, size_t n) {
            app.iqRing.write(s, n);
        });
        app.source = std::move(src);
        app.log("Replay", std::string(argv[1]));
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (app.channelsDirty) {
            rebuildChannels(app);
            app.channelsDirty = false;
        }

        // Applica i comandi arrivati dal Cockpit web.
        {
            double tuneHz = -1.0;
            std::string mode;
            {
                std::lock_guard<std::mutex> lk(app.remoteMutex);
                tuneHz = app.pendingTuneHz;
                app.pendingTuneHz = -1.0;
                mode.swap(app.pendingMode);
            }
            if (!mode.empty()) {
                for (int m = 0; m < 6; m++)
                    if (mode == kListenModeNames[m])
                        app.listenMode = AppState::ListenMode(m);
                rebuildListener(app);
            }
            if (tuneHz > 0) applyTunedFrequency(app, tuneHz);
        }

        updateSpectrum(app);
        uploadWaterfallTexture(app);
        app.cockpit.setDeviceInfo(
            app.source ? app.source->name() : "nessuna sorgente",
            app.freqMHz * 1e6, app.sampleRate);
        app.cockpit.setVfoInfo(app.freqMHz * 1e6 + app.listenOffsetHz,
                               kListenModeNames[int(app.listenMode)]);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawSidebar(app);
        drawSpectrumPanel(app);
        drawFrequenciesPanel(app);
        drawModulesPanel(app);
        drawLogPanel(app);

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    app.cockpit.stop();
    for (auto& lm : app.modules) lm.module()->stop();
    if (app.source) app.source->stop();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
