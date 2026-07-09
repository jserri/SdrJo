#include "sdrjo/dsp/resampler.hpp"
#include "sdrjo/dsp/fir_filter.hpp"

#include <cmath>

namespace sdrjo::dsp {

std::pair<unsigned, unsigned> rationalApprox(double x, unsigned maxFactor)
{
    unsigned bestL = 1, bestM = 1;
    double bestErr = 1e18;
    for (unsigned m = 1; m <= maxFactor; m++) {
        unsigned l = unsigned(std::lround(x * m));
        if (l < 1 || l > maxFactor) continue;
        double err = std::fabs(double(l) / m - x);
        if (err < bestErr - 1e-15) {
            bestErr = err;
            bestL = l;
            bestM = m;
        }
    }
    return {bestL, bestM};
}

RationalResampler::RationalResampler(unsigned interpolation, unsigned decimation,
                                     int tapsPerPhase)
    : L_(interpolation), M_(decimation), tapsPerPhase_(tapsPerPhase),
      history_(size_t(tapsPerPhase), cfloat(0, 0))
{
    // Filtro prototipo alla frequenza "alta" (rate * L): taglia a meta'
    // del rate piu' basso tra ingresso e uscita.
    int numTaps = int(L_) * tapsPerPhase_;
    if (numTaps % 2 == 0) numTaps++;
    double cutoff = 0.45 / double(std::max(L_, M_)); // normalizzata su rate*L
    taps_ = designLowPass(1.0, cutoff, numTaps);
    // Compensa l'attenuazione 1/L dell'interpolazione a zeri.
    for (auto& t : taps_) t *= float(L_);
    taps_.resize(size_t(L_) * tapsPerPhase_, 0.0f); // pad al multiplo esatto
}

size_t RationalResampler::process(const cfloat* in, size_t n,
                                  std::vector<cfloat>& out)
{
    // Modello: il flusso interpolato virtuale ha indice t = i*L + p.
    // Ogni output avanza di M. Per l'input i-esimo sono pronti gli output
    // con fase p in [0, L).
    size_t added = 0;
    for (size_t i = 0; i < n; i++) {
        history_[histPos_] = in[i];

        // outPhase_ e' la fase (0..L-1) dell'output dovuto su questo input.
        while (outPhase_ < L_) {
            // y = somma_k h[p + k*L] * x[i - k]
            cfloat acc(0, 0);
            size_t idx = histPos_;
            for (int k = 0; k < tapsPerPhase_; k++) {
                size_t tapIdx = outPhase_ + size_t(k) * L_;
                if (tapIdx < taps_.size()) acc += history_[idx] * taps_[tapIdx];
                idx = (idx == 0) ? history_.size() - 1 : idx - 1;
            }
            out.push_back(acc);
            added++;
            outPhase_ += M_;
        }
        outPhase_ -= L_;

        histPos_ = (histPos_ + 1) % history_.size();
    }
    return added;
}

} // namespace sdrjo::dsp
