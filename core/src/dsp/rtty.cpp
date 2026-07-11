#include "sdrjo/dsp/rtty.hpp"

#include <cmath>

namespace sdrjo::dsp {

namespace {
constexpr double kPi = 3.14159265358979323846;

// Tabelle Baudot ITA2 (variante US-TTY, la stessa di fldigi).
const char kLtrs[32] = {'\0', 'E', '\n', 'A', ' ', 'S',  'I', 'U',
                        '\r', 'D', 'R',  'J', 'N', 'F',  'C', 'K',
                        'T',  'Z', 'L',  'W', 'H', 'Y',  'P', 'Q',
                        'O',  'B', 'G',  '\0', 'M', 'X', 'V', '\0'};
const char kFigs[32] = {'\0', '3', '\n', '-', ' ', '\'', '8', '7',
                        '\r', '$', '4',  '#', ',', '!',  ':', '(',
                        '5',  '"', ')',  '2', '=', '6',  '0', '1',
                        '9',  '?', '&',  '\0', '.', '/', ';', '\0'};
constexpr int kCodeFigs = 27;
constexpr int kCodeLtrs = 31;
} // namespace

void RttyDecoder::Tone::configure(double freqHz, double rate, double bwHz)
{
    double w = 2.0 * kPi * freqHz / rate;
    coeffRe = float(std::cos(w));
    coeffIm = float(-std::sin(w));
    alpha = float(1.0 - std::exp(-2.0 * kPi * bwHz / rate));
    re = im = env = 0;
}

void RttyDecoder::Tone::step(float x)
{
    // Mixa il campione a banda base per questo tono e liscia: e' un
    // filtro passa-banda strettissimo attorno a freqHz.
    float nre = re * coeffRe - im * coeffIm;
    float nim = re * coeffIm + im * coeffRe;
    re = nre + alpha * (x - nre);
    im = nim - alpha * nim;
    float mag = re * re + im * im;
    env += 0.02f * (mag - env);
}

RttyDecoder::RttyDecoder(double sampleRate, CharCallback cb, double markHz,
                         double spaceHz, double baud)
    : cb_(std::move(cb)), rate_(sampleRate)
{
    samplesPerBit_ = sampleRate / baud;
    // Banda dei risonatori: meta' del baud rate abbondante.
    mark_.configure(markHz, sampleRate, baud);
    space_.configure(spaceHz, sampleRate, baud);
}

void RttyDecoder::reset()
{
    state_ = 0;
    bitIndex_ = 0;
    bits_ = 0;
    figs_ = false;
    lastTone_ = true;
    mark_.re = mark_.im = mark_.env = 0;
    space_.re = space_.im = space_.env = 0;
}

void RttyDecoder::processAudio(const float* samples, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        mark_.step(samples[i]);
        space_.step(samples[i]);
        bool tone = mark_.env > space_.env; // true = mark (riposo)
        if (reverse_) tone = !tone;

        if (state_ == 0) {
            // In attesa del fronte mark -> space che apre lo start bit.
            if (lastTone_ && !tone) {
                state_ = 1;
                sampleCount_ = 0;
                bitIndex_ = 0;
                bits_ = 0;
            }
        } else {
            sampleCount_ += 1.0;
            double t = sampleCount_ / samplesPerBit_;
            if (bitIndex_ == 0) {
                // Centro dello start bit: deve essere ancora space.
                if (t >= 0.5) {
                    if (tone) state_ = 0; // falso allarme
                    else bitIndex_ = 1;
                }
            } else if (bitIndex_ <= 5) {
                // Centro del bit dati (LSB per primo).
                if (t >= double(bitIndex_) + 0.5) {
                    if (tone) bits_ |= 1 << (bitIndex_ - 1);
                    bitIndex_++;
                }
            } else if (t >= 6.0) {
                // Tutti i bit letti: emetti e torna in attesa (lo stop
                // a mark fara' da fronte per il prossimo start).
                if (bits_ == kCodeFigs) figs_ = true;
                else if (bits_ == kCodeLtrs) figs_ = false;
                else {
                    char c = figs_ ? kFigs[bits_] : kLtrs[bits_];
                    if (c && cb_) cb_(c);
                }
                state_ = 0;
            }
        }
        lastTone_ = tone;
    }
}

} // namespace sdrjo::dsp
