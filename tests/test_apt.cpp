//
// Test del decoder NOAA APT: sintetizza l'audio (sottoportante AM a
// 2400 Hz) di alcune righe con sync e pattern noto e verifica l'immagine.
//
#include <sdrjo/apt/apt_decoder.hpp>
#include <sdrjo/util/bmp_writer.hpp>
#include "test_util.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace sdrjo::apt;

int main()
{
    const double fs = 24960.0; // 6 campioni per pixel: rapporto esatto
    const int lines = 12;

    // Costruisci le righe: sync A + livello variabile per riga.
    std::vector<float> pixelLevels;
    for (int line = 0; line < lines; line++) {
        // Sync A: 4 bassi, 7 cicli (2 alti 2 bassi), coda bassa.
        std::vector<float> row(kPixelsPerLine, 0.0f);
        for (int c = 0; c < 7; c++) {
            row[4 + c * 4] = 1.0f;
            row[5 + c * 4] = 1.0f;
        }
        // Canale video A: livello costante crescente con la riga.
        float level = 0.15f + 0.06f * float(line);
        for (int i = 86; i < 990; i++) row[i] = level;
        // Marker bianco fisso (tipo minute marker).
        for (int i = 1000; i < 1040; i++) row[i] = 1.0f;
        pixelLevels.insert(pixelLevels.end(), row.begin(), row.end());
    }

    // Modula in AM sulla sottoportante a 2400 Hz.
    std::vector<float> audio;
    audio.reserve(size_t(pixelLevels.size() * 6));
    size_t idx = 0;
    for (float px : pixelLevels) {
        for (int k = 0; k < 6; k++) {
            double t = double(idx++) / fs;
            // Profondita' di modulazione standard APT: 87.5%.
            float amp = 0.125f + 0.875f * px;
            audio.push_back(amp * float(std::cos(2.0 * M_PI * 2400.0 * t)));
        }
    }

    AptDecoder dec(fs);
    dec.processAudio(audio.data(), audio.size());

    CHECK(dec.rows() >= lines - 3);   // le prime righe servono all'aggancio
    CHECK(dec.syncedRows() >= dec.rows() - 2);

    // Nelle righe agganciate il sync deve stare all'inizio riga:
    // pixel 0..3 bassi, pixel 4..5 alti.
    const auto& img = dec.image();
    int checked = 0;
    for (int r = 3; r < dec.rows(); r++) {
        const uint8_t* row = img.data() + size_t(r) * kPixelsPerLine;
        // Impulsi del sync ai pixel 4-5 e 8-9 (i fronti 3/6 sono smussati
        // dal filtro dell'inviluppo).
        CHECK(row[4] > 150 && row[5] > 150);
        CHECK(row[8] > 150 && row[9] > 150);
        CHECK(row[0] < 90 && row[2] < 90);      // testa bassa
        CHECK(row[1010] > 170);                 // marker bianco
        checked++;
    }
    CHECK(checked >= 5);

    // Il canale video (grigio) deve stare tra la testa (nera) e il sync
    // (bianco) in ogni riga agganciata.
    if (dec.rows() >= 6) {
        int r = dec.rows() - 2;
        int v = img[size_t(r) * kPixelsPerLine + 500];
        CHECK(v > 60 && v < 220);
    }

    // Scrittura BMP.
    {
        const char* path = "test_apt.bmp";
        CHECK(sdrjo::writeGrayscaleBmp(path, img.data(), kPixelsPerLine, dec.rows()));
        FILE* f = std::fopen(path, "rb");
        CHECK(f != nullptr);
        if (f) {
            char hdr[2];
            CHECK(std::fread(hdr, 1, 2, f) == 2);
            CHECK(hdr[0] == 'B' && hdr[1] == 'M');
            std::fclose(f);
        }
        std::remove(path);
    }

    return testResult("test_apt");
}
