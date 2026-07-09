//
// sdrjo-cli: strumento a riga di comando per provare i decoder senza GUI.
//
//   sdrjo-cli adsb-hex <frame-esadecimale> [...]
//       Decodifica frame Mode S dati in esadecimale.
//
//   sdrjo-cli adsb-iq <file.bin>
//       Decodifica un file IQ u8 registrato a 2.0 MS/s su 1090 MHz
//       (es. "rtl_sdr -f 1090000000 -s 2000000 cattura.bin").
//
//   sdrjo-cli morse-iq <file.bin> <sample_rate>
//       Decodifica CW da un file IQ u8 centrato sul tono.
//
//   sdrjo-cli adsb-serve <file.bin|-> [lat lon] [porta]
//       Mappa web dei voli (stile SDRAngel): decodifica il file IQ (o lo
//       stdin, per l'uso in diretta con "rtl_sdr ... - |") e serve la mappa
//       su http://localhost:8757. lat/lon = posizione dell'antenna.
//
#include <sdrjo/adsb/mode_s.hpp>
#include <sdrjo/adsb/preamble_detector.hpp>
#include <sdrjo/adsb/aircraft_tracker.hpp>
#include <sdrjo/adsb/adsb_server.hpp>
#include <sdrjo/morse/cw_decoder.hpp>
#include <sdrjo/dsp/types.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
  #include <fcntl.h>
  #include <io.h>
#endif

using namespace sdrjo;

static void printMessage(const adsb::ModeSMessage& msg)
{
    std::printf("DF%-2d ICAO %06X", msg.df, msg.icao);
    if (msg.typeCode >= 0) std::printf(" TC%-2d", msg.typeCode);
    if (msg.hasCallsign) std::printf("  volo %s", msg.callsign.c_str());
    if (msg.hasAltitude) std::printf("  alt %d ft", msg.altitudeFt);
    if (msg.hasVelocity)
        std::printf("  gs %.0f kt  rotta %.0f  vario %+d ft/min",
                    msg.groundSpeedKt, msg.trackDeg, msg.verticalRateFpm);
    if (msg.hasCprPosition)
        std::printf("  CPR-%s lat=%.5f lon=%.5f", msg.cprOdd ? "odd" : "even",
                    msg.cprLat, msg.cprLon);
    std::printf("\n");
}

static int cmdAdsbHex(int argc, char** argv)
{
    if (argc < 1) { std::fprintf(stderr, "serve almeno un frame esadecimale\n"); return 1; }
    int failures = 0;
    for (int i = 0; i < argc; i++) {
        adsb::ModeSMessage msg;
        if (adsb::decodeHex(argv[i], msg)) {
            printMessage(msg);
        } else {
            std::printf("%s: frame non valido o CRC errato\n", argv[i]);
            failures++;
        }
    }
    return failures ? 1 : 0;
}

static std::vector<cfloat> loadIqFile(const char* path)
{
    std::vector<cfloat> iq;
    FILE* f = std::fopen(path, "rb");
    if (!f) return iq;
    std::vector<uint8_t> raw(1 << 20);
    size_t rd;
    while ((rd = std::fread(raw.data(), 1, raw.size(), f)) > 0) {
        size_t n = rd / 2;
        size_t base = iq.size();
        iq.resize(base + n);
        convertU8Iq(raw.data(), n, iq.data() + base);
    }
    std::fclose(f);
    return iq;
}

static int cmdAdsbIq(const char* path)
{
    auto iq = loadIqFile(path);
    if (iq.empty()) { std::fprintf(stderr, "impossibile leggere %s\n", path); return 1; }
    std::printf("caricati %zu campioni (%.1f s a 2.0 MS/s)\n",
                iq.size(), double(iq.size()) / 2.0e6);

    adsb::AircraftTracker tracker;
    size_t frames = 0;
    adsb::PreambleDetector det([&](const uint8_t* frame, size_t len) {
        adsb::ModeSMessage msg;
        if (adsb::decode(frame, len, msg)) {
            frames++;
            printMessage(msg);
            tracker.update(msg);
        }
    });
    det.processIq(iq.data(), iq.size());

    std::printf("\n%zu frame validi. Aerei visti:\n", frames);
    for (const auto& ac : tracker.activeAircraft(1e9)) {
        std::printf("  %06X %-8s", ac.icao, ac.callsign.c_str());
        if (ac.hasPosition) std::printf("  %.4f %.4f", ac.latDeg, ac.lonDeg);
        if (ac.hasAltitude) std::printf("  %d ft", ac.altitudeFt);
        std::printf("  (%u msg)\n", ac.messageCount);
    }
    return 0;
}

static int cmdMorseIq(const char* path, double sampleRate)
{
    auto iq = loadIqFile(path);
    if (iq.empty()) { std::fprintf(stderr, "impossibile leggere %s\n", path); return 1; }

    morse::CwDecoder dec(sampleRate);
    std::vector<float> mag(iq.size());
    for (size_t i = 0; i < iq.size(); i++) mag[i] = std::abs(iq[i]);
    dec.processAudio(mag.data(), mag.size());
    dec.flush();

    std::printf("testo decodificato (%.0f WPM): %s\n", dec.wpm(), dec.text().c_str());
    return 0;
}

static int cmdAdsbServe(int argc, char** argv)
{
    const char* path = argv[0];
    double lat = 0, lon = 0;
    bool haveQth = false;
    uint16_t port = adsb::AdsbWebServer::kDefaultPort;
    if (argc >= 3) {
        lat = std::atof(argv[1]);
        lon = std::atof(argv[2]);
        haveQth = true;
    }
    if (argc >= 4) port = uint16_t(std::atoi(argv[3]));

    FILE* f;
    bool fromStdin = (std::strcmp(path, "-") == 0);
    if (fromStdin) {
#if defined(_WIN32)
        _setmode(_fileno(stdin), _O_BINARY);
#endif
        f = stdin;
    } else {
        f = std::fopen(path, "rb");
        if (!f) { std::fprintf(stderr, "impossibile leggere %s\n", path); return 1; }
    }

    adsb::AircraftTracker tracker;
    std::mutex mutex;
    adsb::AdsbWebServer web(tracker, mutex);
    if (haveQth) web.setAntennaPosition(lat, lon);
    if (!web.start(port)) {
        std::fprintf(stderr, "porta %u occupata\n", port);
        return 1;
    }
    std::printf("mappa voli: http://localhost:%u  (Ctrl+C per uscire)\n",
                web.port());

    size_t frames = 0;
    adsb::PreambleDetector det([&](const uint8_t* frame, size_t len) {
        adsb::ModeSMessage msg;
        if (adsb::decode(frame, len, msg)) {
            std::lock_guard<std::mutex> lk(mutex);
            tracker.update(msg);
            frames++;
        }
    });

    // 1/8 di secondo di campioni a 2.0 MS/s per iterazione.
    constexpr size_t kChunk = 250000;
    std::vector<uint8_t> raw(kChunk * 2);
    std::vector<sdrjo::cfloat> iq(kChunk);
    auto next = std::chrono::steady_clock::now();
    auto lastReport = next;

    size_t rd;
    while ((rd = std::fread(raw.data(), 2, kChunk, f)) > 0) {
        sdrjo::convertU8Iq(raw.data(), rd, iq.data());
        det.processIq(iq.data(), rd);

        if (!fromStdin) { // replay in tempo reale
            next += std::chrono::nanoseconds(int64_t(1e9 * double(rd) / 2.0e6));
            std::this_thread::sleep_until(next);
        }
        auto now = std::chrono::steady_clock::now();
        if (now - lastReport > std::chrono::seconds(5)) {
            lastReport = now;
            std::lock_guard<std::mutex> lk(mutex);
            std::printf("frame validi: %zu, aerei attivi: %zu\n", frames,
                        tracker.activeAircraft(60.0).size());
        }
    }
    if (f != stdin) std::fclose(f);

    std::printf("flusso terminato (%zu frame). Server ancora attivo su "
                "http://localhost:%u — Ctrl+C per uscire.\n",
                frames, web.port());
    while (true) std::this_thread::sleep_for(std::chrono::seconds(3600));
    return 0;
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::fprintf(stderr,
            "uso:\n"
            "  %s adsb-hex <frame> [...]\n"
            "  %s adsb-iq <file.bin>\n"
            "  %s adsb-serve <file.bin|-> [lat lon] [porta]\n"
            "  %s morse-iq <file.bin> <sample_rate>\n",
            argv[0], argv[0], argv[0], argv[0]);
        return 1;
    }

    std::string cmd = argv[1];
    if (cmd == "adsb-hex") return cmdAdsbHex(argc - 2, argv + 2);
    if (cmd == "adsb-iq" && argc >= 3) return cmdAdsbIq(argv[2]);
    if (cmd == "adsb-serve" && argc >= 3) return cmdAdsbServe(argc - 2, argv + 2);
    if (cmd == "morse-iq" && argc >= 4) return cmdMorseIq(argv[2], std::atof(argv[3]));

    std::fprintf(stderr, "comando sconosciuto: %s\n", cmd.c_str());
    return 1;
}
