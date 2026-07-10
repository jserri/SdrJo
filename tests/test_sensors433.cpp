//
// Test del modulo sensori 433 MHz: decoder Nexus dai tempi degli
// impulsi e catena completa OOK da IQ sintetico.
//
#include <sdrjo/sensors/nexus_decoder.hpp>
#include <sdrjo/sensors/ook_detector.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

using namespace sdrjo::sensors;

// Trama Nexus a 36 bit dai campi.
static uint64_t nexusFrame(uint8_t id, bool batt, int channel, double tempC,
                           int hum)
{
    int t = int(std::lround(tempC * 10.0));
    if (t < 0) t += 0x1000;
    uint64_t b = 0;
    b |= uint64_t(id) << 28;
    if (batt) b |= uint64_t(1) << 27;
    b |= uint64_t((channel - 1) & 3) << 24;
    b |= uint64_t(t & 0xFFF) << 12;
    b |= uint64_t(0xF) << 8;
    b |= uint64_t(hum & 0xFF);
    return b;
}

// Impulsi PPM per una trama (bit MSB per primo): ogni bit e' un impulso
// da 500 us seguito dalla pausa che ne codifica il valore.
static std::vector<Pulse> framePulses(uint64_t frame)
{
    std::vector<Pulse> out;
    for (int i = 35; i >= 0; i--) {
        bool bit = (frame >> i) & 1;
        out.push_back({500.0, bit ? 2000.0 : 1000.0});
    }
    return out;
}

int main()
{
    // --- Decoder puro dagli impulsi ---
    {
        std::vector<NexusReading> got;
        NexusDecoder dec([&](const NexusReading& r) { got.push_back(r); });
        for (const auto& p : framePulses(nexusFrame(0xA7, true, 2, 21.7, 45)))
            dec.onPulse(p);
        CHECK(got.size() == 1);
        if (!got.empty()) {
            CHECK(got[0].id == 0xA7);
            CHECK(got[0].batteryOk);
            CHECK(got[0].channel == 2);
            CHECK(std::fabs(got[0].tempC - 21.7) < 1e-9);
            CHECK(got[0].humidity == 45);
        }
    }

    // Temperatura negativa (complemento a due).
    {
        std::vector<NexusReading> got;
        NexusDecoder dec([&](const NexusReading& r) { got.push_back(r); });
        for (const auto& p :
             framePulses(nexusFrame(0x12, false, 1, -8.3, 78)))
            dec.onPulse(p);
        CHECK(got.size() == 1);
        if (!got.empty()) {
            CHECK(std::fabs(got[0].tempC + 8.3) < 1e-9);
            CHECK(!got[0].batteryOk);
            CHECK(got[0].humidity == 78);
        }
    }

    // Impulsi spuri: niente letture fantasma.
    {
        std::vector<NexusReading> got;
        NexusDecoder dec([&](const NexusReading& r) { got.push_back(r); });
        for (int i = 0; i < 100; i++)
            dec.onPulse({250.0 + (i % 7) * 123.0, 600.0 + (i % 5) * 700.0});
        CHECK(got.empty());
    }

    // --- Catena completa: IQ OOK sintetico -> pulses -> lettura ---
    {
        const double rate = OokDetector::kSampleRateHz;
        std::vector<sdrjo::cfloat> iq;
        auto addSamples = [&](double us, float amp) {
            size_t n = size_t(us * rate / 1e6);
            for (size_t i = 0; i < n; i++) {
                // Portante con piccolo offset + rumore di fondo.
                double ph = 2 * M_PI * 5000.0 * double(iq.size()) / rate;
                float noise = 0.01f * float(std::sin(12.9898 * double(iq.size())));
                iq.push_back(sdrjo::cfloat(
                    amp * float(std::cos(ph)) + noise,
                    amp * float(std::sin(ph)) + noise));
            }
        };

        // 30 ms di solo rumore per assestare inviluppo e soglia, poi un
        // "risveglio" (4 impulsi di preambolo, scartati dal decoder) e
        // 2 ripetizioni della trama separate dalla pausa di sync, come
        // trasmettono i sensori veri.
        addSamples(30000.0, 0.0f);
        for (int i = 0; i < 4; i++) {
            addSamples(500.0, 0.8f);
            addSamples(4000.0, 0.0f);
        }
        uint64_t frame = nexusFrame(0x5C, true, 3, 24.6, 61);
        for (int rep = 0; rep < 2; rep++) {
            for (int i = 35; i >= 0; i--) {
                addSamples(500.0, 0.8f);
                addSamples(((frame >> i) & 1) ? 2000.0 : 1000.0, 0.0f);
            }
        }
        addSamples(500.0, 0.8f);   // impulso di chiusura
        addSamples(15000.0, 0.0f); // coda per svuotare l'ultimo gap

        std::vector<NexusReading> got;
        NexusDecoder dec([&](const NexusReading& r) { got.push_back(r); });
        OokDetector det([&](const Pulse& p) { dec.onPulse(p); });
        det.processIq(iq.data(), iq.size());

        std::printf("catena OOK: %zu letture\n", got.size());
        CHECK(!got.empty());
        for (const auto& r : got) {
            CHECK(r.id == 0x5C);
            CHECK(r.channel == 3);
            CHECK(std::fabs(r.tempC - 24.6) < 1e-9);
            CHECK(r.humidity == 61);
        }
    }

    if (failures == 0) std::printf("test_sensors433: OK\n");
    return failures == 0 ? 0 : 1;
}
