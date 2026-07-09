#pragma once
//
// Registratore IQ: salva il flusso in formato u8 interleaved (lo stesso di
// rtl_sdr, riproducibile con FileSource e con sdrjo-cli), piu' un file
// .txt di fianco con frequenza/rate/data per non perdersi le registrazioni.
//
#include "../dsp/types.hpp"

#include <cstdio>
#include <string>

namespace sdrjo {

class IqRecorder {
public:
    ~IqRecorder() { stop(); }

    // Apre <path> e scrive <path>.txt con i metadati.
    bool start(const std::string& path, double centerFreqHz, double sampleRateHz);
    void stop();

    bool isRecording() const { return file_ != nullptr; }
    size_t bytesWritten() const { return bytesWritten_; }
    double secondsWritten() const
    {
        return rateHz_ > 0 ? double(bytesWritten_) / 2.0 / rateHz_ : 0.0;
    }
    const std::string& path() const { return path_; }

    // Da chiamare dal thread DSP con i campioni in transito.
    void write(const cfloat* samples, size_t n);

private:
    FILE* file_ = nullptr;
    std::string path_;
    double rateHz_ = 0.0;
    size_t bytesWritten_ = 0;
};

} // namespace sdrjo
