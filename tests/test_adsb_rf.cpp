//
// Test end-to-end del rilevatore ADS-B: sintetizza il segnale PPM di un
// frame noto (a 2.0 MS/s) e verifica che venga rilevato e decodificato.
//
#include <sdrjo/adsb/preamble_detector.hpp>
#include <sdrjo/adsb/mode_s.hpp>
#include "test_util.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

using namespace sdrjo::adsb;

// Costruisce il vettore delle ampiezze per un frame Mode S (2 campioni/us).
static void appendFrame(std::vector<float>& mag, const std::string& hex,
                        float high, float noise)
{
    auto rnd = [&]() { return noise * float(std::rand()) / float(RAND_MAX); };

    // Preambolo: impulsi a 0, 1.0, 3.5, 4.5 us.
    const int pulseSamples[] = {0, 2, 7, 9};
    size_t base = mag.size();
    for (int i = 0; i < 16; i++) mag.push_back(rnd());
    for (int p : pulseSamples) mag[base + p] = high + rnd();

    // Bit PPM: 1 = alto/basso, 0 = basso/alto (1 us per bit).
    for (char c : hex) {
        int nib = (c <= '9') ? c - '0' : (c - 'A' + 10);
        for (int b = 3; b >= 0; b--) {
            bool bit = (nib >> b) & 1;
            mag.push_back(bit ? high + rnd() : rnd());
            mag.push_back(bit ? rnd() : high + rnd());
        }
    }
}

int main()
{
    std::srand(7);
    const std::string frameHex = "8D4840D6202CC371C32CE0576098";

    std::vector<float> mag;
    for (int i = 0; i < 300; i++) mag.push_back(0.02f * float(std::rand()) / float(RAND_MAX));
    appendFrame(mag, frameHex, 0.8f, 0.05f);
    for (int i = 0; i < 500; i++) mag.push_back(0.02f * float(std::rand()) / float(RAND_MAX));
    appendFrame(mag, frameHex, 0.6f, 0.05f); // secondo frame, piu' debole
    for (int i = 0; i < 300; i++) mag.push_back(0.02f * float(std::rand()) / float(RAND_MAX));

    int found = 0;
    PreambleDetector det([&](const uint8_t* frame, size_t len) {
        ModeSMessage msg;
        if (decode(frame, len, msg) && msg.icao == 0x4840D6 &&
            msg.callsign == "KLM1023")
            found++;
    });

    // Elabora in chunk piccoli per esercitare la gestione della coda.
    for (size_t off = 0; off < mag.size(); off += 128) {
        size_t n = std::min<size_t>(128, mag.size() - off);
        det.processMagnitude(mag.data() + off, n);
    }

    CHECK(found == 2);
    return testResult("test_adsb_rf");
}
