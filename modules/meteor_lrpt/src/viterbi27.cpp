#include "sdrjo/lrpt/viterbi27.hpp"

#include <algorithm>
#include <array>
#include <limits>

namespace sdrjo::lrpt {

static inline int parity(uint32_t x)
{
    x ^= x >> 16; x ^= x >> 8; x ^= x >> 4; x ^= x >> 2; x ^= x >> 1;
    return int(x & 1);
}

// Uscite (g1,g2) per uno stato a 7 bit (6 di memoria + bit corrente in testa).
static inline void branchOutput(uint32_t reg, int& g1, int& g2)
{
    g1 = parity(reg & Viterbi27::kPolyG1);
    g2 = parity(reg & Viterbi27::kPolyG2);
}

std::vector<uint8_t> Viterbi27::encode(const std::vector<uint8_t>& bits)
{
    std::vector<uint8_t> out;
    out.reserve(bits.size() * 2);
    uint32_t reg = 0; // 6 bit di memoria
    for (uint8_t b : bits) {
        reg = ((reg << 1) | (b & 1)) & 0x7F;
        int g1, g2;
        branchOutput(reg, g1, g2);
        out.push_back(uint8_t(g1));
        out.push_back(uint8_t(g2));
    }
    return out;
}

// Nucleo Viterbi comune: metrica di ramo fornita dal chiamante.
// metricFn(t, expectedG1, expectedG2) -> costo del ramo al passo t.
template <typename MetricFn>
static std::vector<uint8_t> viterbiCore(size_t numSteps, MetricFn metricFn)
{
    constexpr int kNumStates = Viterbi27::kNumStates;
    constexpr uint32_t kInf = std::numeric_limits<uint32_t>::max() / 2;

    std::vector<uint32_t> metric(kNumStates, kInf);
    metric[0] = 0; // il codificatore parte da stato zero
    std::vector<uint32_t> nextMetric(kNumStates);

    // decisions[t][s] = bit di ingresso che ha portato allo stato s al passo t
    // insieme allo stato precedente (impacchettato: prev<<1 | bit).
    std::vector<std::array<uint16_t, kNumStates>> decisions(numSteps);

    for (size_t t = 0; t < numSteps; t++) {
        std::fill(nextMetric.begin(), nextMetric.end(), kInf);
        for (int s = 0; s < kNumStates; s++) {
            if (metric[s] >= kInf) continue;
            for (int bit = 0; bit <= 1; bit++) {
                uint32_t reg = ((uint32_t(s) << 1) | bit) & 0x7F;
                int g1, g2;
                branchOutput(reg, g1, g2);
                int nextState = int(reg & 0x3F);
                uint32_t m = metric[s] + metricFn(t, g1, g2);
                if (m < nextMetric[nextState]) {
                    nextMetric[nextState] = m;
                    decisions[t][nextState] = uint16_t((s << 1) | bit);
                }
            }
        }
        metric.swap(nextMetric);
    }

    // Traceback dallo stato con metrica minima.
    int best = int(std::min_element(metric.begin(), metric.end()) - metric.begin());
    std::vector<uint8_t> bits(numSteps);
    int state = best;
    for (size_t t = numSteps; t-- > 0;) {
        uint16_t d = decisions[t][state];
        bits[t] = uint8_t(d & 1);
        state = d >> 1;
    }
    return bits;
}

std::vector<uint8_t> Viterbi27::decodeHard(const std::vector<uint8_t>& symbols)
{
    size_t numSteps = symbols.size() / 2;
    return viterbiCore(numSteps, [&](size_t t, int g1, int g2) -> uint32_t {
        return uint32_t((symbols[2 * t] != g1) + (symbols[2 * t + 1] != g2));
    });
}

std::vector<uint8_t> Viterbi27::decodeSoft(const std::vector<uint8_t>& soft)
{
    size_t numSteps = soft.size() / 2;
    return viterbiCore(numSteps, [&](size_t t, int g1, int g2) -> uint32_t {
        // Costo = distanza dal valore atteso (0 -> 0, 1 -> 255).
        uint32_t c1 = g1 ? (255u - soft[2 * t])     : soft[2 * t];
        uint32_t c2 = g2 ? (255u - soft[2 * t + 1]) : soft[2 * t + 1];
        return c1 + c2;
    });
}

} // namespace sdrjo::lrpt
