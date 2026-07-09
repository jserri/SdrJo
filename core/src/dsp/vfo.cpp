#include "sdrjo/dsp/vfo.hpp"

#include <cmath>

namespace sdrjo::dsp {

Vfo::Vfo(double inRate, double outRate, double offsetHz)
    : shifter_(inRate, -offsetHz)
{
    // Prima si scende il piu' possibile con la decimazione intera
    // (economica), poi si rifinisce con il polifase razionale.
    unsigned decim = std::max(1u, unsigned(std::floor(inRate / outRate)));
    double rateAfterDecim = inRate / decim;

    if (decim > 1) {
        // Anti-alias: taglio a meta' del rate finale.
        int taps = int(8 * decim) | 1;
        decimator_ = std::make_unique<FirDecimator>(
            designLowPass(inRate, outRate * 0.45, taps), decim);
    }

    double residual = outRate / rateAfterDecim; // in (0.5, 1]
    if (std::fabs(residual - 1.0) > 1e-9) {
        auto [l, m] = rationalApprox(residual);
        resampler_ = std::make_unique<RationalResampler>(l, m);
        actualOutRate_ = rateAfterDecim * double(l) / double(m);
    } else {
        actualOutRate_ = rateAfterDecim;
    }
}

size_t Vfo::process(const cfloat* in, size_t n, std::vector<cfloat>& out)
{
    shifted_.resize(n);
    shifter_.process(in, n, shifted_.data());

    const cfloat* stage = shifted_.data();
    size_t stageN = n;

    if (decimator_) {
        decimated_.resize(n / decimator_->decimation() + 2);
        stageN = decimator_->process(shifted_.data(), n, decimated_.data());
        stage = decimated_.data();
    }

    if (resampler_) return resampler_->process(stage, stageN, out);

    out.insert(out.end(), stage, stage + stageN);
    return stageN;
}

} // namespace sdrjo::dsp
