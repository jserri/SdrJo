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
#include <sdrjo/util/iq_recorder.hpp>
#include <sdrjo/web/cockpit_server.hpp>
#include <sdrjo/source/sample_source.hpp>
#include <sdrjo/source/file_source.hpp>
#include <sdrjo/source/rtl_sdr_source.hpp>
#include <sdrjo/util/ring_buffer.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr size_t kFftSize = 4096;
constexpr int kWaterfallRows = 256;

struct AppState : public sdrjo::IModuleHost {
    std::unique_ptr<sdrjo::ISampleSource> source;
    sdrjo::RingBuffer<sdrjo::cfloat> iqRing{1 << 20};

    double freqMHz = 100.0;
    double sampleRate = 2.4e6;
    float gainDb = -1.0f; // <0 = AGC

    std::vector<float> spectrum = std::vector<float>(kFftSize, -120.0f);
    std::vector<float> waterfall =
        std::vector<float>(kFftSize * kWaterfallRows, -120.0f);
    int waterfallHead = 0;
    GLuint waterfallTex = 0;

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
}

void updateSpectrum(AppState& app)
{
    static std::vector<sdrjo::cfloat> chunk(kFftSize);
    while (app.iqRing.available() >= kFftSize) {
        app.iqRing.read(chunk.data(), kFftSize);
        app.dcBlocker.processInPlace(chunk.data(), kFftSize);
        if (app.recorder.isRecording())
            app.recorder.write(chunk.data(), kFftSize);
        {
            std::lock_guard<std::mutex> lk(app.spectrumMutex);
            sdrjo::dsp::powerSpectrumDb(chunk.data(), kFftSize,
                                        app.spectrum.data());
        }

        // Riga nel waterfall circolare.
        std::memcpy(&app.waterfall[size_t(app.waterfallHead) * kFftSize],
                    app.spectrum.data(), kFftSize * sizeof(float));
        app.waterfallHead = (app.waterfallHead + 1) % kWaterfallRows;

        // Distribuisci a ogni modulo il SUO canale (VFO dedicato).
        for (size_t m = 0; m < app.modules.size(); m++) {
            auto& ch = app.channels[m];
            if (ch.vfo) {
                ch.buf.clear();
                ch.vfo->process(chunk.data(), kFftSize, ch.buf);
                if (!ch.buf.empty())
                    app.modules[m].module()->processIq(ch.buf.data(),
                                                       ch.buf.size());
            } else {
                app.modules[m].module()->processIq(chunk.data(), kFftSize);
            }
        }

        // Catena di ascolto.
        if (app.listenMode != AppState::ListenMode::Off && app.listenVfo) {
            static std::vector<sdrjo::cfloat> lchan;
            lchan.clear();
            app.listenVfo->process(chunk.data(), kFftSize, lchan);
            if (lchan.empty()) continue;

            app.ensureAudio();
            if (app.wfmDemod) {
                app.audioL.clear();
                app.audioR.clear();
                app.wfmDemod->process(lchan.data(), lchan.size(), app.audioL,
                                      app.audioR);
                app.audioInterleaved.resize(app.audioL.size() * 2);
                for (size_t i = 0; i < app.audioL.size(); i++) {
                    app.audioInterleaved[2 * i] = app.volume * app.audioL[i];
                    app.audioInterleaved[2 * i + 1] =
                        app.volume * app.audioR[i];
                }
                app.audio.write(app.audioInterleaved.data(),
                                app.audioInterleaved.size());
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
                for (auto& v : app.audioMono) v *= app.volume;
                app.audio.writeMono(app.audioMono.data(),
                                    app.audioMono.size());
            }
        }
    }
}

void uploadWaterfallTexture(AppState& app)
{
    static std::vector<uint32_t> pixels(kFftSize * kWaterfallRows);
    auto colorize = [](float db) -> uint32_t {
        float t = std::clamp((db + 100.0f) / 70.0f, 0.0f, 1.0f);
        uint8_t r = uint8_t(255.0f * std::clamp(t * 2.5f - 1.2f, 0.0f, 1.0f));
        uint8_t g = uint8_t(255.0f * std::clamp(t * 2.0f - 0.5f, 0.0f, 1.0f));
        uint8_t b = uint8_t(255.0f * std::clamp(t * 3.0f, 0.0f, 1.0f) *
                            (1.0f - 0.5f * t));
        return 0xFF000000u | (uint32_t(b) << 16) | (uint32_t(g) << 8) | r;
    };
    for (int row = 0; row < kWaterfallRows; row++) {
        int src = (app.waterfallHead + row) % kWaterfallRows;
        for (size_t x = 0; x < kFftSize; x++)
            pixels[size_t(row) * kFftSize + x] =
                colorize(app.waterfall[size_t(src) * kFftSize + x]);
    }
    if (!app.waterfallTex) {
        glGenTextures(1, &app.waterfallTex);
        glBindTexture(GL_TEXTURE_2D, app.waterfallTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    glBindTexture(GL_TEXTURE_2D, app.waterfallTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kFftSize, kWaterfallRows, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
}

void drawDevicePanel(AppState& app)
{
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 590), ImGuiCond_FirstUseEver);
    ImGui::Begin("Dispositivo");

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

        ImGui::SeparatorText("Ascolto");
        static const char* kModes[] = {"Spento", "WFM stereo", "NFM",
                                       "AM", "USB", "LSB"};
        int mode = int(app.listenMode);
        if (ImGui::Combo("Demodulatore", &mode, kModes, 6)) {
            app.listenMode = AppState::ListenMode(mode);
            rebuildListener(app);
        }
        if (app.listenMode != AppState::ListenMode::Off) {
            ImGui::Text("VFO: %+.1f kHz dal centro (click sullo spettro)",
                        app.listenOffsetHz / 1e3);
            if (app.wfmDemod) {
                ImGui::SameLine();
                ImGui::TextDisabled(app.wfmDemod->stereoLocked() ? "STEREO"
                                                                 : "mono");
            }
        }
        ImGui::SliderFloat("Volume", &app.volume, 0.0f, 1.0f, "%.2f");

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
    ImGui::End();
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
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(350, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - 360, 520),
                             ImGuiCond_FirstUseEver);
    ImGui::Begin("Spettro");

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bandH = 20.0f;
    float specH = std::max(120.0f, avail.y * 0.42f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    const float w = avail.x;

    const double centerHz = app.freqMHz * 1e6;
    const double f0 = centerHz - app.sampleRate / 2.0;
    const double f1 = centerHz + app.sampleRate / 2.0;
    auto xOf = [&](double f) {
        return p0.x + float((f - f0) / (f1 - f0)) * w;
    };
    auto freqAt = [&](float x) {
        return f0 + double((x - p0.x) / w) * (f1 - f0);
    };

    // --- Fascia delle bande (cosa si sta ascoltando) ---
    dl->AddRectFilled(p0, ImVec2(p0.x + w, p0.y + bandH),
                      ImGui::GetColorU32(ImVec4(0, 0, 0, 0.35f)), 4.0f);
    for (const auto& b : sdrjo::bandsInRange(f0, f1)) {
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

    const float dbMin = -110.0f, dbMax = -10.0f;
    auto yOf = [&](float db) {
        float t = (std::clamp(db, dbMin, dbMax) - dbMin) / (dbMax - dbMin);
        return s1.y - t * specH;
    };

    // Griglia di frequenza con etichette in MHz.
    double step = niceStep((f1 - f0) / 8.0);
    ImU32 gridCol = ImGui::GetColorU32(ImVec4(0.3f, 0.42f, 0.53f, 0.18f));
    ImU32 textCol = ImGui::GetColorU32(ImVec4(0.55f, 0.65f, 0.75f, 0.9f));
    for (double f = std::ceil(f0 / step) * step; f < f1; f += step) {
        float x = xOf(f);
        dl->AddLine(ImVec2(x, s0.y), ImVec2(x, s1.y), gridCol);
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "%.4g MHz", f / 1e6);
        dl->AddText(ImVec2(x + 4, s1.y - 16), textCol, lbl);
    }
    for (float db = dbMin + 20; db < dbMax; db += 20)
        dl->AddLine(ImVec2(s0.x, yOf(db)), ImVec2(s1.x, yOf(db)), gridCol);

    // Traccia dello spettro (max dei bin per colonna di pixel).
    {
        std::lock_guard<std::mutex> lk(app.spectrumMutex);
        const size_t n = app.spectrum.size();
        ImU32 line = ImGui::GetColorU32(ImVec4(0.22f, 0.71f, 1.0f, 1.0f));
        ImU32 fill = ImGui::GetColorU32(ImVec4(0.22f, 0.71f, 1.0f, 0.18f));
        float prevY = 0;
        for (int px = 0; px < int(w); px++) {
            size_t b0 = size_t(double(px) / w * n);
            size_t b1 = std::max(b0 + 1, size_t(double(px + 1) / w * n));
            float v = -160.0f;
            for (size_t b = b0; b < b1 && b < n; b++)
                v = std::max(v, app.spectrum[b]);
            float y = yOf(v);
            dl->AddLine(ImVec2(s0.x + px, y), ImVec2(s0.x + px, s1.y), fill);
            if (px > 0)
                dl->AddLine(ImVec2(s0.x + px - 1, prevY),
                            ImVec2(s0.x + px, y), line, 1.4f);
            prevY = y;
        }
    }

    // Marker del VFO di ascolto con la sua larghezza di banda.
    if (app.listenMode != AppState::ListenMode::Off) {
        double bw = listenBandwidthHz(app.listenMode);
        float xc = xOf(centerHz + app.listenOffsetHz);
        float xb0 = xOf(centerHz + app.listenOffsetHz - bw / 2);
        float xb1 = xOf(centerHz + app.listenOffsetHz + bw / 2);
        dl->AddRectFilled(ImVec2(xb0, s0.y), ImVec2(xb1, s1.y),
                          ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.15f)));
        dl->AddLine(ImVec2(xc, s0.y), ImVec2(xc, s1.y),
                    ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.9f)), 1.5f);
    }

    // Interazione: tooltip con la frequenza, click = VFO, doppio = centro.
    if (hovered) {
        float mx = ImGui::GetIO().MousePos.x;
        double f = freqAt(mx);
        dl->AddLine(ImVec2(mx, s0.y), ImVec2(mx, s1.y),
                    ImGui::GetColorU32(ImVec4(1, 1, 1, 0.25f)));
        ImGui::SetTooltip("%.4f MHz\nclick: sposta l'ascolto\n"
                          "doppio click: centra qui", f / 1e6);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            app.freqMHz = f / 1e6;
            if (app.source) app.source->setCenterFrequency(f);
            app.listenOffsetHz = 0.0;
            if (app.listenVfo) app.listenVfo->setOffset(0.0);
        } else if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            app.listenOffsetHz = f - centerHz;
            if (app.listenVfo) app.listenVfo->setOffset(app.listenOffsetHz);
        }
    }

    // Waterfall nel resto dello spazio.
    if (app.waterfallTex) {
        ImGui::Image((ImTextureID)(intptr_t)app.waterfallTex,
                     ImVec2(ImGui::GetContentRegionAvail().x,
                            ImGui::GetContentRegionAvail().y));
    }
    ImGui::End();
}

void drawAudioPanel(AppState& app)
{
    ImGui::SetNextWindowPos(ImVec2(10, 450), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, 150), ImGuiCond_FirstUseEver);
    ImGui::Begin("Audio");

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
    ImGui::End();
}

void drawModulesPanel(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(10, 610), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, vp->WorkSize.y - 620),
                             ImGuiCond_FirstUseEver);
    ImGui::Begin("Moduli");
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
    ImGui::SetNextWindowPos(ImVec2(350, 540), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - 360,
                                    vp->WorkSize.y - 550),
                             ImGuiCond_FirstUseEver);
    ImGui::Begin("Log");
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
        return app.source ? app.spectrum : std::vector<float>{};
    });
    app.cockpit.setTuneHandler([&app](double freqHz) {
        return app.requestTune(freqHz, app.sampleRate);
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
        updateSpectrum(app);
        uploadWaterfallTexture(app);
        app.cockpit.setDeviceInfo(
            app.source ? app.source->name() : "nessuna sorgente",
            app.freqMHz * 1e6, app.sampleRate);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawDevicePanel(app);
        drawSpectrumPanel(app);
        drawAudioPanel(app);
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
