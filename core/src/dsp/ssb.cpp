#include "sdrjo/dsp/ssb.hpp"

namespace sdrjo::dsp {

// USB: banda utile [0, +B]; LSB: [-B, 0]. La si centra su DC spostando
// di -/+ B/2, si filtra con un passa-basso a B/2 e si torna indietro:
// il risultato e' l'analitico della sola banda scelta, audio = parte reale.
SsbDemodulator::SsbDemodulator(double sampleRate, bool upperSideband,
                               double bandwidthHz, float gain)
    : sampleRate_(sampleRate), bandwidthHz_(bandwidthHz), usb_(upperSideband),
      gain_(gain),
      shiftDown_(sampleRate, upperSideband ? -bandwidthHz / 2 : bandwidthHz / 2),
      shiftUp_(sampleRate, upperSideband ? bandwidthHz / 2 : -bandwidthHz / 2),
      lpf_(designLowPass(sampleRate, bandwidthHz / 2, 201))
{
}

void SsbDemodulator::setSideband(bool upperSideband)
{
    usb_ = upperSideband;
    shiftDown_.setShift(usb_ ? -bandwidthHz_ / 2 : bandwidthHz_ / 2);
    shiftUp_.setShift(usb_ ? bandwidthHz_ / 2 : -bandwidthHz_ / 2);
}

size_t SsbDemodulator::process(const cfloat* iq, size_t n,
                               std::vector<float>& out)
{
    a_.resize(n);
    b_.resize(n);
    shiftDown_.process(iq, n, a_.data());
    lpf_.process(a_.data(), n, b_.data());
    shiftUp_.process(b_.data(), n, a_.data());
    for (size_t i = 0; i < n; i++) out.push_back(gain_ * a_[i].real());
    return n;
}

} // namespace sdrjo::dsp
