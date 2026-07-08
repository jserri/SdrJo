//
// Test dei mattoni CCSDS del decoder LRPT: Viterbi, PN, ricerca sync.
//
#include <sdrjo/lrpt/viterbi27.hpp>
#include <sdrjo/lrpt/ccsds.hpp>
#include "test_util.hpp"

#include <cstdlib>
#include <vector>

using namespace sdrjo::lrpt;

int main()
{
    // --- Viterbi hard: round-trip senza errori ---------------------------
    {
        std::srand(42);
        std::vector<uint8_t> bits(512);
        for (auto& b : bits) b = uint8_t(std::rand() & 1);

        auto symbols = Viterbi27::encode(bits);
        CHECK(symbols.size() == bits.size() * 2);
        auto decoded = Viterbi27::decodeHard(symbols);
        CHECK(decoded == bits);
    }

    // --- Viterbi hard: corregge errori sparsi ----------------------------
    {
        std::srand(1234);
        std::vector<uint8_t> bits(512);
        for (auto& b : bits) b = uint8_t(std::rand() & 1);

        auto symbols = Viterbi27::encode(bits);
        // Inverti ~4% dei simboli, distanziati (entro la capacita' del codice).
        for (size_t i = 20; i < symbols.size(); i += 25)
            symbols[i] ^= 1;
        auto decoded = Viterbi27::decodeHard(symbols);
        CHECK(decoded == bits);
    }

    // --- Viterbi soft: rumore gaussiano leggero --------------------------
    {
        std::srand(99);
        std::vector<uint8_t> bits(256);
        for (auto& b : bits) b = uint8_t(std::rand() & 1);

        auto symbols = Viterbi27::encode(bits);
        std::vector<uint8_t> soft(symbols.size());
        for (size_t i = 0; i < symbols.size(); i++) {
            int ideal = symbols[i] ? 220 : 35;                // non saturato
            int noise = (std::rand() % 61) - 30;              // +-30
            int v = ideal + noise;
            soft[i] = uint8_t(v < 0 ? 0 : (v > 255 ? 255 : v));
        }
        auto decoded = Viterbi27::decodeSoft(soft);
        CHECK(decoded == bits);
    }

    // --- Sequenza PN: involutiva (derandom(derandom(x)) == x) ------------
    {
        std::vector<uint8_t> data(kCaduDataBytes);
        for (size_t i = 0; i < data.size(); i++) data[i] = uint8_t(i * 7 + 3);
        auto orig = data;
        derandomize(data.data(), data.size());
        CHECK(data != orig); // deve cambiare davvero
        derandomize(data.data(), data.size());
        CHECK(data == orig);
    }

    // --- Ricerca del sync word (dritto e invertito) -----------------------
    {
        std::vector<uint8_t> bits(300, 0);
        // Inserisci il sync 0x1ACFFC1D a offset 77.
        for (int b = 0; b < 32; b++)
            bits[77 + b] = uint8_t((kSyncWord >> (31 - b)) & 1);

        bool inverted = true;
        auto off = findSync(bits, inverted);
        CHECK(off.has_value());
        if (off) CHECK(*off == 77);
        CHECK(!inverted);

        // Versione con fase invertita.
        for (auto& b : bits) b ^= 1;
        auto off2 = findSync(bits, inverted);
        CHECK(off2.has_value());
        if (off2) CHECK(*off2 == 77);
        CHECK(inverted);
    }

    // --- packBits ---------------------------------------------------------
    {
        std::vector<uint8_t> bits = {1,0,1,0, 1,1,0,0, 1,1,1,1, 0,0,0,0};
        auto bytes = packBits(bits, 0, 16);
        CHECK(bytes.size() == 2);
        CHECK(bytes[0] == 0xAC);
        CHECK(bytes[1] == 0xF0);
    }

    return testResult("test_lrpt");
}
