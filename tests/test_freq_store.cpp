//
// Test del frequency manager (persistenza CSV) e dei filtri audio.
//
#include <sdrjo/util/frequency_store.hpp>
#include <sdrjo/dsp/audio_filters.hpp>
#include "test_util.hpp"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace sdrjo;

static double toneRms(dsp::AudioFilterChain& ch, double freq, double rate)
{
    const size_t len = size_t(rate); // 1 secondo
    std::vector<float> x(len);
    for (size_t i = 0; i < x.size(); i++)
        x[i] = float(std::sin(2.0 * M_PI * freq * double(i) / rate));
    ch.reset();
    ch.process(x.data(), x.size());
    double acc = 0;
    for (size_t i = x.size() / 2; i < x.size(); i++) acc += double(x[i]) * x[i];
    return std::sqrt(acc / double(x.size() / 2));
}

int main()
{
    // --- FrequencyStore: round-trip su file --------------------------------
    {
        const char* path = "test_freq.csv";
        FrequencyStore store;
        store.add({"Radio Uno; test", 90.3e6, "WFM", 200000});
        store.add({"CQ 40m", 7.074e6, "USB", 2700});
        store.add({"NOAA 19", 137.1e6, "NFM", 40000});
        CHECK(store.save(path));

        FrequencyStore loaded;
        CHECK(loaded.load(path));
        CHECK(loaded.items().size() == 3);
        // Ordinata per frequenza.
        CHECK(loaded.items()[0].mode == "USB");
        CHECK_NEAR(loaded.items()[0].freqHz, 7.074e6, 1);
        CHECK_NEAR(loaded.items()[0].bandwidthHz, 2700, 1);
        // Il ';' nel nome e' stato sostituito (non rompe il CSV).
        CHECK(loaded.items()[1].name == "Radio Uno, test");
        CHECK(loaded.items()[2].name == "NOAA 19");

        loaded.remove(1);
        CHECK(loaded.items().size() == 2);
        std::remove(path);

        // File inesistente: lista vuota senza errore.
        FrequencyStore empty;
        CHECK(empty.load("non_esiste.csv"));
        CHECK(empty.items().empty());
    }

    // --- AudioFilterChain ----------------------------------------------------
    {
        const double rate = 48000.0;
        dsp::AudioFilterChain ch;

        // Passa-alto a 300 Hz: attenua 50 Hz, passa 2 kHz.
        ch.configure(rate, 300.0, 0.0);
        CHECK(toneRms(ch, 50.0, rate) < 0.25);
        CHECK(toneRms(ch, 2000.0, rate) > 0.6);

        // Passa-basso a 2 kHz: attenua 8 kHz, passa 500 Hz.
        ch.configure(rate, 0.0, 2000.0);
        CHECK(toneRms(ch, 8000.0, rate) < 0.25);
        CHECK(toneRms(ch, 500.0, rate) > 0.6);

        // Tutto spento: guadagno unitario.
        ch.configure(rate, 0.0, 0.0);
        CHECK_NEAR(toneRms(ch, 1000.0, rate), 1.0 / std::sqrt(2.0), 0.01);
    }

    return testResult("test_freq_store");
}
