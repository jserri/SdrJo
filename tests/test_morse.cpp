//
// Test del decoder CW: sintetizza l'inviluppo di "SOS SOS" a 20 WPM
// e verifica il testo decodificato.
//
#include <sdrjo/morse/cw_decoder.hpp>
#include <sdrjo/morse/morse_table.hpp>
#include "test_util.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace sdrjo::morse;

// Genera audio (tono a toneHz) con keying Morse del testo dato.
static std::vector<float> synthesize(const std::string& text, double rate,
                                     double dotMs, double toneHz)
{
    std::vector<float> out;
    auto emit = [&](bool on, double ms) {
        size_t n = size_t(rate * ms / 1000.0);
        for (size_t i = 0; i < n; i++) {
            double t = double(out.size()) / rate;
            out.push_back(on ? float(std::sin(2.0 * M_PI * toneHz * t)) : 0.0f);
        }
    };

    emit(false, 20 * dotMs); // silenzio iniziale
    for (char c : text) {
        if (c == ' ') { emit(false, 4 * dotMs); continue; } // + 3 della lettera = 7
        std::string sym = charToSymbol(c);
        for (size_t i = 0; i < sym.size(); i++) {
            emit(true, sym[i] == '.' ? dotMs : 3 * dotMs);
            if (i + 1 < sym.size()) emit(false, dotMs);
        }
        emit(false, 3 * dotMs); // spazio tra lettere
    }
    emit(false, 20 * dotMs);
    return out;
}

int main()
{
    // Tabella
    CHECK(symbolToChar("...") == 'S');
    CHECK(symbolToChar("---") == 'O');
    CHECK(symbolToChar(".--.") == 'P');
    CHECK(charToSymbol('R') == ".-.");
    CHECK(symbolToChar("......." ) == '\0');

    // Decodifica end-to-end a 20 WPM (punto = 60 ms), tono 600 Hz, 8 kHz.
    {
        const double rate = 8000.0;
        auto audio = synthesize("SOS SOS", rate, 60.0, 600.0);

        CwDecoder dec(rate);
        dec.processAudio(audio.data(), audio.size());
        dec.flush();

        CHECK(dec.text().find("SOS") != std::string::npos);
        // Devono esserci due gruppi SOS separati.
        auto first = dec.text().find("SOS");
        CHECK(dec.text().find("SOS", first + 1) != std::string::npos);
        CHECK_NEAR(dec.wpm(), 20.0, 6.0);
    }

    // Velocita' diversa (12 WPM, punto = 100 ms) e pitch diverso.
    {
        const double rate = 8000.0;
        auto audio = synthesize("CQ DX", rate, 100.0, 750.0);

        CwDecoder dec(rate);
        dec.processAudio(audio.data(), audio.size());
        dec.flush();
        CHECK(dec.text().find("CQ") != std::string::npos);
        CHECK(dec.text().find("DX") != std::string::npos);
    }

    return testResult("test_morse");
}
