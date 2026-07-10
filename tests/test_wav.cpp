//
// Test dello scrittore WAV: header corretto e campioni fedeli.
//
#include <sdrjo/util/wav_writer.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static uint32_t rdU32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

static uint16_t rdU16(const uint8_t* p)
{
    return uint16_t(p[0] | (p[1] << 8));
}

int main()
{
    const char* path = "test_out.wav";
    const int rate = 48000;

    {
        sdrjo::WavWriter w;
        CHECK(w.start(path, rate, 1));
        CHECK(w.isOpen());
        std::vector<float> chunk = {0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 2.0f};
        w.write(chunk.data(), chunk.size());
        CHECK(std::fabs(w.secondsWritten() - 6.0 / rate) < 1e-9);
        w.stop();
        CHECK(!w.isOpen());
    }

    FILE* f = std::fopen(path, "rb");
    CHECK(f != nullptr);
    if (f) {
        uint8_t buf[44 + 12];
        size_t n = std::fread(buf, 1, sizeof(buf), f);
        std::fclose(f);
        CHECK(n == 44 + 12);
        CHECK(std::memcmp(buf, "RIFF", 4) == 0);
        CHECK(std::memcmp(buf + 8, "WAVE", 4) == 0);
        CHECK(rdU32(buf + 4) == 36 + 12);    // dimensione RIFF
        CHECK(rdU16(buf + 20) == 1);         // PCM
        CHECK(rdU16(buf + 22) == 1);         // mono
        CHECK(rdU32(buf + 24) == 48000);
        CHECK(rdU16(buf + 34) == 16);        // bit
        CHECK(std::memcmp(buf + 36, "data", 4) == 0);
        CHECK(rdU32(buf + 40) == 12);        // 6 campioni x 2 byte

        auto smp = [&](int i) {
            return int16_t(buf[44 + 2 * i] | (buf[45 + 2 * i] << 8));
        };
        CHECK(smp(0) == 0);
        CHECK(smp(1) == 16383);
        CHECK(smp(2) == -16383);
        CHECK(smp(3) == 32767);
        CHECK(smp(4) == -32767);
        CHECK(smp(5) == 32767); // 2.0 va limitato a fondo scala
    }
    std::remove(path);

    if (failures == 0) std::printf("test_wav: OK\n");
    return failures == 0 ? 0 : 1;
}
