#pragma once
//
// Sorgente da file IQ registrato (formato u8 interleaved I/Q, come rtl_sdr).
// Utile per sviluppare e testare i moduli senza hardware collegato.
//
#include "sample_source.hpp"

#include <atomic>
#include <string>
#include <thread>

namespace sdrjo {

class FileSource : public ISampleSource {
public:
    // throttle = true riproduce in tempo reale; false = alla massima velocita'.
    FileSource(std::string path, double sampleRateHz, bool throttle = true);
    ~FileSource() override;

    std::string name() const override { return "File IQ"; }

    bool setCenterFrequency(double hz) override { freqHz_ = hz; return true; }
    double centerFrequency() const override { return freqHz_; }

    bool setSampleRate(double hz) override { rateHz_ = hz; return true; }
    double sampleRate() const override { return rateHz_; }

    bool setGain(double) override { return true; }

    bool start(IqCallback cb) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }

private:
    void workerLoop();

    std::string path_;
    double freqHz_ = 0.0;
    double rateHz_;
    bool throttle_;
    IqCallback callback_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace sdrjo
