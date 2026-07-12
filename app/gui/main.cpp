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
#include <sdrjo/dsp/rtty.hpp>
#include <sdrjo/dsp/psk31.hpp>
#include <sdrjo/dsp/tetra_activity.hpp>
#include <sdrjo/morse/cw_decoder.hpp>
#include <sdrjo/sat/orbit.hpp>
#include <sdrjo/sat/tle.hpp>
#include <sdrjo/web/ws_audio_server.hpp>
#include <sdrjo/util/frequency_store.hpp>
#include <sdrjo/util/geolocate.hpp>
#include <sdrjo/util/iq_recorder.hpp>
#include <sdrjo/util/wav_writer.hpp>
#include <sdrjo/web/cockpit_server.hpp>
#include <sdrjo/source/sample_source.hpp>
#include <sdrjo/source/file_source.hpp>
#include <sdrjo/source/rtl_sdr_source.hpp>
#include <sdrjo/util/ring_buffer.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <cstring>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr size_t kChunkSize = 4096;   // blocco DSP per moduli/ascolto
constexpr float kStatusBarH = 34.0f;  // barra di stato in fondo (font 17 + bordo)
// Colonne della texture waterfall: piu' alte = zoom piu' nitido (meno
// "sgranato"). 8192 raddoppia il dettaglio orizzontale con un costo di
// memoria contenuto (la RAM scala col numero di righe scelto in "Memoria").
constexpr int kWfWidth = 8192;
constexpr int kWaterfallRows = 256;

const char* kListenModeNames[] = {"Spento", "WFM stereo", "NFM",
                                  "AM", "USB", "LSB"};

// Guida concisa: mostra una riga di spiegazione quando il mouse resta
// sull'ultimo controllo disegnato (come i tooltip di SDR#).
void helpTip(const char* text)
{
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

// Etichetta su una riga propria + widget successivo a piena larghezza
// della colonna: evita i troncamenti delle label a destra nelle sidebar
// strette (usare un id "##..." nel widget che segue).
void fieldLabel(const char* text)
{
    ImGui::TextUnformatted(text);
    ImGui::SetNextItemWidth(-FLT_MIN);
}

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
    float wfContrast = 1.0f;         // gamma della palette (slider laterale)
    float uiScale = 1.0f;            // scala dei font dell'interfaccia
    int fftWindow = 0;               // 0 = Hann, 1 = Blackman-Harris
    int wfRows = kWaterfallRows;     // memoria del waterfall (righe)
    int wfSpeedDiv = 4;              // 1 riga ogni N FFT (velocita')
    int wfSpeedCounter = 0;
    int wfPalette = 0;               // 0 classica, 1 grigi, 2 fuoco
    float wfSplit = 0.42f;           // quota di altezza dello spettro

    std::vector<sdrjo::LoadedModule> modules;
    // Interruttore per modulo: quando spento il modulo NON riceve IQ (ne'
    // il suo VFO viene elaborato), cosi' i decoder inattivi non pesano su
    // CPU/anello IQ. Persistente tra le ricostruzioni dei canali.
    std::vector<char> moduleEnabled;
    std::vector<std::string> logLines;
    std::mutex logMutex;

    sdrjo::CockpitServer cockpit;
    sdrjo::WsAudioServer wsAudio; // streaming a bassa latenza (ADPCM)
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
    ListenMode listenMode = ListenMode::Am; // all'avvio la radio parte in AM
    double listenOffsetHz = 0.0;
    bool muted = false; // mute istantaneo (bottone accanto al volume)
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
    bool rtlAgc = false; // AGC digitale dell'RTL2832 (come in SDR#)
    bool biasTee = false;
    int ppmCorrection = 0;

    // Zoom dello spettro: frazioni dello span pieno dell'hardware.
    double viewSpanFrac = 1.0;
    double viewCenterFrac = 0.5;

    // Passo di sintonia (rotellina/click) e larghezza canale regolabile.
    double snapHz = 12500.0;
    double listenBwHz = 0.0;
    bool snapToPeak = false; // click di sintonia: aggancia al picco vicino

    // Squelch sul canale di ascolto.
    bool squelchOn = false;
    float squelchDb = -50.0f;
    float chanLevelDb = -120.0f;
    bool squelchOpen = true;

    // Filtri audio (passa-alto/passa-basso/notch).
    float audioHighPassHz = 0.0f;
    float audioLowPassHz = 0.0f;
    float notchHz = 0.0f;
    sdrjo::dsp::AudioFilterChain filterL, filterR;

    // Noise blanker sul canale IQ di ascolto.
    bool nbOn = false;
    float nbThreshold = 4.0f;
    sdrjo::dsp::NoiseBlanker noiseBlanker;

    // Tracce extra dello spettro (media EMA e max hold).
    bool specAvgOn = false;
    bool specMaxOn = false;
    std::vector<float> specAvg, specMax;

    // Presa sull'audio demodulato per lo spettro audio (2048 campioni).
    std::mutex audioTapMutex;
    std::vector<float> audioTap = std::vector<float>(2048, 0.0f);
    size_t audioTapPos = 0;

    // Scanner delle memorie salvate.
    bool scanOn = false;
    int scanDwellMs = 500;       // attesa su ogni memoria
    float scanResumeSec = 2.0f;  // riprende dopo N s di squelch chiuso
    bool scanRecord = false;
    int scanIndex = -1;
    bool scanPaused = false;
    std::chrono::steady_clock::time_point scanLastHop{}, scanSigLost{};
    sdrjo::WavWriter scanWav;

    // Decoder di testi (CW/RTTY) sull'audio del canale d'ascolto,
    // stile fldigi. Stato protetto da dspMutex.
    // Passa-banda stretto per il CW: risonatore complesso sul tono.
    struct ToneEnvelope {
        float cRe = 1, cIm = 0, re = 0, im = 0, alpha = 0.01f;
        void configure(double freqHz, double rate)
        {
            double w = 2.0 * 3.14159265358979323846 * freqHz / rate;
            cRe = float(std::cos(w));
            cIm = float(-std::sin(w));
            alpha = float(1.0 - std::exp(-2.0 * 3.14159265358979323846 *
                                         120.0 / rate));
            re = im = 0;
        }
        float step(float x)
        {
            float nre = re * cRe - im * cIm;
            float nim = re * cIm + im * cRe;
            re = nre + alpha * (x - nre);
            im = nim - alpha * nim;
            return std::sqrt(re * re + im * im);
        }
    };
    int textDecoderMode = 0; // 0 spento, 1 CW, 2 RTTY, 3 PSK31
    float cwToneHz = 700.0f;
    bool cwAutoSpeed = true;   // CW: velocita' auto o WPM fissa
    float cwWpm = 20.0f;       // WPM usati in modalita' manuale
    bool rttyReverse = false;
    float rttyMarkHz = 2125.0f; // tono mark; space = mark + shift
    int rttyBaudIdx = 0;        // 0=45.45 1=50 2=75
    int rttyShiftIdx = 0;       // 0=170 1=425 2=850 Hz
    float psk31ToneHz = 1000.0f; // tono audio del segnale BPSK31
    bool decoderAfc = false;      // centra da solo il tono CW/PSK sul picco
    float decoderSquelch = 0.15f; // soglia d'ampiezza: sotto = niente decodifica
    ToneEnvelope cwTone;
    std::unique_ptr<sdrjo::morse::CwDecoder> cwDecoder;
    std::unique_ptr<sdrjo::dsp::RttyDecoder> rttyDecoder;
    std::unique_ptr<sdrjo::dsp::Psk31Decoder> psk31Decoder;
    std::string decodedText;
    std::atomic<float> decoderLevel{0.0f}; // livello audio RMS visto dai decoder

    // Rivelatore di attivita' TETRA (solo presenza, niente decodifica).
    bool tetraDetectOn = false;
    std::unique_ptr<sdrjo::dsp::TetraActivityDetector> tetraDet;
    std::atomic<bool> tetraActive{false};
    std::atomic<float> tetraSnr{0.0f};
    std::atomic<float> tetraOcc{0.0f};
    std::atomic<float> tetraLevel{0.0f};

    // Satelliti: TLE, passaggi calcolati e inseguimento Doppler.
    std::vector<sdrjo::sat::Tle> tles;
    std::string tlePath;
    int satSel = 0;
    std::vector<sdrjo::sat::SatPass> satPasses;
    bool dopplerOn = false;
    double dopplerBaseMHz = 137.1;
    double dopplerCurrentHz = 0.0;
    std::chrono::steady_clock::time_point lastDopplerTune{};

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

    // Rilevamento posizione via IP: gira in un thread suo, l'esito viene
    // applicato dal thread GUI (0 fermo, 1 in corso, 2 esito pronto).
    std::atomic<int> geoState{0};
    sdrjo::GeoIpResult geoResult;
    std::mutex geoMutex;
    std::thread geoThread;

    // Comandi arrivati dal Cockpit web (thread HTTP): applicati nel loop
    // principale per non toccare lo stato da thread diversi.
    std::mutex remoteMutex;
    double pendingTuneHz = -1.0;
    double pendingRateHz = -1.0;
    std::string pendingMode;

    // Thread DSP: drena l'anello IQ e fa TUTTO il calcolo (moduli, ascolto,
    // FFT, righe waterfall). La GUI si limita a disegnare: niente freeze.
    std::thread dspThread;
    std::atomic<bool> dspRunning{false};
    // Ricorsivo: le rebuild* si richiamano tra loro dal thread GUI.
    std::recursive_mutex dspMutex;

    // Waterfall: aggiornamento incrementale della texture (solo le righe
    // nuove), con redraw completo solo quando cambiano palette/range.
    std::mutex wfMutex;
    std::vector<int> wfNewRows;
    bool wfFullRedraw = true;

    // Limitatore dei retune hardware (il set_center_freq via USB e'
    // bloccante: durante il drag ne basterebbero 60/s per inchiodare).
    double pendingHwTuneHz = -1.0;
    std::chrono::steady_clock::time_point lastHwTune{};

    // Al cambio di frequenza hardware: chiede al thread DSP di scartare il
    // backlog dell'anello IQ (la vecchia banda) cosi' lo spettro passa
    // SUBITO alla nuova frequenza invece di smaltire i campioni vecchi.
    std::atomic<bool> iqDrainReq{false};

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
    std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
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

// Token casuale per lo streaming WebSocket (nuovo a ogni avvio).
std::string randomToken()
{
    std::random_device rd;
    static const char* kHex = "0123456789abcdef";
    std::string t;
    for (int i = 0; i < 8; i++) {
        uint32_t v = rd();
        for (int k = 0; k < 4; k++) t += kHex[(v >> (k * 4)) & 15];
    }
    return t;
}

// Config minimale (chiave=valore) accanto all'eseguibile. Oltre alla
// posizione, ricorda l'ultima sessione radio (freq/modo/banda/volume/snap)
// cosi' riaprendo l'app ritrovi dov'eri.
void loadConfig(AppState& app)
{
    FILE* f = std::fopen(app.configPath.c_str(), "r");
    if (!f) return;
    char line[256];
    while (std::fgets(line, sizeof(line), f)) {
        double v = 0;
        int iv = 0;
        if (std::sscanf(line, "station_lat=%lf", &v) == 1) app.stationLat = v;
        else if (std::sscanf(line, "station_lon=%lf", &v) == 1)
            app.stationLon = v;
        else if (std::sscanf(line, "freq_mhz=%lf", &v) == 1) app.freqMHz = v;
        else if (std::sscanf(line, "mode=%d", &iv) == 1 && iv >= 0 && iv <= 5)
            app.listenMode = AppState::ListenMode(iv);
        else if (std::sscanf(line, "bandwidth_hz=%lf", &v) == 1)
            app.listenBwHz = v;
        else if (std::sscanf(line, "volume=%lf", &v) == 1)
            app.volume = float(v);
        else if (std::sscanf(line, "snap_hz=%lf", &v) == 1) app.snapHz = v;
        else if (std::sscanf(line, "ui_scale=%lf", &v) == 1)
            app.uiScale = std::clamp(float(v), 0.7f, 2.0f);
        else if (std::sscanf(line, "decoder_squelch=%lf", &v) == 1)
            app.decoderSquelch = std::clamp(float(v), 0.0f, 0.6f);
        else if (std::sscanf(line, "snap_to_peak=%d", &iv) == 1)
            app.snapToPeak = iv != 0;
    }
    std::fclose(f);
}

void saveConfig(AppState& app)
{
    FILE* f = std::fopen(app.configPath.c_str(), "w");
    if (!f) return;
    std::fprintf(f,
                 "station_lat=%.6f\nstation_lon=%.6f\n"
                 "freq_mhz=%.6f\nmode=%d\nbandwidth_hz=%.1f\n"
                 "volume=%.3f\nsnap_hz=%.1f\nui_scale=%.2f\n"
                 "decoder_squelch=%.3f\nsnap_to_peak=%d\n",
                 app.stationLat, app.stationLon, app.freqMHz,
                 int(app.listenMode), app.listenBwHz, double(app.volume),
                 app.snapHz, double(app.uiScale), double(app.decoderSquelch),
                 app.snapToPeak ? 1 : 0);
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
    std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
    app.chanFilter.reset();
    if (app.listenMode == AppState::ListenMode::Off ||
        app.listenMode == AppState::ListenMode::WfmStereo)
        return;
    // Minimo 100 Hz: per il CW su HF servono filtri strettissimi
    // (200-500 Hz), 1 kHz e' gia' larghissimo.
    double bw = std::clamp(app.listenBwHz, 100.0, 46000.0);
    if (app.ssbDemod) {
        app.ssbDemod = std::make_unique<sdrjo::dsp::SsbDemodulator>(
            48000.0, app.listenMode == AppState::ListenMode::Usb, bw);
    } else {
        // Filtri stretti = piu' tap per bordi ripidi (250 tap sotto 1 kHz).
        int taps = bw < 1000.0 ? 251 : (bw < 4000.0 ? 199 : 129);
        app.chanFilter = std::make_unique<sdrjo::dsp::FirFilter>(
            sdrjo::dsp::designLowPass(48000.0, bw / 2.0, taps));
    }
}

// (Ri)costruisce la catena di ascolto per modalita'/rate correnti.
void rebuildListener(AppState& app)
{
    std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
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

// Corpo del thread DSP: gira finche' l'app e' viva, drena l'anello IQ
// e fa tutto il lavoro pesante fuori dal thread di rendering.
void dspLoop(AppState& app)
{
    static std::vector<sdrjo::cfloat> chunk(kChunkSize);
    while (app.dspRunning.load()) {
        // Cambio di frequenza: butta via il backlog della vecchia banda
        // (fatto dal thread consumatore, cioe' questo) cosi' i prossimi
        // campioni elaborati sono gia' alla frequenza nuova.
        if (app.iqDrainReq.exchange(false)) {
            app.iqRing.clear();
            std::lock_guard<std::recursive_mutex> dspLk(app.dspMutex);
            app.captureFilled = 0;
            app.capturePos = 0;
            app.samplesSinceFft = 0;
        }
        if (app.iqRing.available() < kChunkSize) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        std::lock_guard<std::recursive_mutex> dspLk(app.dspMutex);
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

        // Distribuisci a ogni modulo il SUO canale (VFO dedicato). I
        // moduli spenti non vengono elaborati (ne' VFO ne' decoder): a
        // riposo l'app non spende CPU sui decodificatori che non usi.
        for (size_t m = 0; m < app.modules.size(); m++) {
            if (m < app.moduleEnabled.size() && !app.moduleEnabled[m])
                continue;
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

            // Rivelatore di attivita' TETRA: analizza il canale largo
            // (prima del filtro stretto), solo alla velocita' standard 48 kHz.
            if (app.tetraDetectOn && app.tetraDet &&
                app.listenMode != AppState::ListenMode::WfmStereo) {
                app.tetraDet->processIq(lchan.data(), lchan.size());
                app.tetraActive.store(app.tetraDet->active(),
                                      std::memory_order_relaxed);
                app.tetraSnr.store(app.tetraDet->snrDb(),
                                   std::memory_order_relaxed);
                app.tetraOcc.store(app.tetraDet->occupiedKHz(),
                                   std::memory_order_relaxed);
                app.tetraLevel.store(app.tetraDet->level(),
                                     std::memory_order_relaxed);
            }

            // Noise blanker: schiaccia i disturbi impulsivi sul canale.
            app.noiseBlanker.processInPlace(lchan.data(), lchan.size());

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

            // Presa per lo spettro audio (anello degli ultimi campioni).
            auto tapAudio = [&app](const float* s, size_t n) {
                std::lock_guard<std::mutex> tlk(app.audioTapMutex);
                for (size_t i = 0; i < n; i++) {
                    app.audioTap[app.audioTapPos] = s[i];
                    app.audioTapPos =
                        (app.audioTapPos + 1) % app.audioTap.size();
                }
            };

            // Decoder di testi (CW/RTTY/PSK31) sull'audio demodulato. Uno
            // squelch d'ampiezza blocca la decodifica quando non c'e' segnale
            // (evita fiumi di testo spazzatura sul solo rumore).
            auto feedTextDecoders = [&app](const float* s, size_t n) {
                if (app.textDecoderMode == 0 || n == 0) return;
                double sum = 0.0;
                for (size_t i = 0; i < n; i++) sum += double(s[i]) * s[i];
                float rms = float(std::sqrt(sum / double(n)));
                app.decoderLevel.store(rms, std::memory_order_relaxed);
                if (rms < app.decoderSquelch) return; // sotto soglia: fermo

                if (app.textDecoderMode == 1 && app.cwDecoder) {
                    static std::vector<float> env;
                    env.resize(n);
                    for (size_t i = 0; i < n; i++)
                        env[i] = app.cwTone.step(s[i]);
                    app.cwDecoder->processAudio(env.data(), n);
                } else if (app.textDecoderMode == 2 && app.rttyDecoder) {
                    app.rttyDecoder->processAudio(s, n);
                } else if (app.textDecoderMode == 3 && app.psk31Decoder) {
                    app.psk31Decoder->processAudio(s, n);
                }
            };

            app.ensureAudio();
            if (app.wfmDemod) {
                app.audioL.clear();
                app.audioR.clear();
                app.wfmDemod->process(lchan.data(), lchan.size(), app.audioL,
                                      app.audioR);
                app.filterL.process(app.audioL.data(), app.audioL.size());
                app.filterR.process(app.audioR.data(), app.audioR.size());
                tapAudio(app.audioL.data(), app.audioL.size());
                feedTextDecoders(app.audioL.data(), app.audioL.size());
                float g = (app.squelchOpen && !app.muted) ? app.volume : 0.0f;
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
                app.wsAudio.pushAudio(mix.data(), mix.size());
                if (app.scanWav.isOpen())
                    app.scanWav.write(mix.data(), mix.size());
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
                tapAudio(app.audioMono.data(), app.audioMono.size());
                feedTextDecoders(app.audioMono.data(),
                                 app.audioMono.size());
                float g = (app.squelchOpen && !app.muted) ? app.volume : 0.0f;
                for (auto& v : app.audioMono) v *= g;
                app.audio.writeMono(app.audioMono.data(),
                                    app.audioMono.size());
                app.cockpit.pushAudio(app.audioMono.data(),
                                      app.audioMono.size());
                app.wsAudio.pushAudio(app.audioMono.data(),
                                      app.audioMono.size());
                if (app.scanWav.isOpen())
                    app.scanWav.write(app.audioMono.data(),
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
            sdrjo::dsp::powerSpectrumDb(
                lin.data(), lin.size(), app.spectrum.data(),
                app.fftWindow == 1 ? sdrjo::dsp::FftWindow::BlackmanHarris
                                   : sdrjo::dsp::FftWindow::Hann);
        }

        // Riga del waterfall (max-decimata a kWfWidth colonne).
        if (++app.wfSpeedCounter >= app.wfSpeedDiv) {
            app.wfSpeedCounter = 0;
            std::lock_guard<std::mutex> wlk(app.wfMutex);
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
            app.wfNewRows.push_back(app.waterfallHead);
            if (app.wfNewRows.size() > size_t(app.wfRows))
                app.wfFullRedraw = true; // troppo indietro: riparti
            app.waterfallHead = (app.waterfallHead + 1) % app.wfRows;
        }
    }
    }
}

// Cambia il numero di righe di storia del waterfall.
void resizeWaterfall(AppState& app, int rows)
{
    std::lock_guard<std::mutex> lk(app.wfMutex);
    app.wfRows = rows;
    app.waterfall.assign(size_t(kWfWidth) * size_t(rows), -120.0f);
    app.waterfallHead = 0;
    app.wfNewRows.clear();
    app.wfFullRedraw = true;
}

void uploadWaterfallTexture(AppState& app)
{
    // Righe nuove dal thread DSP (di norma 0 o 1 per frame video).
    static std::vector<int> newRows;
    newRows.clear();
    bool full = false;
    {
        std::lock_guard<std::mutex> lk(app.wfMutex);
        newRows.swap(app.wfNewRows);
        full = app.wfFullRedraw;
        app.wfFullRedraw = false;
    }

    const float lo = app.rangeMinDb;
    const float span = std::max(1.0f, app.rangeMaxDb - app.rangeMinDb);
    const int palette = app.wfPalette;
    // Contrasto (levetta laterale): curva gamma sulla mappa dei colori.
    const float gamma = 1.0f / std::max(0.25f, app.wfContrast);
    auto colorize = [&](float db) -> uint32_t {
        float t = std::clamp((db - lo) / span, 0.0f, 1.0f);
        t = std::pow(t, gamma);
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

    static int allocatedRows = 0;
    if (!app.waterfallTex) {
        glGenTextures(1, &app.waterfallTex);
        glBindTexture(GL_TEXTURE_2D, app.waterfallTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        full = true;
    }
    glBindTexture(GL_TEXTURE_2D, app.waterfallTex);

    if (full || allocatedRows != app.wfRows) {
        // Redraw completo: solo su cambio palette/range/memoria.
        static std::vector<uint32_t> pixels;
        std::lock_guard<std::mutex> lk(app.wfMutex);
        pixels.resize(size_t(kWfWidth) * size_t(app.wfRows));
        for (size_t i = 0; i < pixels.size(); i++)
            pixels[i] = colorize(app.waterfall[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, kWfWidth, app.wfRows, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        allocatedRows = app.wfRows;
        return;
    }

    // Aggiornamento incrementale: solo le righe appena scritte.
    static std::vector<float> rowF(kWfWidth);
    static std::vector<uint32_t> rowPix(kWfWidth);
    for (int r : newRows) {
        if (r < 0 || r >= allocatedRows) continue;
        {
            std::lock_guard<std::mutex> lk(app.wfMutex);
            std::memcpy(rowF.data(), &app.waterfall[size_t(r) * kWfWidth],
                        kWfWidth * sizeof(float));
        }
        for (int x = 0; x < kWfWidth; x++) rowPix[size_t(x)] = colorize(rowF[size_t(x)]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, r, kWfWidth, 1, GL_RGBA,
                        GL_UNSIGNED_BYTE, rowPix.data());
    }
}

// Helper di sintonia definiti piu' avanti (usati anche dal pannello
// Dispositivo per il campo Frequenza).
void tuneAbsolute(AppState& app, double f);
void requestHwCenter(AppState& app, double newCenter, bool immediate);
void centerViewOnTuned(AppState& app);

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
        double freq = app.freqMHz * 1e6 + app.listenOffsetHz;
        double freqMHz = freq / 1e6;
        fieldLabel("Frequenza (MHz)");
        if (ImGui::InputDouble("##freqMHz", &freqMHz, 0.1, 1.0, "%.4f")) {
            tuneAbsolute(app, freqMHz * 1e6); // lo spettro segue subito
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Frequenza sintonizzata: la chiavetta si "
                              "porta qui e lo spettro la segue.");

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
        fieldLabel("Sample rate");
        if (ImGui::Combo("##srate", &rateIdx, kRateNames, 8)) {
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
                fieldLabel("Guadagno (dB)");
                if (ImGui::SliderFloat("##gain", &app.gainDb, 0.0f,
                                       49.6f, "%.1f"))
                    app.rtl->setGain(double(app.gainDb));
            }
            if (ImGui::Checkbox("AGC RTL (digitale)", &app.rtlAgc)) {
                if (!app.rtl->setRtlAgc(app.rtlAgc)) {
                    app.log("Dispositivo",
                            "AGC RTL non supportato da questa rtlsdr.dll");
                    app.rtlAgc = false;
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Amplificazione digitale dell'RTL2832: "
                                  "utile sui segnali deboli (il 'RTL AGC' "
                                  "di SDR#).");
            if (ImGui::Checkbox("Bias-T (alimenta LNA)", &app.biasTee)) {
                if (!app.rtl->setBiasTee(app.biasTee)) {
                    app.log("Dispositivo",
                            "bias-T non supportato da questa rtlsdr.dll");
                    app.biasTee = false;
                }
            }
            fieldLabel("Correzione PPM");
            if (ImGui::InputInt("##ppm", &app.ppmCorrection))
                app.rtl->setPpmCorrection(app.ppmCorrection);
            // Stima automatica: sintonizzati ESATTAMENTE su una portante
            // di frequenza nota (una FM forte va benissimo) e premi.
            if (ImGui::SmallButton("Stima PPM dal segnale sintonizzato")) {
                double centerHz = app.freqMHz * 1e6;
                double expectedHz = centerHz + app.listenOffsetHz;
                double err = 0.0;
                {
                    std::lock_guard<std::mutex> lk(app.spectrumMutex);
                    err = sdrjo::dsp::estimatePpm(
                        app.spectrum.data(), app.spectrum.size(),
                        app.sampleRate, centerHz, expectedHz, 20e3);
                }
                if (err == 0.0) {
                    app.log("PPM", "nessuna portante chiara vicino alla "
                                   "sintonia: mettiti su una stazione "
                                   "forte di frequenza nota e riprova");
                } else {
                    // err = scostamento del picco; la correzione va nel
                    // verso opposto rispetto a quella gia' impostata.
                    int suggested =
                        app.ppmCorrection - int(std::lround(err));
                    if (std::abs(suggested) > 200) {
                        app.log("PPM", "stima fuori scala: sintonia "
                                       "davvero sulla portante?");
                    } else {
                        app.ppmCorrection = suggested;
                        app.rtl->setPpmCorrection(app.ppmCorrection);
                        char msg[96];
                        std::snprintf(msg, sizeof(msg),
                                      "scostamento %+.1f ppm: correzione "
                                      "impostata a %d ppm",
                                      err, app.ppmCorrection);
                        app.log("PPM", msg);
                    }
                }
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip(
                    "Sintonizzati esattamente su una portante di frequenza "
                    "nota\n(es. una radio FM forte), poi premi: il picco "
                    "osservato\nviene confrontato con quello atteso.");
        }


        ImGui::SeparatorText("Registrazione IQ");
        if (!app.recorder.isRecording()) {
            if (ImGui::Button("Registra")) {
                char name[64];
                std::snprintf(name, sizeof(name), "sdrjo_%.4fMHz.bin",
                              app.freqMHz);
                std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
                app.recorder.start(name, app.freqMHz * 1e6, app.sampleRate);
            }
        } else {
            if (ImGui::Button("Stop registrazione")) {
                std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
                app.recorder.stop();
            }
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
        fieldLabel("File IQ");
        ImGui::InputText("##replay", replayPath, sizeof(replayPath));
        if (ImGui::Button("Riproduci") && replayPath[0]) {
            auto src = std::make_unique<sdrjo::FileSource>(replayPath,
                                                           app.sampleRate);
            src->setLoop(true); // a fine file si ricomincia
            src->setCenterFrequency(app.freqMHz * 1e6);
            src->start([&app](const sdrjo::cfloat* s, size_t n) {
                app.iqRing.write(s, n);
            });
            app.source = std::move(src);
        }
    }

}

// Posizione dell'antenna: serve alla mappa ADS-B (marker + cerchi di
// portata + distanze) e ai passaggi satellite. Sta in un sottomenu a parte
// perche' la si imposta una volta e poi la si lascia chiusa.
void drawStationSection(AppState& app)
{
    bool posChanged = false;
    fieldLabel("Latitudine");
    posChanged |= ImGui::InputDouble("##lat", &app.stationLat, 0, 0, "%.5f");
    fieldLabel("Longitudine");
    posChanged |= ImGui::InputDouble("##lon", &app.stationLon, 0, 0, "%.5f");
    if (posChanged) {
        saveConfig(app);
        applyStationToModules(app);
    }

    // Rilevamento automatico via IP (precisione a livello di citta').
    int geoSt = app.geoState.load();
    if (geoSt == 1) {
        ImGui::TextDisabled("rilevamento in corso...");
    } else if (ImGui::Button("Rileva dalla rete")) {
        if (app.geoThread.joinable()) app.geoThread.join();
        app.geoState.store(1);
        app.geoThread = std::thread([&app] {
            auto r = sdrjo::geolocateByIp();
            {
                std::lock_guard<std::mutex> lk(app.geoMutex);
                app.geoResult = r;
            }
            app.geoState.store(2);
        });
    }
    if (geoSt == 2) {
        sdrjo::GeoIpResult r;
        {
            std::lock_guard<std::mutex> lk(app.geoMutex);
            r = app.geoResult;
        }
        app.geoState.store(0);
        if (r.ok) {
            app.stationLat = r.latDeg;
            app.stationLon = r.lonDeg;
            saveConfig(app);
            applyStationToModules(app);
            char msg[128];
            std::snprintf(msg, sizeof(msg), "rilevata via IP: %.4f, %.4f %s%s",
                          r.latDeg, r.lonDeg, r.city.empty() ? "" : "- ",
                          r.city.c_str());
            app.log("Posizione", msg);
        } else {
            app.log("Posizione", "rilevamento fallito: " + r.error);
        }
    }
    if (ImGui::IsItemHovered() && geoSt == 0)
        ImGui::SetTooltip("Stima la posizione dal tuo IP (precisione: "
                          "citta'). Puoi sempre rifinire lat/lon a mano.");

    if (app.stationLat == 0.0 && app.stationLon == 0.0)
        ImGui::TextDisabled("(imposta lat/lon per vederti sulla mappa ADS-B)");
}

// Offset anti-DC: quanto tenere il segnale ascoltato lontano dalla riga
// della DC al centro (che ogni RTL-SDR ha e che il DC blocker scava).
// Basta poco per uscire dallo spike centrale, restando vicini al centro.
double antiDcOffset(const AppState& app)
{
    if (app.listenMode == AppState::ListenMode::Off) return 0.0;
    // Offset PICCOLO: basta scostare il segnale dalla riga della DC (che il
    // DC blocker gia' scava sul flusso grezzo). Tenendolo piccolo il segnale
    // sintonizzato resta praticamente al CENTRO dello spettro, cosi' quando
    // cambi frequenza col frequenzimetro si vede subito il segnale ricentrarsi
    // (con l'offset enorme di prima il marker restava fisso a ~60% e sembrava
    // che non cambiasse nulla). Scala con la larghezza per liberare la DC
    // anche sui segnali larghi (WFM), ma senza esagerare.
    double maxOff = app.sampleRate * 0.45 - app.listenBwHz * 0.5;
    double want = std::max(12000.0, app.listenBwHz * 0.55);
    return std::clamp(want, 0.0, std::max(0.0, maxOff));
}

// Centra la vista zoomata sulla frequenza sintonizzata. Usata SOLO per
// azioni esplicite (lever dello zoom, "Sintonizza" di un modulo), non a
// ogni sintonia: quello dava il microlag/scatto lamentato.
void centerViewOnTuned(AppState& app)
{
    if (app.viewSpanFrac >= 0.999) return;
    double tuned = app.freqMHz * 1e6 + app.listenOffsetHz;
    double f0 = app.freqMHz * 1e6 - app.sampleRate / 2.0;
    app.viewCenterFrac = std::clamp((tuned - f0) / app.sampleRate,
                                    app.viewSpanFrac / 2.0,
                                    1.0 - app.viewSpanFrac / 2.0);
}

// Sposta la vista (zoom) SOLO se la frequenza sintonizzata sta per uscire
// dai bordi: nessun ricentraggio continuo (niente microlag mentre si
// sintonizza dentro la porzione visibile).
void keepTunedInView(AppState& app)
{
    if (app.viewSpanFrac >= 0.999) return; // vista piena: sempre visibile
    double f0 = app.freqMHz * 1e6 - app.sampleRate / 2.0;
    double frac = (app.freqMHz * 1e6 + app.listenOffsetHz - f0) /
                  app.sampleRate;
    double half = app.viewSpanFrac / 2.0;
    double margin = app.viewSpanFrac * 0.08;
    if (frac < app.viewCenterFrac - half + margin)
        app.viewCenterFrac = frac + half - margin;
    else if (frac > app.viewCenterFrac + half - margin)
        app.viewCenterFrac = frac - half + margin;
    app.viewCenterFrac = std::clamp(app.viewCenterFrac, half, 1.0 - half);
}

// Chiamata quando l'hardware cambia banda: azzera la cattura per lo
// spettro e chiede al thread DSP di scartare il backlog dell'anello IQ
// (campioni della vecchia frequenza). Il waterfall NON viene svuotato:
// scorre naturalmente mostrando la nuova banda dall'alto, come SDR#.
void resetSpectrumAfterRetune(AppState& app)
{
    app.captureFilled = 0;
    app.capturePos = 0;
    app.samplesSinceFft = 0;
    app.iqDrainReq.store(true);
}

// Diagnostica sintonia: registra cosa e' stato chiesto alla chiavetta e
// cosa la chiavetta rilegge davvero (rtlsdr_get_center_freq). Se "chiesto"
// e "legge" divergono, il comando di sintonia non va a segno.
void logHwTune(AppState& app, double requestedHz)
{
    char buf[200];
    if (app.rtl) {
        double actual = app.rtl->actualCenterFrequency();
        int rc = app.rtl->lastTuneResult();
        std::snprintf(buf, sizeof(buf),
                      "HW: chiesto %.4f MHz | rc=%d %s | chiavetta legge "
                      "%.4f MHz | VFO off %.1f kHz",
                      requestedHz / 1e6, rc, rc == 0 ? "OK" : "ERRORE",
                      actual / 1e6, app.listenOffsetHz / 1e3);
    } else {
        std::snprintf(buf, sizeof(buf),
                      "HW: chiesto %.4f MHz (replay/nessuna chiavetta) "
                      "| VFO off %.1f kHz",
                      requestedHz / 1e6, app.listenOffsetHz / 1e3);
    }
    app.log("Sintonia", buf);
}

// Richiede all'hardware una nuova frequenza centrale.
// immediate = true (dial, campo MHz, modulo, click) risintonizza SUBITO,
// senza limitatore: la sintonia deve rispondere all'istante.
// immediate = false (drag/rotellina continui) usa il limitatore ~20/s
// cosi' il flusso di comandi USB non inchioda la GUI.
void requestHwCenter(AppState& app, double newCenter, bool immediate = false)
{
    double oldCenter = app.freqMHz * 1e6;
    bool bigChange = std::fabs(newCenter - oldCenter) > 1.0;
    if (!app.source) {
        app.freqMHz = newCenter / 1e6; // niente hardware (replay/nessuna sorgente)
        return;
    }
    auto now = std::chrono::steady_clock::now();
    if (immediate || now - app.lastHwTune > std::chrono::milliseconds(50)) {
        app.lastHwTune = now;
        bool ok = app.source->setCenterFrequency(newCenter);
        // Il righello segue la frequenza REALMENTE riletta dalla chiavetta,
        // non quella chiesta: se la sintonia non va a segno il numero non
        // mente (prima il righello si spostava anche quando l'hardware no).
        double effective =
            app.rtl ? app.rtl->actualCenterFrequency() : newCenter;
        // Ritenta una volta se la chiavetta non ha accettato o non si e'
        // mossa (a volte il primo comando USB va perso durante lo streaming).
        if (app.rtl && (!ok || std::fabs(effective - newCenter) > 1000.0)) {
            app.source->setCenterFrequency(newCenter);
            effective = app.rtl->actualCenterFrequency();
        }
        app.freqMHz = effective / 1e6;
        app.pendingHwTuneHz = -1.0;
        if (bigChange) {
            resetSpectrumAfterRetune(app);
            logHwTune(app, newCenter); // diagnostica: chiesto vs riletto
        }
    } else {
        app.pendingHwTuneHz = newCenter;
    }
}

// Sintonia da grafico/righello/waterfall: se la frequenza cade dentro lo
// span dell'hardware la si raggiunge col VFO (offset), senza muovere la
// chiavetta ne' la vista; altrimenti si risintonizza l'hardware.
// forceHwRetune = true (doppio click) ricentra comunque l'hardware.
void applyTunedFrequency(AppState& app, double f, bool forceHwRetune = false)
{
    std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
    f = std::clamp(f, 0.0, 1.999e9);
    double center = app.freqMHz * 1e6;
    if (!forceHwRetune && app.source &&
        std::fabs(f - center) < app.sampleRate * 0.45) {
        // Dentro lo span: sposto solo il VFO, l'hardware e la vista non si
        // muovono (nessun salto: e' la sintonia fine che il click deve dare).
        app.listenOffsetHz = f - center;
        if (app.listenVfo) app.listenVfo->setOffset(app.listenOffsetHz);
        keepTunedInView(app);
    } else {
        // Fuori span (o doppio click): ricentro l'hardware e RICENTRO la
        // vista sulla frequenza sintonizzata, cosi' il righello non "salta".
        double offset = antiDcOffset(app);
        app.listenOffsetHz = offset;
        requestHwCenter(app, f - offset);
        if (app.listenVfo) app.listenVfo->setOffset(app.listenOffsetHz);
        centerViewOnTuned(app);
    }
}

// Sintonia "vai a" (frequenzimetro in alto e campo MHz): come su SDR#,
// porta SEMPRE la frequenza scelta al CENTRO dello spettro risintonizzando
// la chiavetta (con un piccolo offset anti-DC). Cosi' quando digiti o giri
// una frequenza vedi SUBITO quella banda centrata, e non "a volte si muove
// e a volte no". La sintonia fine che sposta solo il marker sui segnali
// fermi e' invece il CLICK sullo spettro (applyTunedFrequency).
void tuneAbsolute(AppState& app, double f)
{
    std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
    f = std::clamp(f, 0.0, 1.999e9);
    double offset = antiDcOffset(app);
    app.listenOffsetHz = offset;
    requestHwCenter(app, f - offset, /*immediate=*/true);
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
                tuneAbsolute(app, double(f + delta)); // ricentra l'hardware
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

// --- Calibrazione S-meter --------------------------------------------------
// Il livello del canale e' in dBFS (0 = fondo scala). Convenzione radio:
// 6 dB per punto S, S9 fissato a un livello forte; sopra S9 si contano i dB.
// Lancetta E numero usano QUESTA scala, cosi' concordano (prima la lancetta
// mappava -120..0 dBFS su S1..+60 e finiva sempre altissima).
constexpr float kS9Db = -20.0f;         // dBFS ~ S9
constexpr float kSPerUnitDb = 6.0f;     // 6 dB per punto S
// Posizione 0..1 sull'arco (0=S1, 0.55=S9, 1.0=+60 dB) da un valore dBFS.
float sMeterPos(float dbfs)
{
    if (dbfs <= kS9Db) {
        float s1Db = kS9Db - 8.0f * kSPerUnitDb; // S1 = S9 - 48 dB
        float u = (dbfs - s1Db) / (kS9Db - s1Db); // 0..1 su S1..S9
        return std::clamp(u, 0.0f, 1.0f) * 0.55f;
    }
    return std::clamp(0.55f + 0.45f * (dbfs - kS9Db) / 60.0f, 0.55f, 1.0f);
}
// Etichetta tipo "S5" o "S9+18".
void sMeterLabel(float dbfs, char* out, size_t n)
{
    if (dbfs >= kS9Db)
        std::snprintf(out, n, "S9+%.0f", double(dbfs - kS9Db));
    else {
        float s = 9.0f - (kS9Db - dbfs) / kSPerUnitDb;
        std::snprintf(out, n, "S%.0f", double(std::clamp(s, 1.0f, 9.0f)));
    }
}

// S-meter analogico "d'epoca": quadrante crema, zona rossa oltre S9,
// lancetta smorzata pilotata dal livello del canale di ascolto.
void drawSMeter(AppState& app)
{
    static float needle = 0.0f;
    float target = sMeterPos(app.chanLevelDb);
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
    char sLbl[16];
    sMeterLabel(app.chanLevelDb, sLbl, sizeof(sLbl));
    char dbLbl[32];
    std::snprintf(dbLbl, sizeof(dbLbl), "%s  %.0f dBFS", sLbl,
                  double(app.chanLevelDb));
    ImVec2 tsz = ImGui::CalcTextSize(dbLbl);
    dl->AddText(small, smallSize, ImVec2(p.x + w - tsz.x - 12, p.y + h - 24),
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

void setDecoderTone(AppState& app, double hz); // def. piu' sotto

// Cerca il picco piu' forte dello spettro RF entro +/- halfWinHz da
// targetHz e ne restituisce la frequenza (o targetHz se non trova niente
// di netto). Serve al "snap al picco" del click di sintonia.
double nearestSpectrumPeak(AppState& app, double f0, double f1,
                           double targetHz, double halfWinHz)
{
    std::lock_guard<std::mutex> lk(app.spectrumMutex);
    const size_t n = app.spectrum.size();
    if (n == 0 || f1 <= f0) return targetHz;
    auto binOf = [&](double f) {
        double b = (f - f0) / (f1 - f0) * double(n);
        return size_t(std::clamp(b, 0.0, double(n - 1)));
    };
    size_t b0 = binOf(targetHz - halfWinHz);
    size_t b1 = binOf(targetHz + halfWinHz);
    if (b1 <= b0) return targetHz;
    size_t bBest = b0;
    for (size_t b = b0; b <= b1 && b < n; b++)
        if (app.spectrum[b] > app.spectrum[bBest]) bBest = b;
    // Richiede un minimo di stacco dal fondo, altrimenti lascia il target.
    double sum = 0;
    for (size_t b = b0; b <= b1 && b < n; b++) sum += app.spectrum[b];
    float avg = float(sum / double(b1 - b0 + 1));
    if (app.spectrum[bBest] < avg + 3.0f) return targetHz; // niente picco netto
    return f0 + (double(bBest) + 0.5) / double(n) * (f1 - f0);
}

// AFC dei decoder: sposta lentamente il tono CW/PSK sul picco piu' vicino
// dell'audio (scan Goertzel stretto), come l'aggancio automatico di fldigi.
void stepDecoderAfc(AppState& app)
{
    int mode = app.textDecoderMode;
    if (!app.decoderAfc || (mode != 1 && mode != 3)) return;

    const size_t n = app.audioTap.size();
    static std::vector<float> buf;
    buf.resize(n);
    {
        std::lock_guard<std::mutex> lk(app.audioTapMutex);
        for (size_t i = 0; i < n; i++)
            buf[i] = app.audioTap[(app.audioTapPos + i) % n];
    }
    double rate = 48000.0;
    double tone = mode == 1 ? double(app.cwToneHz) : double(app.psk31ToneHz);
    // Scan di potenza (Goertzel) in +/-120 Hz attorno al tono, passo 6 Hz.
    double bestF = tone, bestP = -1.0;
    for (double f = tone - 120.0; f <= tone + 120.0; f += 6.0) {
        double w0 = 2.0 * 3.14159265358979323846 * f / rate;
        double coeff = 2.0 * std::cos(w0);
        double s0 = 0, s1 = 0, s2 = 0;
        for (size_t i = 0; i < n; i++) {
            s0 = buf[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        double p = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        if (p > bestP) { bestP = p; bestF = f; }
    }
    // Nudge lento (0.25) per non "saltare" sul rumore.
    double nudged = tone + 0.25 * (bestF - tone);
    if (std::fabs(nudged - tone) >= 1.0) setDecoderTone(app, nudged);
}

// Spettro interattivo: guide di banda, scala in MHz, click per spostare
// il VFO di ascolto, doppio click per risintonizzare l'hardware.
void drawSpectrumPanel(AppState& app)
{
    // Layout proporzionale: spettro e waterfall hanno la priorita' e
    // occupano tutta la parte destra (il resto vive nella sidebar).
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sideW = std::clamp(vp->WorkSize.x * 0.24f, 300.0f, 420.0f);
    const float stripW = 88.0f; // colonna leve/S-meter (finestra a parte)
    ImGui::SetNextWindowPos(ImVec2(sideW + 20, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(
        ImVec2(vp->WorkSize.x - sideW - 40 - stripW, vp->WorkSize.y - 20 - kStatusBarH),
        ImGuiCond_Always);
    ImGui::Begin("Spettro", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

    // Frequenza sintonizzata: cifre cliccabili e scrollabili.
    drawFrequencyDial(app);

    // Leve di regolazione (chiuse di default: spazio al grafico). Il
    // Range/Offset dB vivono sulle leve a destra dello spettro; qui
    // restano velocita', memoria, palette, FFT, finestra e tracce.
    bool regOpen = ImGui::CollapsingHeader("Regolazioni");
    // Guida rapida all'interazione accanto al titolo (senza coprire il
    // waterfall come faceva il vecchio tooltip grande).
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    helpTip("Sullo spettro/righello/waterfall:\n"
            "click = sintonizza (snap)   trascina = sintonia continua\n"
            "rotellina = cambia frequenza a passi di snap\n"
            "Ctrl+rotellina o leva Zoom = ingrandisci\n"
            "doppio click = ricentra la chiavetta qui");
    if (regOpen) {
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
        if (ImGui::Combo("Palette", &app.wfPalette, kPalNames, 3))
            app.wfFullRedraw = true;
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
        // Blackman-Harris: lobi laterali bassissimi, i segnali forti non
        // coprono quelli deboli accanto (picchi un po' piu' larghi).
        ImGui::SetNextItemWidth(150);
        static const char* kWinNames[] = {"Hann", "Blackman-Harris"};
        ImGui::Combo("Finestra", &app.fftWindow, kWinNames, 2);
        ImGui::SameLine();
        // Tracce extra: media (liscia il rumore) e max hold (segnali
        // intermittenti: rimane il picco piu' alto visto).
        ImGui::Checkbox("Media", &app.specAvgOn);
        ImGui::SameLine();
        ImGui::Checkbox("Max hold", &app.specMaxOn);
        ImGui::SameLine();
        if (ImGui::SmallButton("Azzera max")) app.specMax.clear();
        ImGui::Spacing();
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    const float bandH = 20.0f;
    const float scaleH = 22.0f; // righello delle frequenze (cliccabile)
    const float splitH = 7.0f;
    // Altezza dello spettro regolabile con il divisore trascinabile.
    const float usableH = std::max(180.0f, avail.y - bandH - scaleH - splitH);
    float specH = std::clamp(usableH * app.wfSplit, 90.0f, usableH - 90.0f);
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    // Le levette Zoom/Contrasto/Range/Offset e l'S-meter verticale sono
    // in una finestra dedicata a destra (drawRightStrip): qui lo spettro
    // usa tutta la larghezza, senza sovrapposizioni col waterfall.
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

    // --- Bookmark: le frequenze salvate che cadono nella vista ---
    // Tacca viola in cima allo spettro + nome; per sintonizzarle usa
    // "Vai" nella sezione Frequenze (qui sono un riferimento visivo).
    {
        const ImU32 bmCol = ImGui::GetColorU32(ImVec4(0.78f, 0.57f, 0.92f, 0.9f));
        float lastLabelX = -1e9f;
        for (const auto& ff : app.freqStore.items()) {
            if (ff.freqHz < v0 || ff.freqHz > v1) continue;
            float x = xOf(ff.freqHz);
            dl->AddLine(ImVec2(x, s0.y), ImVec2(x, s0.y + 12.0f), bmCol, 1.5f);
            dl->AddTriangleFilled(ImVec2(x - 3, s0.y), ImVec2(x + 3, s0.y),
                                  ImVec2(x, s0.y + 5), bmCol);
            // Etichette non sovrapposte: salta se troppo vicine.
            if (x - lastLabelX > 46.0f) {
                dl->AddText(ImVec2(x + 3, s0.y + 1), bmCol, ff.name.c_str());
                lastLabelX = x;
            }
        }
    }

    ImGui::InvisibleButton("##specarea", ImVec2(w, bandH + specH));
    bool hovSpec = ImGui::IsItemHovered();
    bool activeSpec = ImGui::IsItemActive();

    const float dbMin = app.rangeMinDb, dbMax = app.rangeMaxDb;
    auto yOf = [&](float db) {
        float t = (std::clamp(db, dbMin, dbMax) - dbMin) /
                  std::max(1.0f, dbMax - dbMin);
        return s1.y - t * specH;
    };

    // Griglia di frequenza (le etichette MHz vivono nel righello sotto).
    double step = niceStep((v1 - v0) / 8.0);
    ImU32 gridCol = ImGui::GetColorU32(ImVec4(0.3f, 0.42f, 0.53f, 0.18f));
    ImU32 textCol = ImGui::GetColorU32(ImVec4(0.55f, 0.65f, 0.75f, 0.9f));
    // Decimali adattivi: con lo zoom spinto servono piu' cifre.
    int freqDecimals = std::clamp(
        -int(std::floor(std::log10(step / 1e6))), 0, 6);
    for (double f = std::ceil(v0 / step) * step; f < v1; f += step)
        dl->AddLine(ImVec2(xOf(f), s0.y), ImVec2(xOf(f), s1.y), gridCol);
    // ~10 righe orizzontali con passo "bello" sul range dB corrente.
    {
        double dbStep = niceStep(double(dbMax - dbMin) / 10.0);
        for (double db = std::ceil(dbMin / dbStep) * dbStep; db < dbMax;
             db += dbStep) {
            float gy = yOf(float(db));
            dl->AddLine(ImVec2(s0.x, gy), ImVec2(s1.x, gy), gridCol);
            // Niente etichetta troppo in alto o troppo in basso: cosi'
            // non si accavalla al bordo banda ne' al righello dei MHz.
            if (gy > s0.y + 8.0f && gy < s1.y - 16.0f) {
                char dbLbl[16];
                std::snprintf(dbLbl, sizeof(dbLbl), "%.0f", db);
                dl->AddText(ImVec2(s0.x + 4, gy - 14), textCol, dbLbl);
            }
        }
    }

    // Traccia dello spettro (max dei bin per colonna di pixel), con
    // media EMA e max-hold opzionali.
    {
        std::lock_guard<std::mutex> lk(app.spectrumMutex);
        const size_t n = app.spectrum.size();
        const float* raw = app.spectrum.data();

        if (app.specAvgOn) {
            if (app.specAvg.size() != n)
                app.specAvg.assign(raw, raw + n);
            else
                for (size_t i = 0; i < n; i++)
                    app.specAvg[i] += 0.25f * (raw[i] - app.specAvg[i]);
        }
        if (app.specMaxOn) {
            if (app.specMax.size() != n)
                app.specMax.assign(raw, raw + n);
            else
                for (size_t i = 0; i < n; i++)
                    app.specMax[i] = std::max(app.specMax[i], raw[i]);
        }

        // Mappa i pixel sui bin della finestra di vista (zoom incluso).
        const double startBin = (v0 - f0) / (f1 - f0) * double(n);
        const double binsPerPx = (v1 - v0) / (f1 - f0) * double(n) / w;
        auto plotTrace = [&](const float* src, ImU32 line, ImU32 fill,
                             bool withFill, float thickness) {
            float prevY = 0;
            for (int px = 0; px < int(w); px++) {
                float v = -160.0f;
                if (binsPerPx >= 1.0) {
                    // Vista larga: massimo dei bin che cadono nel pixel.
                    size_t b0 =
                        size_t(std::max(0.0, startBin + px * binsPerPx));
                    size_t b1 = std::max(
                        b0 + 1, size_t(std::max(
                                    0.0, startBin + (px + 1) * binsPerPx)));
                    for (size_t b = b0; b < b1 && b < n; b++)
                        v = std::max(v, src[b]);
                } else {
                    // Zoom spinto: interpolazione lineare tra bin, cosi'
                    // la traccia resta una linea liscia (niente gradini).
                    double bp = startBin + (double(px) + 0.5) * binsPerPx -
                                0.5;
                    bp = std::clamp(bp, 0.0, double(n - 1));
                    size_t b = size_t(bp);
                    double fr = bp - double(b);
                    float a = src[b];
                    float c = src[std::min(b + 1, n - 1)];
                    v = float(a * (1.0 - fr) + c * fr);
                }
                float y = yOf(v);
                if (withFill)
                    dl->AddLine(ImVec2(s0.x + px, y),
                                ImVec2(s0.x + px, s1.y), fill);
                if (px > 0)
                    dl->AddLine(ImVec2(s0.x + px - 1, prevY),
                                ImVec2(s0.x + px, y), line, thickness);
                prevY = y;
            }
        };

        if (app.specMaxOn && app.specMax.size() == n)
            plotTrace(app.specMax.data(),
                      ImGui::GetColorU32(ImVec4(1.0f, 0.85f, 0.3f, 0.55f)),
                      0, false, 1.0f);
        const float* main = (app.specAvgOn && app.specAvg.size() == n)
                                ? app.specAvg.data()
                                : raw;
        plotTrace(main, ImGui::GetColorU32(ImVec4(0.22f, 0.71f, 1.0f, 1.0f)),
                  ImGui::GetColorU32(ImVec4(0.22f, 0.71f, 1.0f, 0.18f)),
                  true, 1.4f);
    }

    // Marker del VFO di ascolto: banda evidenziata, bordi trascinabili.
    // Le coordinate servono anche a righello e waterfall piu' sotto.
    static bool draggingBw = false;
    bool nearEdge = false;
    bool hasVfo = false;
    float vfoX = 0.0f, vfoX0 = 0.0f, vfoX1 = 0.0f;
    const ImU32 vfoCol = ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.9f));
    const ImU32 vfoColSoft =
        ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.5f));
    float mx = ImGui::GetIO().MousePos.x;
    if (app.listenMode != AppState::ListenMode::Off) {
        double bw = app.listenBwHz;
        double vfoHz = centerHz + app.listenOffsetHz;
        // In SSB la banda utile sta tutta da un lato della portante: USB
        // sopra [f, f+bw], LSB sotto [f-bw, f]. Il marker resta sulla
        // portante soppressa, cosi' (come su SDR#/SDR++) lo si mette sul
        // bordo del segnale e non al centro. Gli altri modi sono simmetrici.
        bool usb = app.listenMode == AppState::ListenMode::Usb;
        bool lsb = app.listenMode == AppState::ListenMode::Lsb;
        double loHz, hiHz;
        if (usb) { loHz = vfoHz; hiHz = vfoHz + bw; }
        else if (lsb) { loHz = vfoHz - bw; hiHz = vfoHz; }
        else { loHz = vfoHz - bw / 2; hiHz = vfoHz + bw / 2; }
        hasVfo = true;
        vfoX = xOf(vfoHz);
        vfoX0 = xOf(loHz);
        vfoX1 = xOf(hiHz);
        dl->AddRectFilled(ImVec2(vfoX0, s0.y), ImVec2(vfoX1, s1.y),
                          ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.15f)));
        dl->AddLine(ImVec2(vfoX, s0.y), ImVec2(vfoX, s1.y), vfoCol, 1.5f);
        dl->AddLine(ImVec2(vfoX0, s0.y), ImVec2(vfoX0, s1.y), vfoColSoft);
        dl->AddLine(ImVec2(vfoX1, s0.y), ImVec2(vfoX1, s1.y), vfoColSoft);
        // Etichetta del lato attivo accanto al marker, per non confondersi.
        if (usb || lsb)
            dl->AddText(ImVec2(vfoX + (usb ? 4 : -22), s1.y - 16), vfoCol,
                        usb ? "USB" : "LSB");

        // Il bordo trascinabile e' quello esterno del canale: in SSB solo il
        // lato della banda utile, negli altri modi entrambi (larghezza
        // simmetrica). Trascinandolo cambia la larghezza del canale.
        float dragEdgeX = usb ? vfoX1 : (lsb ? vfoX0 : mx); // mx = sempre "vicino"
        nearEdge = hovSpec && ((usb || lsb)
                       ? std::fabs(mx - dragEdgeX) < 8.0f
                       : (std::fabs(mx - vfoX0) < 8.0f ||
                          std::fabs(mx - vfoX1) < 8.0f));
        if (nearEdge || draggingBw)
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        if (nearEdge && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            draggingBw = true;
        if (draggingBw) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                double maxBw =
                    (app.listenMode == AppState::ListenMode::WfmStereo)
                        ? 220000.0 : 46000.0;
                // In SSB la larghezza e' la distanza dalla portante (un lato),
                // negli altri modi il doppio (banda simmetrica).
                double d = std::fabs(freqAt(mx) - vfoHz);
                app.listenBwHz =
                    std::clamp((usb || lsb) ? d : 2.0 * d, 100.0, maxBw);
            } else {
                draggingBw = false;
                rebuildChanFilter(app); // applica la nuova larghezza
            }
        }

        // Larghezza in chiaro (come SDR#): appare mentre trascini un bordo
        // o comunque quando il mouse e' sopra la banda del VFO, cosi' vedi
        // subito quanti kHz stai per selezionare.
        bool overBand = hovSpec && mx >= std::min(vfoX0, vfoX1) - 8.0f &&
                        mx <= std::max(vfoX0, vfoX1) + 8.0f;
        if (draggingBw || nearEdge || overBand) {
            char bwLbl[32];
            std::snprintf(bwLbl, sizeof(bwLbl), "%.1f kHz",
                          app.listenBwHz / 1e3);
            ImVec2 sz = ImGui::CalcTextSize(bwLbl);
            ImVec2 tp(xOf(vfoHz) - sz.x / 2, s0.y + 4);
            dl->AddRectFilled(ImVec2(tp.x - 4, tp.y - 2),
                              ImVec2(tp.x + sz.x + 4, tp.y + sz.y + 2),
                              ImGui::GetColorU32(ImVec4(0, 0, 0, 0.65f)),
                              3.0f);
            dl->AddText(tp, vfoCol, bwLbl);
        }
    }

    // --- Righello delle frequenze (cliccabile e trascinabile) ---
    ImGui::InvisibleButton("##freqscale", ImVec2(w, scaleH));
    bool hovScale = ImGui::IsItemHovered();
    bool activeScale = ImGui::IsItemActive();
    {
        ImVec2 r0 = ImGui::GetItemRectMin();
        ImVec2 r1 = ImGui::GetItemRectMax();
        dl->AddRectFilled(r0, r1,
                          ImGui::GetColorU32(ImVec4(0.07f, 0.09f, 0.13f, 1)));
        // Tacche minori (1/5 di passo) e maggiori con etichetta centrata.
        const double minor = step / 5.0;
        for (long long k = (long long)std::ceil(v0 / minor);
             double(k) * minor < v1; k++) {
            double f = double(k) * minor;
            float x = xOf(f);
            bool major = (k % 5) == 0;
            dl->AddLine(ImVec2(x, r0.y), ImVec2(x, r0.y + (major ? 8.f : 4.f)),
                        textCol);
            if (major) {
                char lbl[32];
                std::snprintf(lbl, sizeof(lbl), "%.*f", freqDecimals, f / 1e6);
                ImVec2 sz = ImGui::CalcTextSize(lbl);
                dl->AddText(ImVec2(x - sz.x / 2, r1.y - sz.y - 1), textCol,
                            lbl);
            }
        }
        // Indice del VFO sul righello (triangolino arancione).
        if (hasVfo && vfoX >= r0.x && vfoX <= r1.x)
            dl->AddTriangleFilled(ImVec2(vfoX - 5, r0.y),
                                  ImVec2(vfoX + 5, r0.y),
                                  ImVec2(vfoX, r0.y + 7), vfoCol);
    }

    // Divisore trascinabile: regola l'altezza spettro/waterfall.
    ImGui::InvisibleButton("##split", ImVec2(w, splitH));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive() && usableH > 1.0f)
        app.wfSplit = std::clamp(
            app.wfSplit + ImGui::GetIO().MouseDelta.y / usableH, 0.12f, 0.85f);
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
    // La texture e' in ordine di scrittura: la riga piu' recente va in
    // alto disegnando due segmenti con la V invertita.
    ImVec2 wfAvail = ImGui::GetContentRegionAvail();
    ImVec2 wp = ImGui::GetCursorScreenPos();
    bool hovWf = false;
    bool activeWf = false;
    if (wfAvail.y > 4.0f) {
        ImGui::InvisibleButton("##wfarea", wfAvail);
        hovWf = ImGui::IsItemHovered();
        activeWf = ImGui::IsItemActive();
    }
    if (app.waterfallTex && wfAvail.y > 4.0f) {
        int rows, head;
        {
            std::lock_guard<std::mutex> lk(app.wfMutex);
            rows = app.wfRows;
            head = app.waterfallHead;
        }
        float u0 = float((v0 - f0) / (f1 - f0));
        float u1 = float((v1 - f0) / (f1 - f0));
        ImTextureID tex = (ImTextureID)(intptr_t)app.waterfallTex;
        float vh = float(head) / float(rows);
        float h1 = wfAvail.y * vh;
        if (h1 > 0.5f)
            dl->AddImage(tex, wp, ImVec2(wp.x + wfAvail.x, wp.y + h1),
                         ImVec2(u0, vh), ImVec2(u1, 0.0f));
        if (wfAvail.y - h1 > 0.5f)
            dl->AddImage(tex, ImVec2(wp.x, wp.y + h1),
                         ImVec2(wp.x + wfAvail.x, wp.y + wfAvail.y),
                         ImVec2(u0, 1.0f), ImVec2(u1, vh));

        // Linea del VFO anche sul waterfall: stessa X dello spettro,
        // per calibrare la frequenza sulle tracce che scorrono.
        if (hasVfo && vfoX >= wp.x && vfoX <= wp.x + wfAvail.x) {
            dl->AddLine(ImVec2(vfoX, wp.y), ImVec2(vfoX, wp.y + wfAvail.y),
                        vfoCol, 1.2f);
            if (vfoX0 >= wp.x && vfoX1 <= wp.x + wfAvail.x &&
                vfoX1 - vfoX0 > 6.0f) {
                dl->AddLine(ImVec2(vfoX0, wp.y),
                            ImVec2(vfoX0, wp.y + wfAvail.y), vfoColSoft, 0.8f);
                dl->AddLine(ImVec2(vfoX1, wp.y),
                            ImVec2(vfoX1, wp.y + wfAvail.y), vfoColSoft, 0.8f);
            }
        }
    }

    // --- Interazione di sintonia, comune a spettro/righello/waterfall ---
    // click  = sintonizza (snap) alla frequenza PREMUTA (non a quella al
    //          rilascio: cosi' un tremolio del mouse non sposta il tiro);
    // trascina             = sintonia continua ("afferri" lo spettro);
    // rotellina            = cambia frequenza (passi di snap);
    // Ctrl+rotellina o leva Zoom = ingrandisci;
    // doppio click         = centra l'hardware qui.
    static bool draggingTune = false;
    static bool dragMoved = false;
    static float dragStartX = 0.0f;
    static double dragStartFreq = 0.0;
    static double pressFreq = 0.0;

    // Le tre aree (spettro, righello, waterfall) sono unificate qui: il
    // drag e' pilotato dallo stato "active" del pulsante ImGui su cui hai
    // premuto (robusto: continua finche' tieni premuto, ovunque vada il
    // mouse). Cosi' il trascinamento dal righello non "salta" piu'.
    const bool anyActive = activeSpec || activeScale || activeWf;
    const bool tuneHover = (hovSpec || hovScale || hovWf) && !draggingBw;

    if (tuneHover && !draggingTune) {
        double f = freqAt(mx);
        // Guida verticale di puntamento: solo al passaggio del mouse (non
        // durante il click/drag), cosi' non resta "impressa" sullo spettro.
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            dl->AddLine(ImVec2(mx, s0.y), ImVec2(mx, wp.y + wfAvail.y),
                        ImGui::GetColorU32(ImVec4(1, 1, 1, 0.22f)));
        // Tooltip compatto: solo la frequenza (l'aiuto completo sta nel
        // titolo della sezione, non serve coprire il waterfall).
        ImGui::SetTooltip("%.4f MHz", f / 1e6);

        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyCtrl) {
                // Zoom (avanzato) tenendo ferma la frequenza sotto il
                // cursore; lo zoom "vero" resta la leva laterale.
                double frac = double((mx - p0.x) / w);
                app.viewSpanFrac =
                    std::clamp(app.viewSpanFrac * std::pow(0.8, double(wheel)),
                               0.005, 1.0);
                double newSpan = (f1 - f0) * app.viewSpanFrac;
                double newV0 = f - frac * newSpan;
                app.viewCenterFrac =
                    (newV0 + newSpan / 2.0 - f0) / (f1 - f0);
            } else {
                // Rotellina = cambia frequenza a passi di snap.
                double cur = centerHz + app.listenOffsetHz;
                double next = std::round((cur + double(wheel) * app.snapHz) /
                                         app.snapHz) * app.snapHz;
                applyTunedFrequency(app, next);
            }
        }

        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            double t = app.snapToPeak
                           ? nearestSpectrumPeak(app, f0, f1, f, app.snapHz)
                           : f;
            double snapped = app.snapToPeak
                                 ? t
                                 : std::round(t / app.snapHz) * app.snapHz;
            applyTunedFrequency(app, snapped, /*forceHwRetune=*/true);
        } else if (!nearEdge && anyActive) {
            // Premuto su una delle tre aree: inizia il drag/click di sintonia.
            draggingTune = true;
            dragMoved = false;
            dragStartX = mx;
            dragStartFreq = centerHz + app.listenOffsetHz;
            pressFreq = f;
        }
    }

    // Drag attivo: prosegue finche' il tasto resta premuto, anche se il
    // cursore esce dall'area di partenza.
    if (draggingTune) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float dx = mx - dragStartX;
            if (std::fabs(dx) > 5.0f) dragMoved = true;
            if (dragMoved) {
                // "Afferri" lo spettro: mouse a destra = frequenze piu' basse.
                double hzPerPx = (v1 - v0) / double(w);
                applyTunedFrequency(app, dragStartFreq - double(dx) * hzPerPx);
            }
        } else {
            if (!dragMoved) {
                // Click secco: sintonizza sul punto premuto. Con "snap al
                // picco" attivo aggancia il segnale piu' forte li' vicino,
                // altrimenti arrotonda al passo di sintonia.
                double snapped =
                    app.snapToPeak
                        ? nearestSpectrumPeak(app, f0, f1, pressFreq,
                                              app.snapHz)
                        : std::round(pressFreq / app.snapHz) * app.snapHz;
                applyTunedFrequency(app, snapped);
            }
            draggingTune = false;
        }
    }
    ImGui::End();
}

// Colonna a destra dello spettro (finestra a parte, cosi' i controlli
// NON finiscono sopra il waterfall e restano sempre cliccabili): levette
// verticali Zoom/Contrasto/Range/Offset + S-meter verticale in fondo.
void drawRightStrip(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float stripW = 88.0f;
    ImGui::SetNextWindowPos(ImVec2(vp->WorkSize.x - stripW - 10, 10),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(stripW, vp->WorkSize.y - 20 - kStatusBarH),
                             ImGuiCond_Always);
    ImGui::Begin("Vista", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse);

    const float availH = ImGui::GetContentRegionAvail().y;
    // 4 levette in alto, S-meter in basso: ognuna con etichetta + slider.
    const float sliderH = std::max(40.0f, (availH - 130.0f) / 4.0f - 18.0f);
    auto label = [](const char* t) {
        float wv = ImGui::GetContentRegionAvail().x;
        float tw = ImGui::CalcTextSize(t).x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f, (wv - tw) * 0.5f));
        ImGui::TextUnformatted(t);
    };
    auto centerSlider = [](float wide) {
        float wv = ImGui::GetContentRegionAvail().x;
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                             std::max(0.0f, (wv - wide) * 0.5f));
    };

    label("Zoom");
    centerSlider(30);
    float zoomV = float(std::log(1.0 / app.viewSpanFrac) / std::log(200.0));
    if (ImGui::VSliderFloat("##lvZoom", ImVec2(30, sliderH), &zoomV, 0.0f,
                            1.0f, "")) {
        app.viewSpanFrac = 1.0 / std::pow(200.0, double(zoomV));
        centerViewOnTuned(app);
    }
    helpTip("Ingrandimento dello spettro attorno alla frequenza "
            "sintonizzata.");

    label("Contr");
    centerSlider(30);
    if (ImGui::VSliderFloat("##lvContr", ImVec2(30, sliderH), &app.wfContrast,
                            0.3f, 3.0f, ""))
        app.wfFullRedraw = true;
    helpTip("Contrasto del waterfall (curva dei colori).");

    label("Range");
    centerSlider(30);
    float span = app.rangeMaxDb - app.rangeMinDb;
    if (ImGui::VSliderFloat("##lvRange", ImVec2(30, sliderH), &span, 20.0f,
                            140.0f, "")) {
        app.rangeMinDb = app.rangeMaxDb - span;
        app.wfFullRedraw = true;
    }
    helpTip("Ampiezza della scala dB mostrata (dinamica).");

    label("Offset");
    centerSlider(30);
    float off = app.rangeMaxDb;
    if (ImGui::VSliderFloat("##lvOffset", ImVec2(30, sliderH), &off, -60.0f,
                            10.0f, "")) {
        float keepSpan = app.rangeMaxDb - app.rangeMinDb;
        app.rangeMaxDb = off;
        app.rangeMinDb = off - keepSpan;
        app.wfFullRedraw = true;
    }
    helpTip("Sposta in alto/basso la scala dB (tetto del fondoscala).");
    // (L'S-meter e' ora nella barra di stato in fondo, piu' leggibile.)

    ImGui::End();
}

void drawReceiverSection(AppState& app)
{
    drawSMeter(app);

    // Demodulatore a scelta rapida (stile SDR#): un click, niente tendina.
    int mode = int(app.listenMode);
    bool modeChanged = false;
    modeChanged |= ImGui::RadioButton("NFM", &mode, 2);
    ImGui::SameLine(78);
    modeChanged |= ImGui::RadioButton("AM", &mode, 3);
    ImGui::SameLine(156);
    modeChanged |= ImGui::RadioButton("USB", &mode, 4);
    modeChanged |= ImGui::RadioButton("WFM", &mode, 1);
    ImGui::SameLine(78);
    modeChanged |= ImGui::RadioButton("LSB", &mode, 5);
    if (modeChanged) {
        app.listenMode = AppState::ListenMode(mode);
        rebuildListener(app);
    }
    helpTip("Tipo di demodulazione: WFM per la radio FM, NFM per apparati "
            "e servizi, AM per aereo/onde medie, USB/LSB per SSB e "
            "radioamatori. Per silenziare usa Mute (accanto al volume).");
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

    // Mute istantaneo accanto al volume.
    if (ImGui::Button(app.muted ? "Muto" : "Audio")) app.muted = !app.muted;
    helpTip("Mute istantaneo dell'audio (non tocca il volume impostato).");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1);
    ImGui::SliderFloat("##Volume", &app.volume, 0.0f, 1.5f, "vol %.2f");
    helpTip("Volume dell'audio in uscita (fino a 1.5x per i segnali "
            "deboli; oltre 1.0 puo' distorcere se il segnale e' forte).");

    // Larghezza di banda del canale: slider (log) + preset rapidi. Per
    // HF/CW servono filtri stretti (200-500 Hz), 1 kHz e' gia' largo.
    if (app.listenMode != AppState::ListenMode::Off &&
        app.listenMode != AppState::ListenMode::WfmStereo) {
        float bwHz = float(app.listenBwHz);
        if (ImGui::SliderFloat("Larghezza", &bwHz, 100.0f, 20000.0f,
                               "%.0f Hz", ImGuiSliderFlags_Logarithmic)) {
            app.listenBwHz = double(bwHz);
            rebuildChanFilter(app);
        }
        helpTip("Larghezza del filtro di canale. CW 250-500 Hz, SSB "
                "2.4-2.7 kHz, AM 6-9 kHz, NFM 12.5 kHz. Puoi anche "
                "trascinare i bordi della banda sullo spettro.");
        // Preset rapidi tipici.
        struct BwPreset { const char* name; double hz; };
        static const BwPreset kBw[] = {
            {"CW 300", 300}, {"CW 500", 500}, {"SSB 2.4k", 2400},
            {"AM 6k", 6000}, {"AM 9k", 9000}, {"NFM 12.5k", 12500}};
        for (int i = 0; i < 6; i++) {
            if (i % 3) ImGui::SameLine();
            if (ImGui::SmallButton(kBw[i].name)) {
                app.listenBwHz = kBw[i].hz;
                rebuildChanFilter(app);
            }
        }
    }

    // Passo di sintonia per click e rotellina. Passi fini (10-500 Hz) per
    // SSB/CW in onde corte, medi per broadcast/apparati.
    static const double kSnaps[] = {10, 50, 100, 500, 1000, 2500, 5000,
                                    9000, 10000, 12500, 25000, 50000, 100000};
    static const char* kSnapNames[] = {
        "10 Hz (SSB/CW)", "50 Hz",  "100 Hz",  "500 Hz",   "1 kHz",
        "2.5 kHz",        "5 kHz",  "9 kHz (OM)", "10 kHz",  "12.5 kHz",
        "25 kHz",         "50 kHz", "100 kHz"};
    const int nSnaps = int(sizeof(kSnaps) / sizeof(kSnaps[0]));
    int snapIdx = 9; // default 12.5 kHz
    for (int i = 0; i < nSnaps; i++)
        if (std::fabs(kSnaps[i] - app.snapHz) < 1) snapIdx = i;
    fieldLabel("Snap (passo di sintonia)");
    if (ImGui::Combo("##snap", &snapIdx, kSnapNames, nSnaps))
        app.snapHz = kSnaps[snapIdx];
    helpTip("Passo di sintonia: click e rotellina saltano di questo valore. "
            "10-500 Hz per SSB/CW in onde corte, 9 kHz onde medie, 5 kHz FM, "
            "12.5/25 kHz apparati.");
    ImGui::Checkbox("Aggancia al picco", &app.snapToPeak);
    helpTip("Al click di sintonia salta sul segnale piu' forte li' vicino "
            "(comodo su AM/FM con portante; in SSB e' solo indicativo).");
    // Lo zoom vive sulla leva 'Zoom' a destra dello spettro (niente
    // doppione qui).

    ImGui::SeparatorText("Squelch");
    ImGui::Checkbox("Attivo##sq", &app.squelchOn);
    helpTip("Muto automatico sotto la soglia: l'audio passa solo quando il "
            "segnale supera il livello impostato (silenzia il fruscio).");
    ImGui::SameLine();
    ImGui::TextDisabled(app.squelchOn ? (app.squelchOpen ? "APERTO" : "chiuso")
                                      : "");
    fieldLabel("Soglia (dB)");
    ImGui::SliderFloat("##sqthr", &app.squelchDb, -100.0f, 0.0f, "%.0f");
    helpTip("Livello di apertura: alzalo finche' il fruscio tace ma il "
            "segnale utile passa ancora.");
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
    helpTip("Passa-alto toglie ronzio/rumble bassi; passa-basso toglie il "
            "fruscio acuto. Per la voce prova 300 Hz / 3000 Hz.");
    if (changed) {
        std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
        app.filterL.configure(48000.0, double(app.audioHighPassHz),
                              double(app.audioLowPassHz));
        app.filterR.configure(48000.0, double(app.audioHighPassHz),
                              double(app.audioLowPassHz));
    }
    // Notch: scava il fischio alla frequenza scelta (anche con un click
    // sullo Spettro audio).
    if (ImGui::SliderFloat("Notch (Hz)", &app.notchHz, 0.0f, 6000.0f,
                           app.notchHz < 1 ? "spento" : "%.0f")) {
        std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
        app.filterL.configureNotch(48000.0, double(app.notchHz));
        app.filterR.configureNotch(48000.0, double(app.notchHz));
    }

    ImGui::SeparatorText("Noise blanker");
    bool nbChanged = false;
    nbChanged |= ImGui::Checkbox("Attivo##nb", &app.nbOn);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140);
    nbChanged |= ImGui::SliderFloat("Soglia##nb", &app.nbThreshold, 2.0f,
                                    10.0f, "%.1fx");
    if (nbChanged) {
        std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
        app.noiseBlanker.configure(app.nbOn, app.nbThreshold);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Taglia i disturbi impulsivi (accensioni, motori):"
                          "\npiu' bassa la soglia, piu' aggressivo.");
}

// Definite piu' avanti; nella sidebar compaiono come menu a tendina.
void drawAudioSection(AppState& app);
void drawAudioSpectrumSection(AppState& app);
void drawTextDecoderSection(AppState& app);
void drawTetraSection(AppState& app);
void drawScannerSection(AppState& app);
void drawSatellitesSection(AppState& app);
void drawFrequenciesSection(AppState& app);
void drawModulesSection(AppState& app);
void drawLogSection(AppState& app);
void drawBandsSection(AppState& app);

// Porta la radio su una banda: modo + larghezza + frequenza in un colpo.
void goToBand(AppState& app, double freqHz, AppState::ListenMode mode,
              double bwHz)
{
    if (app.listenMode != mode) {
        app.listenMode = mode;
        rebuildListener(app);
    }
    if (bwHz > 0 && mode != AppState::ListenMode::Off &&
        mode != AppState::ListenMode::WfmStereo) {
        app.listenBwHz = bwHz;
        rebuildChanFilter(app);
    }
    tuneAbsolute(app, freqHz);
}

// Scorciatoie di banda: un click e sei sintonizzato col modo giusto.
void drawBandsSection(AppState& app)
{
    ImGui::TextDisabled("Un click: frequenza + modo + larghezza pronti.");
    struct Band {
        const char* name;
        double freqHz;
        AppState::ListenMode mode;
        double bwHz;
    };
    static const Band kBands[] = {
        {"FM 98.0", 98.0e6, AppState::ListenMode::WfmStereo, 0},
        {"Aereo 127", 127.0e6, AppState::ListenMode::Am, 9000},
        {"PMR 446", 446.0e6, AppState::ListenMode::Nfm, 12500},
        {"CB 27", 27.185e6, AppState::ListenMode::Am, 6000},
        {"2m 145", 145.0e6, AppState::ListenMode::Nfm, 12500},
        {"70cm 433", 433.5e6, AppState::ListenMode::Nfm, 12500},
        {"40m 7.1", 7.1e6, AppState::ListenMode::Lsb, 2700},
        {"20m 14.2", 14.2e6, AppState::ListenMode::Usb, 2700},
        {"OM 1000", 1.0e6, AppState::ListenMode::Am, 9000},
    };
    int n = int(sizeof(kBands) / sizeof(kBands[0]));
    float bw3 = (ImGui::GetContentRegionAvail().x -
                 2.0f * ImGui::GetStyle().ItemSpacing.x) / 3.0f;
    for (int i = 0; i < n; i++) {
        if (i % 3) ImGui::SameLine();
        if (ImGui::Button(kBands[i].name, ImVec2(bw3, 0)))
            goToBand(app, kBands[i].freqHz, kBands[i].mode, kBands[i].bwHz);
    }
}

// Barra di stato in fondo: sorgente, rate, VFO/modo, moduli attivi e
// riempimento dell'anello IQ (spia della salute: se sale, il DSP e'
// indietro e conviene spegnere qualche modulo).
void drawStatusBar(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(
        ImVec2(10, vp->WorkSize.y - kStatusBarH - 6), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - 20, kStatusBarH),
                             ImGuiCond_Always);
    // Padding verticale ridotto: la riga di testo (font 17) resta centrata
    // e il bordo racchiude bene la barra su tutti e quattro i lati (prima
    // il contenuto sfondava e il bordo sotto spariva).
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 7.0f));
    ImGui::Begin("##statusbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoSavedSettings);

    const ImU32 sep = ImGui::GetColorU32(ImVec4(0.4f, 0.46f, 0.54f, 1));
    auto bar = [&]() {
        ImGui::SameLine();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(sep), "|");
        ImGui::SameLine();
    };

    ImGui::Text("%s", app.source ? app.source->name().c_str()
                                  : "nessuna sorgente");
    bar();
    ImGui::Text("%.3f MS/s", app.sampleRate / 1e6);
    bar();
    if (app.listenMode != AppState::ListenMode::Off)
        ImGui::Text("VFO %.4f MHz %s  %.1f kHz",
                    (app.freqMHz * 1e6 + app.listenOffsetHz) / 1e6,
                    kListenModeNames[int(app.listenMode)],
                    app.listenBwHz / 1e3);
    else
        ImGui::TextUnformatted("VFO spento");
    bar();
    int nOn = 0;
    for (char e : app.moduleEnabled) nOn += e ? 1 : 0;
    ImGui::Text("moduli attivi: %d", nOn);
    bar();
    // Riempimento dell'anello IQ: verde ok, giallo/rosso = DSP indietro.
    double fill = double(app.iqRing.available()) / double(1 << 20) * 100.0;
    ImVec4 fillCol = fill < 30 ? ImVec4(0.4f, 0.85f, 0.5f, 1)
                    : fill < 70 ? ImVec4(1.0f, 0.8f, 0.3f, 1)
                                : ImVec4(1.0f, 0.4f, 0.4f, 1);
    ImGui::TextColored(fillCol, "buffer IQ %.0f%%", fill);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Riempimento dell'anello IQ. Se resta alto, il "
                          "PC fatica: spegni qualche modulo o abbassa il "
                          "sample rate.");
    if (app.muted) {
        bar();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1), "MUTO");
    }

    // S-meter TESTUALE a DESTRA della barra (la barretta segmentata era un
    // doppione della lancetta analogica in Ricevitore: qui basta il numero,
    // sempre visibile anche a pannello chiuso). Spia OVL se il canale e'
    // vicino al fondo scala (front-end in saturazione).
    if (app.listenMode != AppState::ListenMode::Off) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 wpos = ImGui::GetWindowPos();
        ImVec2 wsz = ImGui::GetWindowSize();
        char sName[16];
        sMeterLabel(app.chanLevelDb, sName, sizeof(sName));
        bool ovl = app.chanLevelDb > -3.0f; // vicino a 0 dBFS = saturazione
        char sTxt[48];
        std::snprintf(sTxt, sizeof(sTxt), "%s  %.0f dBFS%s", sName,
                      double(app.chanLevelDb), ovl ? "  OVL" : "");
        ImVec2 tsz = ImGui::CalcTextSize(sTxt);
        const auto& pal = sdrjo::gui::palette();
        ImU32 col = ImGui::GetColorU32(ovl ? pal.bad : pal.text);
        dl->AddText(ImVec2(wpos.x + wsz.x - tsz.x - 12,
                           wpos.y + (wsz.y - tsz.y) * 0.5f),
                    col, sTxt);
    }
    ImGui::End();
    ImGui::PopStyleVar(); // WindowPadding
}

// Rivelatore di ATTIVITA' TETRA: dice solo se c'e' un portante digitale
// largo ~canale sopra il rumore. NIENTE decodifica ne' decifratura (la
// voce TETRA e' quasi sempre cifrata e intercettarla e' illegale).
void drawTetraSection(AppState& app)
{
    ImGui::TextWrapped("Rileva solo la PRESENZA di un portante tipo TETRA "
                       "(25 kHz). Non decodifica ne' decifra nulla: la voce "
                       "e' cifrata e l'intercettazione e' illegale.");
    if (app.listenMode == AppState::ListenMode::Off ||
        app.listenMode == AppState::ListenMode::WfmStereo) {
        ImGui::TextDisabled("usa NFM/AM sul canale sospetto, poi attiva qui");
    }

    ImGui::TextDisabled("Interruttore sul titolo per attivare. Sintonizza il "
                        "canale sospetto (25 kHz) in NFM e osserva la spia.");

    if (!app.tetraDetectOn) return;

    bool act = app.tetraActive.load(std::memory_order_relaxed);
    float snr = app.tetraSnr.load(std::memory_order_relaxed);
    float occ = app.tetraOcc.load(std::memory_order_relaxed);
    float lvl = app.tetraLevel.load(std::memory_order_relaxed);

    // LED + stato.
    ImVec2 lp = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(lp.x + 7, lp.y + 8), 6.0f,
        act ? IM_COL32(60, 200, 90, 255) : IM_COL32(120, 120, 120, 255));
    ImGui::Dummy(ImVec2(18, 16));
    ImGui::SameLine();
    ImGui::TextUnformatted(act ? "ATTIVO (portante rilevato)" : "nessuna attivita'");

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                          act ? ImVec4(0.36f, 0.82f, 0.45f, 1.0f)
                              : ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
    ImGui::ProgressBar(std::clamp(lvl, 0.0f, 1.0f), ImVec2(-FLT_MIN, 12.0f));
    ImGui::PopStyleColor();
    ImGui::Text("SNR banda/rumore: %.1f dB", double(snr));
    ImGui::Text("Larghezza occupata: ~%.1f kHz", double(occ));
    ImGui::TextDisabled("(TETRA riempie ~18-24 kHz del canale da 25)");
}

// Header di sezione con interruttore ON/OFF sul titolo (per le funzioni
// attivabili). Ritorna se la sezione e' aperta; 'changed' = casella premuta.
bool sectionHeaderToggle(const char* id, const char* label, bool& en,
                         bool& changed)
{
    ImGui::PushID(id);
    changed = ImGui::Checkbox("##en", &en);
    ImGui::SameLine();
    const auto& pal = sdrjo::gui::palette();
    if (en) ImGui::PushStyleColor(ImGuiCol_Text, pal.acc);
    char hdr[96];
    std::snprintf(hdr, sizeof(hdr), "%s###hdr", label);
    bool open = ImGui::CollapsingHeader(hdr);
    if (en) ImGui::PopStyleColor();
    ImGui::PopID();
    return open;
}

// Sidebar unica: tutte le sezioni di controllo in una colonna, apribili
// e richiudibili come menu a tendina (spettro e waterfall tengono cosi'
// tutta la parte destra dello schermo).
void drawSidebar(AppState& app)
{
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float sideW = std::clamp(vp->WorkSize.x * 0.24f, 300.0f, 420.0f);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(sideW, vp->WorkSize.y - 20 - kStatusBarH),
                             ImGuiCond_Always);
    ImGui::Begin("Controlli", nullptr, ImGuiWindowFlags_NoMove);

    if (ImGui::CollapsingHeader("Dispositivo", ImGuiTreeNodeFlags_DefaultOpen))
        drawDeviceSection(app);
    if (ImGui::CollapsingHeader("Ricevitore", ImGuiTreeNodeFlags_DefaultOpen))
        drawReceiverSection(app);
    if (ImGui::CollapsingHeader("Posizione antenna"))
        drawStationSection(app);
    if (ImGui::CollapsingHeader("Bande rapide"))
        drawBandsSection(app);
    if (ImGui::CollapsingHeader("Spettro audio"))
        drawAudioSpectrumSection(app);
    if (ImGui::CollapsingHeader("Decoder testi"))
        drawTextDecoderSection(app);
    {
        bool ch = false, en = app.tetraDetectOn;
        bool open = sectionHeaderToggle("tetra", "TETRA (attivita')", en, ch);
        if (ch) {
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            app.tetraDetectOn = en;
            if (en)
                app.tetraDet = std::make_unique<sdrjo::dsp::TetraActivityDetector>(
                    48000.0);
            else
                app.tetraDet.reset();
            app.tetraActive.store(false);
        }
        if (open) drawTetraSection(app);
    }
    {
        bool ch = false, en = app.scanOn;
        bool open = sectionHeaderToggle("scan", "Scanner", en, ch);
        if (ch) app.scanOn = en;
        if (open) drawScannerSection(app);
    }
    if (ImGui::CollapsingHeader("Satelliti"))
        drawSatellitesSection(app);
    if (ImGui::CollapsingHeader("Frequenze"))
        drawFrequenciesSection(app);
    if (ImGui::CollapsingHeader("Moduli"))
        drawModulesSection(app);
    if (ImGui::CollapsingHeader("Audio"))
        drawAudioSection(app);
    if (ImGui::CollapsingHeader("Log"))
        drawLogSection(app);

    ImGui::End();
}

// Frequency manager: memorie salvate su file, riordinate per frequenza.
void drawFrequenciesSection(AppState& app)
{
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
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(0, 220))) {
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
}

void drawAudioSection(AppState& app)
{
    // Scala dell'interfaccia (utile su monitor 4K): agisce su tutti i font.
    ImGui::SetNextItemWidth(160);
    ImGui::SliderFloat("Scala interfaccia", &app.uiScale, 0.7f, 2.0f,
                       "%.2fx");
    helpTip("Ingrandisce/rimpicciolisce tutti i testi dell'app "
            "(comodo sui monitor ad alta risoluzione).");
    ImGui::Separator();

    ImGui::TextDisabled("backend: %s", app.audio.backendName().c_str());

    // Scheda di uscita.
    std::string current = (app.audioDeviceIndex < 0 ||
                           app.audioDeviceIndex >= int(app.audioDevices.size()))
                              ? "Predefinita di sistema"
                              : app.audioDevices[size_t(app.audioDeviceIndex)];
    fieldLabel("Scheda di uscita");
    if (ImGui::BeginCombo("##scheda", current.c_str())) {
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
    if (ImGui::SmallButton("Aggiorna schede"))
        app.audioDevices = sdrjo::AudioOutput::listDevices();

    // Frequenza di campionamento dell'uscita.
    static const int kAudioRates[] = {44100, 48000, 96000};
    static const char* kAudioRateNames[] = {"44100 Hz", "48000 Hz", "96000 Hz"};
    int aIdx = (app.audioRateHz == 44100) ? 0 : (app.audioRateHz == 96000 ? 2 : 1);
    fieldLabel("Frequenza audio");
    if (ImGui::Combo("##arate", &aIdx, kAudioRateNames, 3)) {
        app.audioRateHz = kAudioRates[aIdx];
        app.audio.stop();
    }

    // ---- Accesso remoto al Cockpit (LAN, con password) ----
    ImGui::SeparatorText("Accesso remoto");
    static bool lanEnabled = false;
    static char lanPass[64] = "";
    ImGui::Checkbox("Esponi il Cockpit in LAN", &lanEnabled);
    fieldLabel("Password");
    ImGui::InputText("##lanpass", lanPass, sizeof(lanPass),
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
            // Anche lo streaming WebSocket segue: nuovo token e stessa
            // esposizione (solo locale o LAN) del Cockpit.
            app.wsAudio.stop();
            std::string tok = randomToken();
            app.wsAudio.setToken(tok);
            if (app.wsAudio.start(sdrjo::WsAudioServer::kDefaultPort,
                                  lanEnabled))
                app.cockpit.setWsInfo(app.wsAudio.port(), tok);
            else
                app.cockpit.setWsInfo(0, "");
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

void drawModulesSection(AppState& app)
{
    if (app.modules.empty()) {
        ImGui::TextWrapped("Nessun modulo caricato. Copia i moduli (*.dll) "
                           "nella cartella 'modules' accanto all'eseguibile.");
        return;
    }

    ImGui::TextWrapped("I moduli partono spenti: accendi solo quello che ti "
                       "serve. Ogni decoder attivo elabora il segnale e "
                       "consuma CPU, quindi tenerne spenti alleggerisce "
                       "l'app.");
    ImGui::Spacing();

    const ImVec4 onCol(0.24f, 0.86f, 0.59f, 1.0f);   // verde = attivo
    const ImVec4 offCol(0.55f, 0.60f, 0.66f, 1.0f);  // grigio = spento

    if (app.moduleEnabled.size() != app.modules.size())
        app.moduleEnabled.assign(app.modules.size(), 0);

    for (size_t i = 0; i < app.modules.size(); i++) {
        auto& lm = app.modules[i];
        auto info = lm.module()->info();
        bool en = app.moduleEnabled[i] != 0;
        ImGui::PushID(int(i));

        // Interruttore attiva/disattiva DIRETTAMENTE sul titolo del modulo:
        // lo accendi/spegni senza dover aprire il sottomenu. Il titolo e'
        // un'intestazione richiudibile: aprila per i dettagli e i comandi.
        if (ImGui::Checkbox("##on", &en)) {
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            app.moduleEnabled[i] = en ? 1 : 0;
            app.log(info.name, en ? "attivato" : "spento");
        }
        helpTip(en ? "Modulo attivo: sta elaborando il segnale."
                   : "Modulo spento: non consuma CPU. Accendilo per usarlo.");
        ImGui::SameLine();
        char hdr[96];
        std::snprintf(hdr, sizeof(hdr), "%s  (%s)###mod%zu", info.name.c_str(),
                      en ? "attivo" : "spento", i);
        ImGui::PushStyleColor(ImGuiCol_Text, en ? onCol : offCol);
        bool open = ImGui::CollapsingHeader(hdr);
        ImGui::PopStyleColor();

        // Dettagli e comandi quando apri il sottomenu del modulo.
        if (open) {
            ImGui::Indent(10.0f);
            ImGui::TextDisabled("%s", info.description.c_str());
            if (!en)
                ImGui::TextColored(ImVec4(1.0f, 0.71f, 0.33f, 1.0f),
                                   "spento: attiva la casella per usarlo");
            uint16_t port = lm.module()->webPort();
            if (port)
                ImGui::Text("Interfaccia web: http://localhost:%u", port);
            if (info.preferredFreqHz > 0) {
                double cur = app.freqMHz * 1e6;
                bool onFreq =
                    std::fabs(cur - info.preferredFreqHz) < app.sampleRate;
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "Sintonizza (%.3f MHz)",
                              info.preferredFreqHz / 1e6);
                if (ImGui::SmallButton(lbl)) {
                    std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
                    app.listenOffsetHz = 0.0; // il modulo vuole la banda intera
                    requestHwCenter(app, info.preferredFreqHz,
                                    /*immediate=*/true);
                    if (app.listenVfo) app.listenVfo->setOffset(0.0);
                    centerViewOnTuned(app);
                }
                if (!onFreq) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1.0f, 0.71f, 0.33f, 1.0f),
                                       "(fuori banda)");
                    helpTip("La chiavetta non e' sulla frequenza del modulo: "
                            "premi Sintonizza per riceverlo davvero.");
                }
            }
            lm.module()->drawUi();
            ImGui::Unindent(10.0f);
        }
        ImGui::Separator();
        ImGui::PopID();
    }
    ImGui::TextDisabled("Cockpit completo: http://localhost:%u",
                        app.cockpit.port());
}

void drawLogSection(AppState& app)
{
    // Riquadro scorrevole: il log non deve allungare tutta la sidebar. Le
    // righe vanno a capo (wrap) cosi' i messaggi lunghi (es. diagnostica di
    // sintonia) si leggono per intero senza tagli a destra.
    if (ImGui::BeginChild("##logscroll", ImVec2(0, 200),
                          ImGuiChildFlags_Borders)) {
        std::lock_guard<std::mutex> lk(app.logMutex);
        ImGui::PushTextWrapPos(0.0f);
        for (auto& line : app.logLines)
            ImGui::TextUnformatted(line.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    if (ImGui::SmallButton("Copia log negli appunti")) {
        std::lock_guard<std::mutex> lk(app.logMutex);
        std::string all;
        for (auto& line : app.logLines) { all += line; all += '\n'; }
        ImGui::SetClipboardText(all.c_str());
    }
    helpTip("Copia tutte le righe di log: utile per incollarle e farsele "
            "analizzare (es. la diagnostica di sintonia).");
}

// Parametri RTTY condivisi (usati sia dal pannello decoder sia dal
// click-to-tune sullo Spettro audio).
const double kRttyBaud[] = {45.45, 50.0, 75.0};
const double kRttyShift[] = {170.0, 425.0, 850.0};

// Aggiunge un carattere decodificato al buffer di testo (tetto ~4000).
void emitDecoded(AppState& app, char c)
{
    app.decodedText += c;
    if (app.decodedText.size() > 4000) app.decodedText.erase(0, 1000);
}

// (Ri)costruisce il decoder RTTY coi parametri correnti.
void rebuildRttyDecoder(AppState& app)
{
    double baud = kRttyBaud[std::clamp(app.rttyBaudIdx, 0, 2)];
    double shift = kRttyShift[std::clamp(app.rttyShiftIdx, 0, 2)];
    app.rttyDecoder = std::make_unique<sdrjo::dsp::RttyDecoder>(
        48000.0, [&app](char c) { emitDecoded(app, c); },
        double(app.rttyMarkHz), double(app.rttyMarkHz) + shift, baud);
    app.rttyDecoder->setReverse(app.rttyReverse);
}

// Sintonizza il decoder attivo su un tono audio (click-to-tune fldigi):
// CW/PSK spostano il tono, RTTY sposta il mark (lo space segue lo shift).
void setDecoderTone(AppState& app, double hz)
{
    std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
    switch (app.textDecoderMode) {
    case 1:
        app.cwToneHz = float(std::clamp(hz, 300.0, 1200.0));
        app.cwTone.configure(double(app.cwToneHz), 48000.0);
        break;
    case 2:
        app.rttyMarkHz = float(std::clamp(hz, 800.0, 2500.0));
        rebuildRttyDecoder(app);
        break;
    case 3:
        app.psk31ToneHz = float(std::clamp(hz, 300.0, 2500.0));
        if (app.psk31Decoder)
            app.psk31Decoder->setTone(double(app.psk31ToneHz));
        break;
    default:
        break;
    }
}

// Colore "waterfall" da un livello 0..1 (blu scuro -> ciano -> giallo).
ImU32 waterfallColor(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    float r, g, b;
    if (t < 0.5f) { float u = t / 0.5f; r = 0.0f; g = 0.35f * u; b = 0.25f + 0.6f * u; }
    else { float u = (t - 0.5f) / 0.5f; r = u; g = 0.35f + 0.55f * u; b = 0.85f * (1.0f - u); }
    return IM_COL32(int(r * 255), int(g * 255), int(b * 255), 255);
}

// Spettro dell'audio demodulato (0-6 kHz): utile per CW/SSB/RTTY e per
// mirare il notch: un click sul grafico lo piazza sul fischio.
void drawAudioSpectrumSection(AppState& app)
{
    if (app.listenMode == AppState::ListenMode::Off) {
        ImGui::TextDisabled("accendi un demodulatore per vedere l'audio");
        return;
    }

    const size_t n = app.audioTap.size();
    static std::vector<float> tap;
    tap.resize(n);
    {
        std::lock_guard<std::mutex> lk(app.audioTapMutex);
        for (size_t i = 0; i < n; i++)
            tap[i] = app.audioTap[(app.audioTapPos + i) % n];
    }
    static std::vector<sdrjo::cfloat> cbuf;
    static std::vector<float> spec;
    cbuf.resize(n);
    spec.resize(n);
    for (size_t i = 0; i < n; i++) cbuf[i] = sdrjo::cfloat(tap[i], 0.0f);
    sdrjo::dsp::powerSpectrumDb(cbuf.data(), n, spec.data());

    // DC al centro: le frequenze audio positive sono la seconda meta'.
    const double rate = 48000.0;
    const double maxHz = 6000.0;
    const size_t bins = size_t(maxHz / rate * double(n));
    const float dbLo = -100.0f, dbHi = -10.0f;

    const float w = ImGui::GetContentRegionAvail().x;
    const float h = 110.0f;
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGui::InvisibleButton("##audiospec", ImVec2(std::max(60.0f, w), h));
    bool hov = ImGui::IsItemHovered();
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h),
                      ImGui::GetColorU32(ImVec4(0.03f, 0.04f, 0.06f, 1)));

    ImU32 gridCol = ImGui::GetColorU32(ImVec4(0.3f, 0.42f, 0.53f, 0.2f));
    ImU32 textCol = ImGui::GetColorU32(ImVec4(0.55f, 0.65f, 0.75f, 0.9f));
    for (int k = 1; k <= 5; k++) {
        float x = p.x + float(k * 1000.0 / maxHz) * w;
        dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + h), gridCol);
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%dk", k);
        dl->AddText(ImVec2(x + 2, p.y + h - 15), textCol, lbl);
    }

    ImU32 line = ImGui::GetColorU32(ImVec4(0.35f, 0.9f, 0.55f, 1.0f));
    float prevY = 0;
    for (int px = 0; px < int(w); px++) {
        size_t b = n / 2 + size_t(double(px) / double(w) * double(bins));
        if (b >= n) b = n - 1;
        float v = std::clamp(spec[b], dbLo, dbHi);
        float y = p.y + h - (v - dbLo) / (dbHi - dbLo) * h;
        if (px > 0)
            dl->AddLine(ImVec2(p.x + px - 1, prevY), ImVec2(p.x + px, y),
                        line, 1.2f);
        prevY = y;
    }

    // Notch attivo: riga rossa sulla frequenza scavata.
    if (app.notchHz >= 1.0f && app.notchHz < maxHz) {
        float x = p.x + float(app.notchHz / maxHz) * w;
        dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + h),
                    ImGui::GetColorU32(ImVec4(1.0f, 0.35f, 0.35f, 0.9f)),
                    1.5f);
    }

    // Aiuto di taratura (stile fldigi): dove il decoder si aspetta i toni.
    // CW = una riga sul tono; RTTY = due righe (mark/space) da allineare
    // ai due picchi del segnale ruotando la sintonia.
    auto marker = [&](double hz, ImU32 col, const char* lbl) {
        if (hz <= 0 || hz >= maxHz) return;
        float x = p.x + float(hz / maxHz) * w;
        dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + h), col, 1.5f);
        dl->AddText(ImVec2(x + 2, p.y + 2), col, lbl);
    };
    if (app.textDecoderMode == 1) {
        marker(double(app.cwToneHz),
               ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.95f)), "CW");
    } else if (app.textDecoderMode == 2) {
        double shift = kRttyShift[std::clamp(app.rttyShiftIdx, 0, 2)];
        ImU32 c = ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.95f));
        marker(double(app.rttyMarkHz), c, "M");
        marker(double(app.rttyMarkHz) + shift, c, "S");
    } else if (app.textDecoderMode == 3) {
        marker(double(app.psk31ToneHz),
               ImGui::GetColorU32(ImVec4(0.55f, 0.85f, 1.0f, 0.95f)), "PSK");
    }

    bool decActive = app.textDecoderMode != 0;
    if (hov) {
        float mxs = ImGui::GetIO().MousePos.x;
        double f = std::clamp(double(mxs - p.x) / double(w), 0.0, 1.0) *
                   maxHz;
        if (decActive)
            ImGui::SetTooltip("%.0f Hz - click: sintonizza il decoder, "
                              "click destro: notch", f);
        else
            ImGui::SetTooltip("%.0f Hz - click: notch qui", f);
        bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        bool right = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        if (left || right) {
            // Si aggancia al picco piu' forte entro +/-120 Hz dal click:
            // il fischio viene centrato anche con un click impreciso.
            double lo = std::max(0.0, f - 120.0), hi = f + 120.0;
            size_t b0 = n / 2 + size_t(lo / rate * double(n));
            size_t b1 = n / 2 + size_t(hi / rate * double(n));
            size_t bBest = b0;
            for (size_t b = b0; b <= b1 && b < n; b++)
                if (spec[b] > spec[bBest]) bBest = b;
            f = double(bBest - n / 2) * rate / double(n);
            if (left && decActive) {
                // Click sinistro con decoder attivo = sintonizza il tono.
                setDecoderTone(app, f);
            } else {
                // Altrimenti piazza il notch sul fischio.
                app.notchHz = float(f);
                std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
                app.filterL.configureNotch(48000.0, double(app.notchHz), 15.0);
                app.filterR.configureNotch(48000.0, double(app.notchHz), 15.0);
            }
        }
    }

    // --- Waterfall audio (fldigi-like): scorre verso il basso, aiuta a
    // vedere i toni CW/PSK e i due binari RTTY nel tempo. ---
    constexpr int kAwfCols = 128;
    constexpr int kAwfRows = 40;
    static std::vector<float> awf(kAwfCols * kAwfRows, -120.0f);
    static int awfHead = 0;
    // Nuova riga: massimo dB per colonna nel bucket di frequenza.
    for (int cx = 0; cx < kAwfCols; cx++) {
        size_t ba = n / 2 + size_t(double(cx) / kAwfCols * double(bins));
        size_t bb = n / 2 + size_t(double(cx + 1) / kAwfCols * double(bins));
        float mxdb = -120.0f;
        for (size_t b = ba; b <= bb && b < n; b++) mxdb = std::max(mxdb, spec[b]);
        awf[awfHead * kAwfCols + cx] = mxdb;
    }
    awfHead = (awfHead + 1) % kAwfRows;

    const float wfH = 70.0f;
    ImVec2 wp = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##audiowf", ImVec2(std::max(60.0f, w), wfH));
    bool hovWf = ImGui::IsItemHovered();
    float cellW = w / float(kAwfCols);
    float cellH = wfH / float(kAwfRows);
    for (int r = 0; r < kAwfRows; r++) {
        int row = (awfHead - 1 - r + kAwfRows) % kAwfRows; // 0 = riga piu' nuova in alto
        float yy = wp.y + r * cellH;
        for (int cx = 0; cx < kAwfCols; cx++) {
            float t = (awf[row * kAwfCols + cx] - dbLo) / (dbHi - dbLo);
            dl->AddRectFilled(ImVec2(wp.x + cx * cellW, yy),
                              ImVec2(wp.x + (cx + 1) * cellW + 1, yy + cellH + 1),
                              waterfallColor(t));
        }
    }
    // Righe dei toni del decoder anche sul waterfall (stesse posizioni).
    auto wfMarker = [&](double hz, ImU32 col) {
        if (hz <= 0 || hz >= maxHz) return;
        float x = wp.x + float(hz / maxHz) * w;
        dl->AddLine(ImVec2(x, wp.y), ImVec2(x, wp.y + wfH), col, 1.0f);
    };
    ImU32 mkCol = ImGui::GetColorU32(ImVec4(1.0f, 0.71f, 0.33f, 0.9f));
    if (app.textDecoderMode == 1) wfMarker(double(app.cwToneHz), mkCol);
    else if (app.textDecoderMode == 2) {
        double shift = kRttyShift[std::clamp(app.rttyShiftIdx, 0, 2)];
        wfMarker(double(app.rttyMarkHz), mkCol);
        wfMarker(double(app.rttyMarkHz) + shift, mkCol);
    } else if (app.textDecoderMode == 3)
        wfMarker(double(app.psk31ToneHz),
                 ImGui::GetColorU32(ImVec4(0.55f, 0.85f, 1.0f, 0.9f)));
    // Click sul waterfall = come sullo spettro (sintonizza o notch).
    if (hovWf && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                  ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        double f = std::clamp(double(ImGui::GetIO().MousePos.x - wp.x) /
                              double(w), 0.0, 1.0) * maxHz;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && decActive) {
            setDecoderTone(app, f);
        } else {
            app.notchHz = float(f);
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            app.filterL.configureNotch(48000.0, double(app.notchHz), 15.0);
            app.filterR.configureNotch(48000.0, double(app.notchHz), 15.0);
        }
    }

    if (ImGui::SmallButton("Notch spento")) {
        app.notchHz = 0.0f;
        std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
        app.filterL.configureNotch(48000.0, 0.0);
        app.filterR.configureNotch(48000.0, 0.0);
    }
    ImGui::SameLine();
    if (decActive)
        ImGui::TextDisabled("click = sintonizza, click destro = notch");
    else
        ImGui::TextDisabled("click sul grafico = notch sul fischio");
}

// Decoder di testi CW/RTTY sul canale di ascolto, stile fldigi: il
// testo decodificato scorre in un riquadro della sidebar.
void drawTextDecoderSection(AppState& app)
{
    if (app.listenMode == AppState::ListenMode::Off)
        ImGui::TextDisabled("suggerito: USB sul segnale da decodificare");

    static const char* kBaudNames[] = {"45.45 (ama)", "50", "75"};
    static const char* kShiftNames[] = {"170 (ama)", "425", "850"};

    auto textCb = [&app](char c) { emitDecoded(app, c); };

    int mode = app.textDecoderMode;
    bool ch = false;
    ch |= ImGui::RadioButton("Spento##dec", &mode, 0);
    ImGui::SameLine();
    ch |= ImGui::RadioButton("CW (Morse)", &mode, 1);
    ch |= ImGui::RadioButton("RTTY", &mode, 2);
    ImGui::SameLine();
    ch |= ImGui::RadioButton("PSK31", &mode, 3);
    if (ch) {
        std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
        app.textDecoderMode = mode;
        app.cwDecoder.reset();
        app.rttyDecoder.reset();
        app.psk31Decoder.reset();
        if (mode == 1) {
            app.cwTone.configure(double(app.cwToneHz), 48000.0);
            app.cwDecoder =
                std::make_unique<sdrjo::morse::CwDecoder>(48000.0, textCb);
            app.cwDecoder->setAutoSpeed(app.cwAutoSpeed);
            if (!app.cwAutoSpeed) app.cwDecoder->setWpm(double(app.cwWpm));
        } else if (mode == 2) {
            rebuildRttyDecoder(app);
        } else if (mode == 3) {
            app.psk31Decoder = std::make_unique<sdrjo::dsp::Psk31Decoder>(
                48000.0, textCb, double(app.psk31ToneHz));
        }
    }

    if (app.textDecoderMode == 1) {
        // --- Taratura CW (fldigi-style) ---
        fieldLabel("Tono (Hz)");
        if (ImGui::SliderFloat("##cwtono", &app.cwToneHz, 300.0f, 1200.0f,
                               "%.0f")) {
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            app.cwTone.configure(double(app.cwToneHz), 48000.0);
        }
        helpTip("Altezza del tono CW da decodificare: allinea la riga 'CW' "
                "sullo Spettro audio al fischio del segnale.");
        if (ImGui::Checkbox("Velocita' automatica", &app.cwAutoSpeed)) {
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            if (app.cwDecoder) {
                app.cwDecoder->setAutoSpeed(app.cwAutoSpeed);
                if (!app.cwAutoSpeed)
                    app.cwDecoder->setWpm(double(app.cwWpm));
            }
        }
        helpTip("Auto: aggancia da sola la velocita'. Manuale: fissa i WPM "
                "(meglio sui segnali deboli/disturbati).");
        if (!app.cwAutoSpeed) {
            fieldLabel("WPM (parole al minuto)");
            if (ImGui::SliderFloat("##cwwpm", &app.cwWpm, 5.0f, 40.0f, "%.0f")) {
                std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
                if (app.cwDecoder) app.cwDecoder->setWpm(double(app.cwWpm));
            }
        }
        if (app.cwDecoder)
            ImGui::TextDisabled("velocita' stimata: %.0f WPM",
                                app.cwDecoder->wpm());
    } else if (app.textDecoderMode == 2) {
        // --- Taratura RTTY (fldigi-style): baud, shift, mark, reverse ---
        bool rc = false;
        fieldLabel("Velocita' (baud)");
        rc |= ImGui::Combo("##rbaud", &app.rttyBaudIdx, kBaudNames, 3);
        fieldLabel("Shift (Hz)");
        rc |= ImGui::Combo("##rshift", &app.rttyShiftIdx, kShiftNames, 3);
        fieldLabel("Mark (Hz)");
        if (ImGui::SliderFloat("##rmark", &app.rttyMarkHz, 800.0f, 2500.0f,
                               "%.0f"))
            rc = true;
        helpTip("Allinea le righe 'M' (mark) e 'S' (space) sullo Spettro "
                "audio ai due picchi del segnale ruotando la sintonia.");
        bool rev = app.rttyReverse;
        if (ImGui::Checkbox("Inverti mark/space", &rev)) {
            app.rttyReverse = rev;
            rc = true;
        }
        if (rc) {
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            rebuildRttyDecoder(app);
        }
    } else if (app.textDecoderMode == 3) {
        // --- Taratura PSK31: solo il tono audio del segnale ---
        fieldLabel("Tono (Hz)");
        if (ImGui::SliderFloat("##psktono", &app.psk31ToneHz, 300.0f, 2500.0f,
                               "%.0f")) {
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            if (app.psk31Decoder)
                app.psk31Decoder->setTone(double(app.psk31ToneHz));
        }
        helpTip("Porta la riga 'PSK' dello Spettro audio sul segnale BPSK31 "
                "(sembra due righe vicine che pulsano). 31.25 baud.");
    }

    // AFC: aggancio automatico del tono (solo CW/PSK, dove ha senso).
    if (app.textDecoderMode == 1 || app.textDecoderMode == 3) {
        ImGui::Checkbox("AFC (centra il tono)", &app.decoderAfc);
        helpTip("Sposta da solo il tono sul picco piu' vicino dell'audio, "
                "come l'aggancio automatico di fldigi.");
    }

    // --- Squelch dei decoder + indicatore di taratura ---
    if (app.textDecoderMode != 0) {
        fieldLabel("Squelch decoder");
        ImGui::SliderFloat("##decsq", &app.decoderSquelch, 0.0f, 0.6f, "%.2f");
        helpTip("Sotto questa soglia d'ampiezza il decoder si ferma: alza il "
                "valore se compare testo casuale sul rumore.");
        float lvl = app.decoderLevel.load(std::memory_order_relaxed);
        float frac = std::clamp(lvl / 0.6f, 0.0f, 1.0f);
        bool open = lvl >= app.decoderSquelch;
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram,
                              open ? ImVec4(0.36f, 0.82f, 0.45f, 1.0f)
                                   : ImVec4(0.55f, 0.55f, 0.55f, 1.0f));
        ImGui::ProgressBar(frac, ImVec2(-FLT_MIN, 12.0f),
                           open ? "segnale" : "silenzio");
        ImGui::PopStyleColor();

        // Indicatore di taratura specifico del modo.
        std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
        if (app.textDecoderMode == 2 && app.rttyDecoder) {
            float m = app.rttyDecoder->markLevel();
            float s = app.rttyDecoder->spaceLevel();
            float mx = std::max({m, s, 1e-4f});
            ImGui::TextDisabled("Mark/Space (allinea i due picchi):");
            ImGui::ProgressBar(m / mx, ImVec2(-FLT_MIN, 10.0f), "M");
            ImGui::ProgressBar(s / mx, ImVec2(-FLT_MIN, 10.0f), "S");
        } else if (app.textDecoderMode == 3 && app.psk31Decoder) {
            // LED d'aggancio + piccola costellazione (2 lobi = agganciato).
            float lk = app.psk31Decoder->lock();
            ImU32 led = lk > 0.5f ? IM_COL32(60, 200, 90, 255)
                      : lk > 0.25f ? IM_COL32(220, 180, 60, 255)
                                   : IM_COL32(120, 120, 120, 255);
            ImVec2 lp = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddCircleFilled(
                ImVec2(lp.x + 7, lp.y + 8), 5.0f, led);
            ImGui::Dummy(ImVec2(16, 16));
            ImGui::SameLine();
            ImGui::TextDisabled(lk > 0.5f ? "agganciato" : "cerco aggancio...");
            ImVec2 o = ImGui::GetCursorScreenPos();
            float side = 90.0f, r = side * 0.5f;
            ImVec2 c(o.x + r, o.y + r);
            ImDrawList* d = ImGui::GetWindowDrawList();
            d->AddRectFilled(o, ImVec2(o.x + side, o.y + side),
                             ImGui::GetColorU32(ImVec4(0.08f, 0.09f, 0.11f, 1)));
            d->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y),
                       ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1)));
            d->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r),
                       ImGui::GetColorU32(ImVec4(0.3f, 0.3f, 0.3f, 1)));
            sdrjo::cfloat sy = app.psk31Decoder->lastSymbol();
            float mag = std::sqrt(sy.real() * sy.real() + sy.imag() * sy.imag());
            float k = mag > 1e-6f ? (r * 0.8f / mag) : 0.0f;
            ImVec2 pt(c.x + sy.real() * k, c.y - sy.imag() * k);
            d->AddCircleFilled(pt, 3.0f,
                               ImGui::GetColorU32(ImVec4(0.55f, 0.85f, 1, 1)));
            ImGui::Dummy(ImVec2(side, side));
        }
    }

    // Riquadro del testo decodificato (autoscroll).
    if (ImGui::BeginChild("##dectext", ImVec2(0, 140),
                          ImGuiChildFlags_Borders)) {
        std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(app.decodedText.c_str());
        ImGui::PopTextWrapPos();
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
    if (ImGui::SmallButton("Cancella##dec")) {
        std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
        app.decodedText.clear();
    }
}

// ---- Scanner delle memorie: salta di frequenza in frequenza e si ferma
// dove lo squelch apre; opzionalmente registra il segnale su WAV. ----
void scannerHop(AppState& app)
{
    auto& items = app.freqStore.items();
    if (items.empty()) return;
    app.scanIndex = (app.scanIndex + 1) % int(items.size());
    const auto& ff = items[size_t(app.scanIndex)];
    for (int m = 1; m < 6; m++) {
        if (ff.mode == kListenModeNames[m] &&
            app.listenMode != AppState::ListenMode(m)) {
            app.listenMode = AppState::ListenMode(m);
            rebuildListener(app);
        }
    }
    if (ff.bandwidthHz > 0) {
        app.listenBwHz = ff.bandwidthHz;
        rebuildChanFilter(app);
    }
    applyTunedFrequency(app, ff.freqHz);
    app.scanLastHop = std::chrono::steady_clock::now();
}

void updateScanner(AppState& app)
{
    using namespace std::chrono;
    if (!app.scanOn) {
        if (app.scanWav.isOpen()) {
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            app.scanWav.stop();
        }
        app.scanPaused = false;
        app.scanIndex = -1;
        return;
    }
    if (app.freqStore.items().empty() ||
        app.listenMode == AppState::ListenMode::Off || !app.squelchOn)
        return;

    auto now = steady_clock::now();
    if (app.scanPaused) {
        // Fermo su un segnale: si riparte quando lo squelch resta chiuso
        // abbastanza a lungo.
        if (app.squelchOpen) {
            app.scanSigLost = now;
        } else if (now - app.scanSigLost >
                   milliseconds(int(app.scanResumeSec * 1000.0f))) {
            if (app.scanWav.isOpen()) {
                std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
                app.scanWav.stop();
            }
            app.scanPaused = false;
            scannerHop(app);
        }
        return;
    }

    // Dopo il salto il livello del canale deve assestarsi prima di
    // fidarsi dello squelch.
    bool settled = now - app.scanLastHop >
                   milliseconds(std::max(150, app.scanDwellMs / 3));
    if (app.scanIndex >= 0 && settled && app.squelchOpen) {
        app.scanPaused = true;
        app.scanSigLost = now;
        if (app.scanRecord) {
            const auto& ff = app.freqStore.items()[size_t(app.scanIndex)];
            char name[96];
            std::snprintf(name, sizeof(name), "scan_%.4fMHz.wav",
                          ff.freqHz / 1e6);
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            app.scanWav.start(name, 48000, 1);
        }
        return;
    }
    if (app.scanIndex < 0 ||
        now - app.scanLastHop >= milliseconds(app.scanDwellMs))
        scannerHop(app);
}

void drawScannerSection(AppState& app)
{
    auto& items = app.freqStore.items();
    bool ready = !items.empty() &&
                 app.listenMode != AppState::ListenMode::Off &&
                 app.squelchOn;
    if (!ready)
        ImGui::TextWrapped("Servono: memorie salvate in Frequenze, un "
                           "demodulatore acceso e lo squelch attivo (lo "
                           "scanner si ferma dove lo squelch apre). "
                           "Avvia/ferma con l'interruttore sul titolo.");
    ImGui::SliderInt("Attesa (ms)", &app.scanDwellMs, 200, 3000);
    ImGui::SliderFloat("Riprendi dopo (s)", &app.scanResumeSec, 0.5f, 10.0f,
                       "%.1f");
    ImGui::Checkbox("Registra su WAV il segnale trovato", &app.scanRecord);
    if (app.scanOn && app.scanIndex >= 0 &&
        app.scanIndex < int(items.size())) {
        const auto& ff = items[size_t(app.scanIndex)];
        ImGui::Text("%s %s (%.4f MHz)",
                    app.scanPaused ? "FERMO su" : "ascolto",
                    ff.name.c_str(), ff.freqHz / 1e6);
    }
    if (app.scanWav.isOpen())
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "REC %s (%.1f s)", app.scanWav.path().c_str(),
                           app.scanWav.secondsWritten());
}

// ---- Satelliti: passaggi dai TLE e inseguimento Doppler. ----
void updateDoppler(AppState& app)
{
    using namespace std::chrono;
    if (!app.dopplerOn || app.tles.empty() ||
        app.satSel >= int(app.tles.size()))
        return;
    auto now = steady_clock::now();
    if (now - app.lastDopplerTune < seconds(1)) return;
    app.lastDopplerTune = now;

    sdrjo::sat::OrbitPropagator orb(app.tles[size_t(app.satSel)]);
    double t = double(std::time(nullptr));
    auto ae = orb.observe(t, app.stationLat, app.stationLon);
    if (ae.elDeg <= 0.0) {
        app.dopplerCurrentHz = 0.0;
        return; // sotto l'orizzonte: non toccare la sintonia
    }
    app.dopplerCurrentHz =
        orb.dopplerHz(t, app.stationLat, app.stationLon,
                      app.dopplerBaseMHz * 1e6);
    applyTunedFrequency(app, app.dopplerBaseMHz * 1e6 +
                                 app.dopplerCurrentHz);
}

void drawSatellitesSection(AppState& app)
{
    if (ImGui::SmallButton("Ricarica TLE")) {
        app.tles = sdrjo::sat::loadTleFile(app.tlePath);
        app.satPasses.clear();
        app.log("Satelliti",
                std::to_string(app.tles.size()) + " TLE caricati da " +
                    app.tlePath);
    }
    ImGui::SameLine();
    ImGui::Text("%zu satelliti", app.tles.size());
    if (app.tles.empty()) {
        ImGui::TextWrapped(
            "Scarica gli elementi orbitali (celestrak.org, gruppo "
            "'weather' per NOAA/Meteor) e salvali come tle.txt accanto "
            "all'eseguibile. Rinnovali ogni pochi giorni.");
        return;
    }

    if (app.satSel >= int(app.tles.size())) app.satSel = 0;
    const auto& tle = app.tles[size_t(app.satSel)];
    if (ImGui::BeginCombo("Satellite", tle.name.c_str())) {
        for (int i = 0; i < int(app.tles.size()); i++) {
            if (ImGui::Selectable(app.tles[size_t(i)].name.c_str(),
                                  app.satSel == i)) {
                app.satSel = i;
                app.satPasses.clear();
            }
        }
        ImGui::EndCombo();
    }
    double ageDays = (double(std::time(nullptr)) - tle.epochUnix) / 86400.0;
    ImGui::TextDisabled("elementi di %.1f giorni fa%s", ageDays,
                        ageDays > 7.0 ? " (vecchi: meglio rinnovarli)" : "");

    if (app.stationLat == 0.0 && app.stationLon == 0.0) {
        ImGui::TextWrapped("Imposta (o rileva) la posizione dell'antenna "
                           "nella sezione Dispositivo.");
        return;
    }

    if (ImGui::Button("Calcola passaggi (24h)")) {
        sdrjo::sat::OrbitPropagator orb(tle);
        app.satPasses = orb.findPasses(double(std::time(nullptr)), 24.0,
                                       app.stationLat, app.stationLon, 5.0);
    }
    if (!app.satPasses.empty() &&
        ImGui::BeginTable("passi", 4, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Inizio");
        ImGui::TableSetupColumn("Durata");
        ImGui::TableSetupColumn("El.max");
        ImGui::TableSetupColumn("Az.");
        ImGui::TableHeadersRow();
        for (const auto& p : app.satPasses) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            time_t aos = time_t(p.aosUnix);
            struct tm tmv;
#if defined(_WIN32)
            localtime_s(&tmv, &aos);
#else
            localtime_r(&aos, &tmv);
#endif
            char when[32];
            std::strftime(when, sizeof(when), "%d/%m %H:%M", &tmv);
            ImGui::TextUnformatted(when);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f min", (p.losUnix - p.aosUnix) / 60.0);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f\xc2\xb0", p.maxElDeg);
            ImGui::TableNextColumn();
            ImGui::Text("%.0f>%.0f", p.aosAzDeg, p.losAzDeg);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Doppler");
    ImGui::SetNextItemWidth(140);
    ImGui::InputDouble("Downlink (MHz)", &app.dopplerBaseMHz, 0, 0, "%.4f");
    ImGui::Checkbox("Insegui Doppler (sintonia automatica)", &app.dopplerOn);
    if (app.dopplerOn) {
        sdrjo::sat::OrbitPropagator orb(tle);
        auto ae = orb.observe(double(std::time(nullptr)), app.stationLat,
                              app.stationLon);
        if (ae.elDeg > 0.0)
            ImGui::Text("el %.0f  az %.0f  offset %+.0f Hz", ae.elDeg,
                        ae.azDeg, app.dopplerCurrentHz);
        else
            ImGui::TextDisabled("sotto l'orizzonte: aspetto il passaggio");
    }
}

} // namespace

int main(int argc, char** argv)
{
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
    // Avvio massimizzato: piu' spazio per spettro/waterfall (la finestra
    // resta ridimensionabile e il layout segue).
    glfwWindowHint(GLFW_MAXIMIZED, GLFW_TRUE);
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
    // Moduli spenti all'avvio: l'app parte leggera, l'utente accende solo
    // il decoder che gli serve (ognuno costa CPU quando riceve).
    app.moduleEnabled.assign(app.modules.size(), 0);

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

    // Elementi orbitali per la sezione Satelliti (se il file esiste).
    app.tlePath =
        (std::filesystem::path(sdrjo::ModuleLoader::defaultModulesDir())
             .parent_path() /
         "tle.txt")
            .string();
    app.tles = sdrjo::sat::loadTleFile(app.tlePath);
    if (!app.tles.empty())
        app.log("Satelliti",
                std::to_string(app.tles.size()) + " TLE caricati");

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
    app.cockpit.setRateHandler([&app](double rateHz) {
        std::lock_guard<std::mutex> lk(app.remoteMutex);
        app.pendingRateHz = rateHz;
        return true;
    });
    // Streaming audio a bassa latenza (WebSocket + ADPCM) accanto al
    // Cockpit: token nuovo a ogni avvio, consegnato da /api/wsinfo.
    {
        std::string tok = randomToken();
        app.wsAudio.setToken(tok);
        if (app.wsAudio.start(sdrjo::WsAudioServer::kDefaultPort, false))
            app.cockpit.setWsInfo(app.wsAudio.port(), tok);
    }
    if (app.cockpit.start())
        app.log("Cockpit", "http://localhost:" +
                               std::to_string(app.cockpit.port()));

    // Thread DSP: tutto il calcolo fuori dal thread di rendering.
    app.dspRunning.store(true);
    app.dspThread = std::thread(dspLoop, std::ref(app));

    // Avvio con replay da riga di comando: sdrjo <file.bin> [rate_MSps] [MHz]
    if (argc >= 2) {
        double rate = (argc >= 3) ? std::atof(argv[2]) * 1e6 : app.sampleRate;
        if (argc >= 4) app.freqMHz = std::atof(argv[3]);
        app.sampleRate = rate;
        rebuildChannels(app);
        auto src = std::make_unique<sdrjo::FileSource>(argv[1], rate);
        src->setLoop(true);
        src->setCenterFrequency(app.freqMHz * 1e6);
        src->start([&app](const sdrjo::cfloat* s, size_t n) {
            app.iqRing.write(s, n);
        });
        app.source = std::move(src);
        app.log("Replay", std::string(argv[1]));
    }

    // Ripristina il modo salvato (o AM di default) e costruisci la catena
    // d'ascolto. rebuildListener rimette la banda di default del modo,
    // quindi ri-applichiamo dopo la larghezza salvata nella sessione.
    {
        double savedBw = app.listenBwHz;
        rebuildListener(app);
        if (savedBw > 0 && app.listenMode != AppState::ListenMode::Off &&
            app.listenMode != AppState::ListenMode::WfmStereo) {
            app.listenBwHz = savedBw;
            rebuildChanFilter(app);
        }
    }

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        if (app.channelsDirty) {
            rebuildChannels(app);
            app.channelsDirty = false;
        }

        // Scanner delle memorie e inseguimento Doppler dei satelliti.
        updateScanner(app);
        updateDoppler(app);

        // Applica i comandi arrivati dal Cockpit web.
        {
            double tuneHz = -1.0, rateHz = -1.0;
            std::string mode;
            {
                std::lock_guard<std::mutex> lk(app.remoteMutex);
                tuneHz = app.pendingTuneHz;
                app.pendingTuneHz = -1.0;
                rateHz = app.pendingRateHz;
                app.pendingRateHz = -1.0;
                mode.swap(app.pendingMode);
            }
            if (rateHz > 0 && app.source &&
                std::fabs(rateHz - app.sampleRate) > 1.0) {
                std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
                app.source->setSampleRate(rateHz);
                app.sampleRate = app.source->sampleRate();
                app.channelsDirty = true;
                rebuildListener(app);
                resetSpectrumAfterRetune(app);
            }
            if (!mode.empty()) {
                for (int m = 0; m < 6; m++)
                    if (mode == kListenModeNames[m])
                        app.listenMode = AppState::ListenMode(m);
                rebuildListener(app);
            }
            if (tuneHz > 0) applyTunedFrequency(app, tuneHz);
        }

        // Retune hardware accodato dal limitatore del drag.
        {
            std::lock_guard<std::recursive_mutex> lk(app.dspMutex);
            if (app.pendingHwTuneHz > 0 && app.source &&
                std::chrono::steady_clock::now() - app.lastHwTune >
                    std::chrono::milliseconds(50)) {
                app.lastHwTune = std::chrono::steady_clock::now();
                app.source->setCenterFrequency(app.pendingHwTuneHz);
                app.pendingHwTuneHz = -1.0;
                resetSpectrumAfterRetune(app);
            }
        }

        // AFC dei decoder: aggancio del tono ~5 volte al secondo (leggero).
        {
            static auto lastAfc = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (app.decoderAfc &&
                now - lastAfc > std::chrono::milliseconds(200)) {
                lastAfc = now;
                stepDecoderAfc(app);
            }
        }

        uploadWaterfallTexture(app);
        app.cockpit.setDeviceInfo(
            app.source ? app.source->name() : "nessuna sorgente",
            app.freqMHz * 1e6, app.sampleRate);
        app.cockpit.setVfoInfo(app.freqMHz * 1e6 + app.listenOffsetHz,
                               kListenModeNames[int(app.listenMode)]);

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Scala dei font dell'interfaccia (per monitor ad alta densita').
        ImGui::GetIO().FontGlobalScale = std::clamp(app.uiScale, 0.7f, 2.0f);

        drawSidebar(app);
        drawSpectrumPanel(app);
        drawRightStrip(app);
        drawStatusBar(app);

        // Scorciatoie da tastiera (solo se non si sta scrivendo in un
        // campo): barra spazio = mute, frecce su/giu = sintonia a passi
        // di snap, M = ciclo modo, F = 1x zoom.
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Space, false))
                app.muted = !app.muted;
            double cur = app.freqMHz * 1e6 + app.listenOffsetHz;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
                applyTunedFrequency(app, std::round((cur + app.snapHz) /
                                                    app.snapHz) * app.snapHz);
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
                applyTunedFrequency(app, std::round((cur - app.snapHz) /
                                                    app.snapHz) * app.snapHz);
            if (ImGui::IsKeyPressed(ImGuiKey_M, false)) {
                int m = (int(app.listenMode) % 5) + 1; // cicla 1..5
                app.listenMode = AppState::ListenMode(m);
                rebuildListener(app);
            }
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                app.viewSpanFrac = 1.0;
                app.viewCenterFrac = 0.5;
            }
        }

        ImGui::Render();
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.06f, 0.06f, 0.08f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window);
    }

    // Ricorda l'ultima sessione (freq/modo/banda/volume/snap + posizione).
    saveConfig(app);

    app.dspRunning.store(false);
    if (app.dspThread.joinable()) app.dspThread.join();
    if (app.geoThread.joinable()) app.geoThread.join();
    app.scanWav.stop();
    app.wsAudio.stop();
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
