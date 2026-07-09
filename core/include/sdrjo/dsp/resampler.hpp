#pragma once
//
// Ricampionatore lineare per rapporti arbitrari (header-only).
// Qualita' adeguata per segnali gia' filtrati passa-basso (es. il video
// APT); per audio hi-fi si usera' un polifase in futuro.
//
#include <cstddef>
#include <vector>

namespace sdrjo::dsp {

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
