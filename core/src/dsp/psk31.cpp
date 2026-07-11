#include "sdrjo/dsp/psk31.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace sdrjo::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kBaud = 31.25;

// Varicode PSK31 (G3PLX): tabella carattere -> stringa di bit. Ogni codice
// inizia e finisce con '1' e non contiene mai "00"; i caratteri nel flusso
// sono separati da "00". Coperto tutto il set stampabile + CR/LF.
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
    {'Z', "1010101101"}, {'[', "111110111"}, {'\\', "111101111"},
    {']', "111111011"}, {'^', "1010111111"}, {'_', "101101101"},
    {'`', "1011011111"}, {'a', "1011"}, {'b', "1011111"},
    {'c', "101111"}, {'d', "101101"}, {'e', "11"},
    {'f', "111101"}, {'g', "1011011"}, {'h', "101011"},
    {'i', "1101"}, {'j', "111101011"}, {'k', "10111111"},
    {'l', "11011"}, {'m', "111011"}, {'n', "1111"},
    {'o', "111"}, {'p', "111111"}, {'q', "110111111"},
    {'r', "10101"}, {'s', "10111"}, {'t', "101"},
    {'u', "110111"}, {'v', "1111011"}, {'w', "1101011"},
    {'x', "11011111"}, {'y', "1011101"}, {'z', "111010101"},
    {'{', "1010110111"}, {'|', "110111011"}, {'}', "1010110101"},
    {'~', "1011010111"},
};

const std::unordered_map<std::string, char>& varicodeMap()
{
    static const std::unordered_map<std::string, char> m = [] {
        std::unordered_map<std::string, char> t;
        for (const auto& v : kVaricode) t[v.bits] = v.c;
        return t;
    }();
    return m;
}
} // namespace

Psk31Decoder::Psk31Decoder(double sampleRate, CharCallback cb, double toneHz)
    : cb_(std::move(cb)), rate_(sampleRate)
{
    // Filtro passa-basso a banda base: banda ~ velocita' di simbolo, cosi'
    // lascia passare il lobo BPSK ma taglia il rumore fuori banda.
    lpfAlpha_ = float(1.0 - std::exp(-2.0 * kPi * kBaud / rate_));
    // Campioni per simbolo (48 kHz -> 1536).
    samplesPerSym_ = std::max(1, int(std::lround(rate_ / kBaud)));
    setTone(toneHz);
}

void Psk31Decoder::setTone(double toneHz)
{
    toneHz_ = toneHz;
    ncoStep_ = 2.0 * kPi * toneHz_ / rate_;
}

void Psk31Decoder::reset()
{
    ncoPhase_ = 0;
    lpf_ = cfloat(0, 0);
    symPos_ = 0;
    acc_ = cfloat(0, 0);
    envMin_ = 1e9f;
    envMinPos_ = 0;
    syncPhase_ = 0;
    prevSym_ = cfloat(1, 0);
    lastSym_ = cfloat(0, 0);
    lockAcc_ = cfloat(0, 0);
    lockMag_ = 0.0f;
    code_.clear();
    lastBit_ = 1;
}

void Psk31Decoder::processAudio(const float* samples, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        // Mixaggio a banda base (tono -> 0 Hz) + passa-basso a una polo.
        cfloat bb = cfloat(samples[i] * float(std::cos(ncoPhase_)),
                           samples[i] * float(-std::sin(ncoPhase_)));
        ncoPhase_ += ncoStep_;
        if (ncoPhase_ > 2.0 * kPi) ncoPhase_ -= 2.0 * kPi;
        lpf_ += lpfAlpha_ * (bb - lpf_);

        // Integrate-and-dump sul simbolo. Le inversioni di fase (BPSK)
        // creano un minimo d'ampiezza al confine del simbolo: seguendo il
        // minimo agganciamo il tempo e campioniamo il simbolo al centro.
        acc_ += lpf_;
        float env = lpf_.real() * lpf_.real() + lpf_.imag() * lpf_.imag();
        if (env < envMin_) {
            envMin_ = env;
            envMinPos_ = symPos_;
        }
        symPos_++;

        int period = samplesPerSym_ + syncPhase_;
        if (symPos_ >= period) {
            // Campione del simbolo = media (filtro adattato rettangolare).
            cfloat sym = acc_ / float(std::max(1, symPos_));
            onSymbol(sym);

            // Correzione lenta del confine verso il minimo d'ampiezza: se il
            // minimo cade prima del centro il confine e' in ritardo e viceversa.
            int center = symPos_ / 2;
            syncPhase_ = 0;
            if (envMinPos_ < center - 2) syncPhase_ = -1;
            else if (envMinPos_ > center + 2) syncPhase_ = +1;

            acc_ = cfloat(0, 0);
            symPos_ = 0;
            envMin_ = 1e9f;
            envMinPos_ = 0;
        }
    }
}

void Psk31Decoder::onSymbol(cfloat sym)
{
    lastSym_ = sym;

    // Rivelatore d'aggancio a portante quadrata: elevando al quadrato il
    // versore del simbolo i due lobi BPSK (0 e pi) collassano su +1; la
    // media |.| e' alta se sono ben concentrati, bassa sul rumore.
    float m = std::sqrt(sym.real() * sym.real() + sym.imag() * sym.imag());
    if (m > 1e-6f) {
        float ur = sym.real() / m, ui = sym.imag() / m;
        cfloat u2(ur * ur - ui * ui, 2.0f * ur * ui);
        lockAcc_ = 0.97f * lockAcc_ + 0.03f * u2;
        lockMag_ = std::sqrt(lockAcc_.real() * lockAcc_.real() +
                             lockAcc_.imag() * lockAcc_.imag());
    }

    // BPSK differenziale: fase costante = 1, inversione = 0.
    float dot = sym.real() * prevSym_.real() + sym.imag() * prevSym_.imag();
    prevSym_ = sym;
    int bit = (dot >= 0.0f) ? 1 : 0;

    // Assemblaggio Varicode: "00" separa i caratteri.
    if (bit == 0 && lastBit_ == 0) {
        if (!code_.empty()) {
            code_.pop_back(); // il primo '0' era il delimitatore
            auto it = varicodeMap().find(code_);
            if (it != varicodeMap().end() && cb_) cb_(it->second);
            code_.clear();
        }
    } else {
        code_ += char('0' + bit);
        if (code_.size() > 20) code_.clear(); // niente sequenze assurde
    }
    lastBit_ = bit;
}

} // namespace sdrjo::dsp
