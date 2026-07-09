#include "sdrjo/dsp/correction.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace sdrjo::dsp {

double estimatePpm(const float* specDb, size_t n, double sampleRate,
                   double centerFreqHz, double expectedHz, double searchHz)
{
    if (n == 0 || centerFreqHz <= 0) return 0.0;

    const double hzPerBin = sampleRate / double(n);
    const double expectedOffset = expectedHz - centerFreqHz;

    // Bin atteso (DC al centro dello spettro).
    const double centerBin = double(n) / 2.0 + expectedOffset / hzPerBin;
    const int half = std::max(1, int(searchHz / hzPerBin));
    const int lo = std::max(0, int(centerBin) - half);
    const int hi = std::min(int(n) - 1, int(centerBin) + half);
    if (lo >= hi) return 0.0;

    // Picco nella finestra e mediana come stima del rumore.
    int peak = lo;
    std::vector<float> window;
    window.reserve(size_t(hi - lo + 1));
    for (int i = lo; i <= hi; i++) {
        window.push_back(specDb[i]);
        if (specDb[i] > specDb[peak]) peak = i;
    }
    std::nth_element(window.begin(), window.begin() + window.size() / 2,
                     window.end());
    float median = window[window.size() / 2];
    if (specDb[peak] < median + 6.0f) return 0.0; // nessuna portante chiara

    // Interpolazione parabolica sul picco per una stima sub-bin.
    double delta = 0.0;
    if (peak > 0 && peak < int(n) - 1) {
        double a = specDb[peak - 1], b = specDb[peak], c = specDb[peak + 1];
        double den = a - 2.0 * b + c;
        if (std::fabs(den) > 1e-9) delta = 0.5 * (a - c) / den;
    }

    double measuredOffset = (double(peak) + delta - double(n) / 2.0) * hzPerBin;
    double errorHz = measuredOffset - expectedOffset;
    return errorHz / centerFreqHz * 1e6;
}

} // namespace sdrjo::dsp
