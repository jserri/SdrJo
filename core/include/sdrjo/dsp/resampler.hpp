#pragma once
//
// Ricampionatori:
//  - LinearResampler:   interpolazione lineare, per segnali gia' filtrati
//                       (es. il video APT)
//  - RationalResampler: polifase L/M con filtro anti-alias, per i rapporti
//                       non interi delle catene radio (es. 2.4M -> 288k)
//
#include "types.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace sdrjo::dsp {

// Migliore approssimazione razionale L/M di x con L,M <= maxFactor
// (ricerca esaustiva: i fattori in gioco sono piccoli).
std::pair<unsigned, unsigned> rationalApprox(double x, unsigned maxFactor = 64);

// Polifase: sale di L, filtra, scende di M. Complesso, con stato streaming.
class RationalResampler {
public:
    // tapsPerPhase: lunghezza del filtro per fase (qualita' vs CPU).
    RationalResampler(unsigned interpolation, unsigned decimation,
                      int tapsPerPhase = 12);

    // Accoda i campioni ricampionati a out; ritorna quanti ne ha aggiunti.
    size_t process(const cfloat* in, size_t n, std::vector<cfloat>& out);

    unsigned interpolation() const { return L_; }
    unsigned decimation() const { return M_; }

private:
    unsigned L_, M_;
    int tapsPerPhase_;
    std::vector<float> taps_;      // filtro prototipo, lunghezza L*tapsPerPhase
    std::vector<cfloat> history_;  // ultimi tapsPerPhase campioni in ingresso
    size_t histPos_ = 0;
    uint64_t outPhase_ = 0;        // fase virtuale del prossimo output
};

class LinearResampler {
public:
    LinearResampler(double inRate, double outRate)
        : step_(inRate / outRate) {}

    // Accoda i campioni ricampionati a out; ritorna quanti ne ha aggiunti.
    size_t process(const float* in, size_t n, std::vector<float>& out)
    {
        size_t added = 0;
        for (size_t i = 0; i < n; i++) {
            float cur = in[i];
            while (pos_ <= 1.0) {
                float v = float(prev_ + (cur - prev_) * pos_);
                out.push_back(v);
                added++;
                pos_ += step_;
            }
            pos_ -= 1.0;
            prev_ = cur;
        }
        return added;
    }

private:
    double step_;
    double pos_ = 0.0;
    float prev_ = 0.0f;
};

} // namespace sdrjo::dsp
