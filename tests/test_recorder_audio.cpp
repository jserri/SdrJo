//
// Test del registratore IQ (round-trip con FileSource) e dell'uscita
// audio (API sicura anche con il backend nullo).
//
#include <sdrjo/util/iq_recorder.hpp>
#include <sdrjo/source/file_source.hpp>
#include <sdrjo/audio/audio_output.hpp>
#include "test_util.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace sdrjo;

int main()
{
    // --- IqRecorder: registra e rileggi ------------------------------------
    {
        const char* path = "rec_test.bin";
        IqRecorder rec;
        CHECK(rec.start(path, 100e6, 48000.0));
        CHECK(rec.isRecording());

        // Un tono noto.
        std::vector<cfloat> sig(4800);
        for (size_t i = 0; i < sig.size(); i++) {
            double ph = 2.0 * M_PI * 1000.0 * double(i) / 48000.0;
            sig[i] = 0.5f * cfloat(float(std::cos(ph)), float(std::sin(ph)));
        }
        rec.write(sig.data(), sig.size());
        CHECK(rec.bytesWritten() == sig.size() * 2);
        CHECK_NEAR(rec.secondsWritten(), 0.1, 1e-6);
        rec.stop();

        // Il sidecar dei metadati deve esistere.
        FILE* meta = std::fopen("rec_test.bin.txt", "r");
        CHECK(meta != nullptr);
        if (meta) {
            char buf[256] = {0};
            std::fread(buf, 1, sizeof(buf) - 1, meta);
            CHECK(std::string(buf).find("100000000") != std::string::npos);
            std::fclose(meta);
        }

        // Round-trip con FileSource.
        std::vector<cfloat> readBack;
        std::atomic<bool> done{false};
        FileSource src(path, 48000.0, /*throttle=*/false);
        src.start([&](const cfloat* s, size_t n) {
            readBack.insert(readBack.end(), s, s + n);
        });
        for (int i = 0; i < 200 && src.isRunning(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        src.stop();

        CHECK(readBack.size() == sig.size());
        double maxErr = 0;
        for (size_t i = 0; i < readBack.size() && i < sig.size(); i++)
            maxErr = std::max(maxErr, double(std::abs(readBack[i] - sig[i])));
        CHECK(maxErr < 0.02); // quantizzazione a 8 bit
        std::remove(path);
        std::remove("rec_test.bin.txt");
    }

    // --- AudioOutput: mai crashare, con o senza backend ---------------------
    {
        AudioOutput audio;
        CHECK(!audio.backendName().empty());

        bool started = audio.start(48000.0, 2);
        // In un container senza scheda audio start() puo' fallire anche
        // con miniaudio: l'importante e' che le scritture siano sicure.
        std::vector<float> samples(960, 0.1f);
        audio.write(samples.data(), samples.size());
        audio.writeMono(samples.data(), samples.size());
        audio.stop();
        audio.write(samples.data(), samples.size()); // dopo stop: no-op
        (void)started;
    }

    return testResult("test_recorder_audio");
}
