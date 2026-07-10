#pragma once
//
// FFT radix-2 in-place, senza dipendenze esterne. Sufficiente per lo
// spettro/waterfall; per pipeline ad alte prestazioni si potra' passare
// a FFTW o a kissfft mantenendo la stessa interfaccia.
//
#include "types.hpp"
#include <cstddef>

namespace sdrjo::dsp {

// FFT in avanti, in-place. n deve essere una potenza di 2.
void fft(cfloat* data, size_t n);

// FFT inversa, in-place (normalizzata per 1/n). n potenza di 2.
void ifft(cfloat* data, size_t n);

// Riempe out[n] con la finestra di Hann.
void hannWindow(float* out, size_t n);

// Riempe out[n] con la finestra di Blackman-Harris a 4 termini: lobi
// laterali molto piu' bassi della Hann (-92 dB), il segnale forte non
// "sbrodola" su quello debole accanto (al prezzo di un picco piu' largo).
void blackmanHarrisWindow(float* out, size_t n);

enum class FftWindow { Hann, BlackmanHarris };

// Calcola lo spettro di potenza in dBFS a partire da n campioni IQ,
// con finestratura e shift DC-al-centro. out deve avere n elementi.
void powerSpectrumDb(const cfloat* in, size_t n, float* out,
                     FftWindow window = FftWindow::Hann);

} // namespace sdrjo::dsp
