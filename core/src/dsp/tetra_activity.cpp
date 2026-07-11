#include "sdrjo/dsp/tetra_activity.hpp"
#include "sdrjo/dsp/fft.hpp"

#include <algorithm>
#include <cmath>

namespace sdrjo::dsp {

namespace {
// Limiti (in Hz) delle bande di misura, riferiti al centro canale.
constexpr double kInBandHz = 10000.0;    // banda utile ~ meta' canale
constexpr double kNoiseLoHz = 14000.0;   // riferimento di rumore: fuori canale
constexpr double kNoiseHiHz = 22000.0;
constexpr float kSnrThreshDb = 6.0f;     // stacco minimo banda/rumore
constexpr float kOccThresh = 0.55f;      // frazione minima di banda "piena"
constexpr int kHoldBlocks = 40;          // ~tenuta anti-sfarfallio
} // namespace

TetraActivityDetector::TetraActivityDetector(double sampleRate)
    : rate_(sampleRate), fftN_(256)
{
    buf_.resize(fftN_);
    spec_.resize(fftN_);
}

void TetraActivityDetector::reset()
{
    pos_ = 0;
    snrDb_ = 0;
    occKHz_ = 0;
    level_ = 0;
    active_ = false;
    hold_ = 0;
}

void TetraActivityDetector::processIq(const cfloat* iq, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        buf_[pos_++] = iq[i];
        if (pos_ >= fftN_) {
            analyze();
            pos_ = 0;
        }
    }
}

void TetraActivityDetector::analyze()
{
    // Spettro di potenza in dB, DC al centro (bin fftN_/2).
    powerSpectrumDb(buf_.data(), fftN_, spec_.data());

    const double binHz = rate_ / double(fftN_);
    const size_t dc = fftN_ / 2;

    // Media della banda utile e del rumore fuori canale + conteggio dei
    // bin "pieni" (ben sopra il rumore) per stimare quanto e' occupata.
    double inSum = 0; int inCnt = 0;
    double nSum = 0; int nCnt = 0;
    for (size_t b = 0; b < fftN_; b++) {
        double f = std::fabs((double(b) - double(dc)) * binHz);
        if (f <= kInBandHz) { inSum += spec_[b]; inCnt++; }
        else if (f >= kNoiseLoHz && f <= kNoiseHiHz) { nSum += spec_[b]; nCnt++; }
    }
    if (inCnt == 0 || nCnt == 0) return;

    float inMean = float(inSum / inCnt);
    float noise = float(nSum / nCnt);
    float rawSnr = inMean - noise;

    // Frazione di banda utile ben sopra il rumore (portante piatto e largo
    // -> quasi tutti i bin; un fischio CW -> pochissimi).
    int filled = 0;
    for (size_t b = 0; b < fftN_; b++) {
        double f = std::fabs((double(b) - double(dc)) * binHz);
        if (f <= kInBandHz && spec_[b] > noise + 4.0f) filled++;
    }
    float occFrac = float(filled) / float(inCnt);

    // Medie mobili: i singoli blocchi FFT ballano molto (rumore), la media
    // stabilizza sia la decisione sia i numeri mostrati a schermo.
    snrDb_ += 0.1f * (rawSnr - snrDb_);
    occKHz_ += 0.1f * (float(filled) * float(binHz) / 1000.0f - occKHz_);

    bool detect = snrDb_ > kSnrThreshDb && occFrac > kOccThresh;
    if (detect) hold_ = kHoldBlocks;
    else if (hold_ > 0) hold_--;
    active_ = hold_ > 0;

    // Livello 0..1 per la barra: combina SNR (0..30 dB) e occupazione.
    float snrN = std::clamp(snrDb_ / 30.0f, 0.0f, 1.0f);
    float rawLevel = 0.5f * snrN + 0.5f * occFrac;
    level_ += 0.1f * (std::clamp(rawLevel, 0.0f, 1.0f) - level_);
}

} // namespace sdrjo::dsp
