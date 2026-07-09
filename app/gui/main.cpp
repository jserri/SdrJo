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

    // Ascolto diretto WFM stereo (indipendente dai moduli).
    bool listenWfm = false;
    std::unique_ptr<sdrjo::dsp::Vfo> listenVfo;
    std::unique_ptr<sdrjo::dsp::WfmStereoDemodulator> wfmDemod;
    std::vector<float> audioL, audioR, audioInterleaved;

    // ---- IModuleHost ----
    void log(const std::string& mod, const std::string& text) override
    {
        std::lock_guard<std::mutex> lk(logMutex);
        logLines.push_back("[" + mod + "] " + text);
        if (logLines.size() > 500)
            logLines.erase(logLines.begin(), logLines.begin() + 100);
    }

    void playAudio(const float* samples, size_t n, double rateHz) override
    {
        if (!audio.isActive()) audio.start(rateHz, 2);
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

        // Ascolto diretto WFM stereo.
        if (app.listenWfm && app.listenVfo && app.wfmDemod) {
            static std::vector<sdrjo::cfloat> lchan;
            lchan.clear();
            app.listenVfo->process(chunk.data(), kFftSize, lchan);
            app.audioL.clear();
            app.audioR.clear();
            app.wfmDemod->process(lchan.data(), lchan.size(), app.audioL,
                                  app.audioR);
            if (!app.audio.isActive()) app.audio.start(48000.0, 2);
            app.audioInterleaved.resize(app.audioL.size() * 2);
            for (size_t i = 0; i < app.audioL.size(); i++) {
                app.audioInterleaved[2 * i] = app.audioL[i];
                app.audioInterleaved[2 * i + 1] = app.audioR[i];
            }
            app.audio.write(app.audioInterleaved.data(),
                            app.audioInterleaved.size());
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
    ImGui::SetNextWindowSize(ImVec2(330, 430), ImGuiCond_FirstUseEver);
    ImGui::Begin("Dispositivo");

    if (!app.source) {
        if (!sdrjo::RtlSdrSource::available()) {
            // librtlsdr caricata a runtime: se manca, spiega cosa fare.
            ImGui::TextWrapped("%s", sdrjo::RtlSdrSource::libraryHint().c_str());
        } else {
            auto devices = sdrjo::RtlSdrSource::enumerate();
            ImGui::Text("RTL-SDR trovate: %zu", devices.size());
            if (devices.empty()) {
                ImGui::TextWrapped("Nessuna chiavetta rilevata: controlla il "
                                   "cavo USB e il driver WinUSB (Zadig).");
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
                    app.source = std::move(src);
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
        if (ImGui::SliderFloat("Guadagno (dB)", &app.gainDb, -1.0f, 49.6f,
                               app.gainDb < 0 ? "AGC" : "%.1f")) {
            app.source->setGain(app.gainDb);
        }
        if (ImGui::Button("Ferma")) {
            app.source->stop();
            app.source.reset();
        }

        ImGui::SeparatorText("Ascolto");
        if (ImGui::Checkbox("Radio FM stereo (centro banda)", &app.listenWfm)) {
            if (app.listenWfm) {
                app.listenVfo = std::make_unique<sdrjo::dsp::Vfo>(
                    app.sampleRate, 240000.0, 0.0);
                app.wfmDemod =
                    std::make_unique<sdrjo::dsp::WfmStereoDemodulator>();
            } else {
                app.listenVfo.reset();
                app.wfmDemod.reset();
            }
        }
        if (app.listenWfm && app.wfmDemod) {
            ImGui::SameLine();
            ImGui::TextDisabled(app.wfmDemod->stereoLocked() ? "STEREO"
                                                             : "mono");
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
    ImGui::End();
}

void drawSpectrumPanel(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(350, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - 360, 520),
                             ImGuiCond_FirstUseEver);
    ImGui::Begin("Spettro");
    ImGui::PlotLines("##spettro", app.spectrum.data(), int(kFftSize), 0,
                     nullptr, -110.0f, 0.0f,
                     ImVec2(ImGui::GetContentRegionAvail().x, 160));
    if (app.waterfallTex) {
        ImGui::Image((ImTextureID)(intptr_t)app.waterfallTex,
                     ImVec2(ImGui::GetContentRegionAvail().x,
                            ImGui::GetContentRegionAvail().y));
    }
    ImGui::End();
}

void drawModulesPanel(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(10, 450), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(330, vp->WorkSize.y - 460),
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

int main(int, char**)
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
