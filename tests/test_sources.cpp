//
// Test delle sorgenti campioni: replay da file IQ e comportamento sicuro
// del wrapper RTL-SDR quando librtlsdr non e' presente.
//
#include <sdrjo/source/file_source.hpp>
#include <sdrjo/source/rtl_sdr_source.hpp>
#include "test_util.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>
#include <vector>

using namespace sdrjo;

int main()
{
    // --- FileSource: replay completo di un file noto -----------------------
    {
        const char* path = "test_iq.bin";
        FILE* f = std::fopen(path, "wb");
        CHECK(f != nullptr);
        // 1000 campioni: I crescente, Q = 127 (zero).
        for (int i = 0; i < 1000; i++) {
            uint8_t iq[2] = {uint8_t(i & 0xFF), 127};
            std::fwrite(iq, 1, 2, f);
        }
        std::fclose(f);

        std::atomic<size_t> received{0};
        FileSource src(path, 48000.0, /*throttle=*/false);
        CHECK(src.setCenterFrequency(100e6));
        src.start([&](const cfloat*, size_t n) { received += n; });

        for (int i = 0; i < 200 && src.isRunning(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        src.stop();

        CHECK(received.load() == 1000);
        std::remove(path);
    }

    // --- RtlSdrSource: mai crashare senza libreria/hardware ----------------
    {
        // Qualunque sia l'esito del caricamento runtime, queste chiamate
        // devono essere sicure.
        bool avail = RtlSdrSource::available();
        auto hint = RtlSdrSource::libraryHint();
        CHECK(!hint.empty());
        auto devices = RtlSdrSource::enumerate();
        if (!avail) CHECK(devices.empty());

        if (devices.empty()) {
            // Senza hardware il costruttore deve lanciare, non crashare.
            bool threw = false;
            try {
                RtlSdrSource src(0);
            } catch (const std::exception&) {
                threw = true;
            }
            CHECK(threw);
        }
    }

    return testResult("test_sources");
}
