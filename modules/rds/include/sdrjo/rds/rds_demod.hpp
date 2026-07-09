#pragma once
//
// Demodulatore RDS: dal segnale multiplex FM (uscita del demodulatore FM,
// campioni reali) ai bit per RdsDecoder.
//
// Catena: mix a -57 kHz -> passa-basso + decimazione -> Costas BPSK ->
// campionamento simboli (2375 Hz) -> decodifica bifase -> differenziale.
//
#include <sdrjo/dsp/types.hpp>
#include <sdrjo/dsp/fir_filter.hpp>

#include <functional>
#include <vector>

namespace sdrjo::rds {

class RdsDemodulator {
public:
    static constexpr double kSubcarrierHz = 57000.0;
    static constexpr double kBitRate = 1187.5;      // bit/s
    static constexpr double kSymbolRate = 2375.0;   // simboli bifase/s

    // sampleRate: frequenza di campionamento del multiplex (>= 128 kHz).
    explicit RdsDemodulator(double sampleRateHz,
                            std::function<void(uint8_t)> onBit);

    void processMultiplex(const float* samples, size_t n);

    bool locked() const { return locked_; }

private:
    void onSymbol(float sym);

    double sampleRate_;
    unsigned decim_;
    double basebandRate_;
    double samplesPerSymbol_;

    dsp::FrequencyShifter shifter_;
    dsp::FirDecimator decimator_;

    // Costas loop BPSK sul residuo di fase.
    double phase_ = 0.0, freq_ = 0.0;
    double alpha_, beta_;
    bool locked_ = false;

    // Clock di simbolo.
    double clock_ = 0.0;
    float prevSample_ = 0.0f;

    // Bifase + differenziale.
    std::vector<float> symbolPair_;
    int pairPhase_ = 0;
    int pairSlipVotes_ = 0;
    uint8_t prevEncodedBit_ = 0;

    std::function<void(uint8_t)> onBit_;
    std::vector<cfloat> mixBuf_;
    std::vector<cfloat> bbBuf_;
};

} // namespace sdrjo::rds
