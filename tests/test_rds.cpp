//
// Test RDS: checkword, sincronizzazione blocchi/gruppi da bitstream,
// e demodulazione end-to-end dal multiplex sintetizzato (57 kHz).
//
#include <sdrjo/rds/rds_blocks.hpp>
#include <sdrjo/rds/rds_demod.hpp>
#include "test_util.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace sdrjo::rds;

// Costruisce i 4 gruppi 0A che trasmettono PI + PS (8 caratteri).
static std::vector<uint32_t> makePsGroups(uint16_t pi, const char* ps)
{
    std::vector<uint32_t> blocks;
    for (int seg = 0; seg < 4; seg++) {
        uint16_t b = uint16_t((0 << 12) | (0 << 11) | (1 << 10) | (10 << 5) | seg);
        uint16_t d = uint16_t((uint8_t(ps[seg * 2]) << 8) | uint8_t(ps[seg * 2 + 1]));
        blocks.push_back(encodeBlock(pi, kOffsetA));
        blocks.push_back(encodeBlock(b, kOffsetB));
        blocks.push_back(encodeBlock(0x1234, kOffsetC)); // AF, ignorato
        blocks.push_back(encodeBlock(d, kOffsetD));
    }
    return blocks;
}

static std::vector<uint8_t> blocksToBits(const std::vector<uint32_t>& blocks)
{
    std::vector<uint8_t> bits;
    for (uint32_t blk : blocks)
        for (int i = 25; i >= 0; i--)
            bits.push_back(uint8_t((blk >> i) & 1));
    return bits;
}

int main()
{
    // --- Checkword: proprieta' di base -------------------------------------
    {
        CHECK(checkword(0x0000) == 0x000);
        // Blocco codificato + offset giusto -> verifica coerente.
        uint32_t blk = encodeBlock(0xABCD, kOffsetB);
        uint16_t info = uint16_t(blk >> 10);
        uint16_t crc = uint16_t(blk & 0x3FF);
        CHECK(info == 0xABCD);
        CHECK((checkword(info) ^ kOffsetB) == crc);
        CHECK((checkword(info) ^ kOffsetA) != crc); // offset diverso -> no
    }

    // --- Decoder di bitstream: PS name --------------------------------------
    {
        RdsDecoder dec;
        // Rumore iniziale per esercitare la ricerca del sync.
        for (int i = 0; i < 41; i++) dec.pushBit(uint8_t((i * 7) & 1));
        auto bits = blocksToBits(makePsGroups(0x5264, "SDRJO FM"));
        // Trasmetti i gruppi due volte (il primo aggancia il sync).
        for (int rep = 0; rep < 2; rep++)
            for (uint8_t b : bits) dec.pushBit(b);

        CHECK(dec.station().synced);
        CHECK(dec.station().pi == 0x5264);
        CHECK(dec.station().pty == 10);
        CHECK(dec.station().tp == true);
        CHECK(std::string(dec.station().ps) == "SDRJO FM");
        CHECK(dec.station().groupCount >= 4);
    }

    // --- RadioText (gruppo 2A) ----------------------------------------------
    {
        RdsDecoder dec;
        const std::string rt = "CIAO DA SDRJO! ";  // 16 char = 4 segmenti
        std::vector<uint32_t> blocks;
        for (int rep = 0; rep < 2; rep++) {
            for (int seg = 0; seg < 4; seg++) {
                uint16_t b = uint16_t((2 << 12) | (0 << 11) | (5 << 5) | seg);
                uint16_t c = uint16_t((uint8_t(rt[seg * 4]) << 8) | uint8_t(rt[seg * 4 + 1]));
                uint16_t d = uint16_t((uint8_t(rt[seg * 4 + 2]) << 8) | uint8_t(rt[seg * 4 + 3]));
                blocks.push_back(encodeBlock(0x5264, kOffsetA));
                blocks.push_back(encodeBlock(b, kOffsetB));
                blocks.push_back(encodeBlock(c, kOffsetC));
                blocks.push_back(encodeBlock(d, kOffsetD));
            }
        }
        for (uint8_t b : blocksToBits(blocks)) dec.pushBit(b);
        CHECK(std::string(dec.station().radioText).rfind("CIAO DA SDRJO!", 0) == 0);
    }

    // --- End-to-end dal multiplex a 57 kHz ----------------------------------
    {
        const double fs = 240000.0;
        auto bits = blocksToBits(makePsGroups(0x5264, "SDRJO FM"));

        // Codifica differenziale + bifase + modulazione sulla sottoportante.
        std::vector<float> mpx;
        uint8_t prev = 0;
        double samplesPerSym = fs / RdsDemodulator::kSymbolRate;
        double symAcc = 0.0;
        size_t sampleIdx = 0;
        auto emitSymbol = [&](float level) {
            symAcc += samplesPerSym;
            size_t count = size_t(symAcc);
            symAcc -= double(count);
            for (size_t i = 0; i < count; i++) {
                double t = double(sampleIdx++) / fs;
                mpx.push_back(level * float(std::cos(2.0 * M_PI * 57000.0 * t)));
            }
        };
        // Preambolo di soli zeri per far agganciare Costas e clock.
        std::vector<uint8_t> tx(100, 0);
        for (int rep = 0; rep < 3; rep++)
            tx.insert(tx.end(), bits.begin(), bits.end());
        for (uint8_t b : tx) {
            uint8_t e = b ^ prev;   // codifica differenziale
            prev = e;
            emitSymbol(e ? 1.0f : -1.0f);   // bifase: due meta' opposte
            emitSymbol(e ? -1.0f : 1.0f);
        }

        RdsDecoder dec;
        RdsDemodulator demod(fs, [&](uint8_t bit) { dec.pushBit(bit); });
        demod.processMultiplex(mpx.data(), mpx.size());

        CHECK(dec.station().synced);
        CHECK(dec.station().pi == 0x5264);
        CHECK(std::string(dec.station().ps) == "SDRJO FM");
    }

    return testResult("test_rds");
}
