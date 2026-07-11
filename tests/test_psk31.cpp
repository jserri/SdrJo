//
// Test del decoder PSK31 (BPSK31): sintetizza audio BPSK differenziale a
// 31.25 baud su un tono (Varicode), poi verifica il testo decodificato,
// anche con un po' di rumore.
//
#include <sdrjo/dsp/psk31.hpp>

#include <cmath>
#include <cstdio>
#include <random>
#include <string>
#include <unordered_map>
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

// Stessa tabella Varicode del decoder (carattere -> bit).
struct Vc { char c; const char* bits; };
const Vc kVaricode[] = {
    {'\n', "11101"}, {'\r', "11111"}, {' ', "1"},
    {'!', "111111111"}, {'"', "101011111"}, {'#', "111110101"},
    {'$', "111011011"}, {'%', "1011010101"}, {'&', "1010111011"},
    {'\'', "101111111"}, {'(', "11111011"}, {')', "11110111"},
    {'*', "101101111"}, {'+', "111011111"}, {',', "1110101"},
    {'-', "110101"}, {'.', "1010111"}, {'/', "110101111"},
    {'0', "10110111"}, {'1', "10111101"}, {'2', "11101101"},
    {'3', "11111111"}, {'4', "101110111"}, {'5', "101011011"},
    {'6', "101101011"}, {'7', "110101101"}, {'8', "110101011"},
    {'9', "110110111"}, {':', "11110101"}, {';', "110111101"},
    {'<', "111101101"}, {'=', "1010101"}, {'>', "111010111"},
    {'?', "1010101111"}, {'@', "1010111101"}, {'A', "1111101"},
    {'B', "11101011"}, {'C', "10101101"}, {'D', "10110101"},
    {'E', "1110111"}, {'F', "11011011"}, {'G', "11111101"},
    {'H', "101010101"}, {'I', "1111111"}, {'J', "111111101"},
    {'K', "101111101"}, {'L', "11010111"}, {'M', "10111011"},
    {'N', "11011101"}, {'O', "10101011"}, {'P', "11010101"},
    {'Q', "111011101"}, {'R', "10101111"}, {'S', "1101111"},
    {'T', "1101101"}, {'U', "101010111"}, {'V', "110110101"},
    {'W', "101011101"}, {'X', "101110101"}, {'Y', "101111011"},
    {'Z', "1010101101"}, {'a', "1011"}, {'b', "1011111"},
    {'c', "101111"}, {'d', "101101"}, {'e', "11"},
    {'f', "111101"}, {'g', "1011011"}, {'h', "101011"},
    {'i', "1101"}, {'j', "111101011"}, {'k', "10111111"},
    {'l', "11011"}, {'m', "111011"}, {'n', "1111"},
    {'o', "111"}, {'p', "111111"}, {'q', "110111111"},
    {'r', "10101"}, {'s', "10111"}, {'t', "101"},
    {'u', "110111"}, {'v', "1111011"}, {'w', "1101011"},
    {'x', "11011111"}, {'y', "1011101"}, {'z', "111010101"},
};

std::string bitsFor(char c)
{
    for (const auto& v : kVaricode)
        if (v.c == c) return v.bits;
    return "";
}

// Genera l'audio BPSK31 di un messaggio.
struct Synth {
    double rate = 48000.0, baud = 31.25, toneHz = 1000.0;
    double amp = 0.7;
    double phase = 0.0; // fase della portante
    int state = 1;      // stato differenziale corrente (+1 / -1)
    std::vector<float> out;

    void symbol(int bit)
    {
        if (bit == 0) state = -state; // inversione di fase = 0
        int sps = int(std::lround(rate / baud));
        for (int i = 0; i < sps; i++) {
            out.push_back(float(amp * state * std::cos(phase)));
            phase += 2.0 * M_PI * toneHz / rate;
        }
    }
    void idle(int n) { for (int i = 0; i < n; i++) symbol(0); }
    void text(const std::string& msg)
    {
        idle(64); // preambolo: inversioni continue per l'aggancio
        for (char ch : msg) {
            std::string b = bitsFor(ch);
            for (char bc : b) symbol(bc - '0');
            symbol(0); // separatore "00"
            symbol(0);
        }
        idle(16);
    }
};

std::string decodeAll(const std::vector<float>& audio, double toneHz = 1000.0)
{
    std::string got;
    sdrjo::dsp::Psk31Decoder dec(48000.0, [&](char c) { got += c; }, toneHz);
    dec.processAudio(audio.data(), audio.size());
    return got;
}

} // namespace

int main()
{
    // Messaggio tipico di un QSO.
    {
        Synth s;
        s.text("CQ de SDRJO");
        std::string got = decodeAll(s.out);
        std::printf("decodificato: '%s'\n", got.c_str());
        CHECK(got.find("CQ de SDRJO") != std::string::npos);
    }

    // Con un po' di rumore additivo.
    {
        Synth s;
        s.text("test psk31");
        std::mt19937 rng(11);
        std::normal_distribution<float> noise(0.0f, 0.05f);
        for (auto& v : s.out) v += noise(rng);
        std::string got = decodeAll(s.out);
        std::printf("con rumore:   '%s'\n", got.c_str());
        CHECK(got.find("test psk31") != std::string::npos);
    }

    if (failures == 0) std::printf("test_psk31: OK\n");
    return failures == 0 ? 0 : 1;
}
