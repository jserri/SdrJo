//
// Test del decoder RTTY: sintetizza FSK Baudot a 45.45 baud (mark 2125,
// space 2295) e verifica il testo decodificato, anche con rumore e in
// polarita' invertita.
//
#include <sdrjo/dsp/rtty.hpp>

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

namespace {

const char kLtrs[32] = {'\0', 'E', '\n', 'A', ' ', 'S',  'I', 'U',
                        '\r', 'D', 'R',  'J', 'N', 'F',  'C', 'K',
                        'T',  'Z', 'L',  'W', 'H', 'Y',  'P', 'Q',
                        'O',  'B', 'G',  '\0', 'M', 'X', 'V', '\0'};
const char kFigs[32] = {'\0', '3', '\n', '-', ' ', '\'', '8', '7',
                        '\r', '$', '4',  '#', ',', '!',  ':', '(',
                        '5',  '"', ')',  '2', '=', '6',  '0', '1',
                        '9',  '?', '&',  '\0', '.', '/', ';', '\0'};

// Genera l'audio FSK di un messaggio (con i cambi LTRS/FIGS necessari).
struct Synth {
    double rate = 48000.0, baud = 45.45;
    double markHz = 2125.0, spaceHz = 2295.0;
    double phase = 0.0;
    bool reverse = false;
    std::vector<float> out;

    void tone(double freqHz, double bits)
    {
        size_t n = size_t(bits * rate / baud);
        for (size_t i = 0; i < n; i++) {
            phase += 2.0 * M_PI * freqHz / rate;
            out.push_back(0.7f * float(std::sin(phase)));
        }
    }
    void bit(bool mark, double len = 1.0)
    {
        bool m = reverse ? !mark : mark;
        tone(m ? markHz : spaceHz, len);
    }
    void code(int c)
    {
        bit(false);                       // start
        for (int k = 0; k < 5; k++) bit((c >> k) & 1);
        bit(true, 1.5);                   // stop
    }
    void text(const std::string& msg)
    {
        bit(true, 20); // lead-in a mark
        bool figs = false;
        for (char ch : msg) {
            int lc = -1, fc = -1;
            for (int i = 0; i < 32; i++) {
                if (kLtrs[i] == ch && i != 0) lc = i;
                if (kFigs[i] == ch && i != 0) fc = i;
            }
            if (lc >= 0 && (!figs || fc < 0)) {
                if (figs) { code(31); figs = false; } // LTRS
                code(lc);
            } else if (fc >= 0) {
                if (!figs) { code(27); figs = true; } // FIGS
                code(fc);
            }
        }
        bit(true, 10); // coda
    }
};

std::string decodeAll(const std::vector<float>& audio, bool reverse = false)
{
    std::string got;
    sdrjo::dsp::RttyDecoder dec(48000.0, [&](char c) {
        if (c != '\r' && c != '\n') got += c;
    });
    dec.setReverse(reverse);
    dec.processAudio(audio.data(), audio.size());
    return got;
}

} // namespace

int main()
{
    // Messaggio classico con lettere, spazi e cifre (cambio FIGS/LTRS).
    {
        Synth s;
        s.text("CQ CQ DE SDRJO 599");
        std::string got = decodeAll(s.out);
        std::printf("decodificato: '%s'\n", got.c_str());
        CHECK(got.find("CQ CQ DE SDRJO") != std::string::npos);
        CHECK(got.find("599") != std::string::npos);
    }

    // Con rumore sopra il segnale.
    {
        Synth s;
        s.text("RYRY TEST");
        std::mt19937 rng(7);
        std::normal_distribution<float> noise(0.0f, 0.15f);
        for (auto& v : s.out) v += noise(rng);
        std::string got = decodeAll(s.out);
        std::printf("con rumore:   '%s'\n", got.c_str());
        CHECK(got.find("RYRY TEST") != std::string::npos);
    }

    // Polarita' invertita + setReverse.
    {
        Synth s;
        s.reverse = true;
        s.text("HELLO");
        CHECK(decodeAll(s.out, true).find("HELLO") != std::string::npos);
        // Senza reverse NON deve decodificare lo stesso testo.
        CHECK(decodeAll(s.out, false).find("HELLO") == std::string::npos);
    }

    if (failures == 0) std::printf("test_rtty: OK\n");
    return failures == 0 ? 0 : 1;
}
