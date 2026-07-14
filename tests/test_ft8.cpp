// Test del decoder FT8: verifica la compatibilita' col protocollo (i toni e i
// bit devono combaciare bit-per-bit coi vettori di riferimento) e la catena di
// decodifica su audio sintetico GFSK con rumore.
#include "sdrjo/dsp/ft8.hpp"
#include "test_util.hpp"
#include "ft8_vectors.hpp"
#include "ft4_vectors.hpp"

#include <cmath>
#include <random>
#include <string>
#include <vector>

using namespace sdrjo::dsp;

// Sintesi GFSK come in WSJT-X (impulso gaussiano, BT=2), per un test realistico.
static std::vector<float> gfskAudio(const int tones[79], double f0, double fs,
                                    double snrDb, int seed)
{
    const double bt = 2.0;
    const double twoPi = 6.283185307179586;
    int nsps = int(std::lround(fs * 0.16));
    int nsym = 79;
    // Impulso gaussiano lungo 3 simboli.
    std::vector<double> pulse(3 * nsps);
    double c = M_PI * std::sqrt(2.0 / std::log(2.0));
    for (int i = 0; i < 3 * nsps; i++) {
        double t = (i + 1 - 1.5 * nsps) / double(nsps);
        pulse[i] = 0.5 * (std::erf(c * bt * (t + 0.5)) - std::erf(c * bt * (t - 0.5)));
    }
    // Frequenza istantanea (dphi) accumulando gli impulsi per tono.
    std::vector<double> dphi((nsym + 2) * nsps, 0.0);
    double dphiPeak = twoPi / nsps;
    for (int j = 0; j < nsym; j++) {
        int ib = j * nsps;
        for (int i = 0; i < 3 * nsps; i++)
            dphi[ib + i] += dphiPeak * pulse[i] * tones[j];
    }
    for (int i = 0; i < 2 * nsps; i++) {
        dphi[i] += dphiPeak * tones[0] * pulse[nsps + i];
        dphi[nsym * nsps + i] += dphiPeak * tones[nsym - 1] * pulse[i];
    }
    for (auto& d : dphi) d += twoPi * f0 / fs;
    // Fase e forma d'onda (salta il primo simbolo fittizio).
    std::vector<float> wave(size_t(nsym) * nsps);
    double phi = 0.0;
    for (size_t i = 0; i < wave.size(); i++) {
        phi += dphi[nsps + i];
        wave[i] = float(std::sin(phi));
    }
    // Inserisci in una finestra da 15 s a 0.5 s, aggiungi rumore bianco.
    size_t total = size_t(15.0 * fs);
    std::vector<float> out(total, 0.0f);
    size_t start = size_t(0.5 * fs);
    // Ampiezza segnale/rumore da SNR in banda 2500 Hz (convenzione FT8).
    double sigRms = 1.0 / std::sqrt(2.0);
    double noiseRms = sigRms / std::pow(10.0, snrDb / 20.0) *
                      std::sqrt(2500.0 / (fs / 2.0));
    std::mt19937 rng(seed);
    std::normal_distribution<double> nd(0.0, noiseRms);
    for (size_t i = 0; i < total; i++) {
        double v = nd(rng);
        if (i >= start && i - start < wave.size()) v += wave[i - start];
        out[i] = float(v);
    }
    return out;
}

int main()
{
    // 1) Compatibilita' col protocollo: pack/unpack/tones bit-per-bit.
    for (const auto& v : kFt8Vectors) {
        uint8_t bits[77];
        bool okp = ft8::pack77(v.msg, bits);
        CHECK(okp);
        std::string mine;
        for (int i = 0; i < 77; i++) mine += char('0' + bits[i]);
        CHECK(mine == std::string(v.bits));

        // unpack dai bit di riferimento deve dare il testo atteso
        uint8_t ref[77];
        for (int i = 0; i < 77; i++) ref[i] = uint8_t(v.bits[i] - '0');
        std::string txt;
        bool oku = ft8::unpack77(ref, txt);
        CHECK(oku);
        CHECK(txt == std::string(v.back));

        // toni dal payload
        int tones[79];
        ft8::tonesFromBits(ref, tones);
        std::string tstr;
        for (int i = 0; i < 79; i++) tstr += char('0' + tones[i]);
        CHECK(tstr == std::string(v.tones));
    }

    // 2) CRC/LDPC: una codeword generata deve rispettare tutti i controlli e
    //    il decoder BP su LLR "puliti" deve tornare al payload.
    {
        uint8_t ref[77];
        for (int i = 0; i < 77; i++) ref[i] = uint8_t(kFt8Vectors[0].bits[i] - '0');
        uint8_t cw[174];
        ft8::encode174(ref, cw);
        float llr[174];
        for (int i = 0; i < 174; i++) llr[i] = cw[i] ? 6.0f : -6.0f;
        uint8_t dec[77];
        bool ok = ft8::bpDecode(llr, dec, 30);
        CHECK(ok);
        bool same = true;
        for (int i = 0; i < 77; i++) if (dec[i] != ref[i]) same = false;
        CHECK(same);
    }

    // 3) Catena completa: audio GFSK + rumore -> decodeAudio trova il messaggio.
    {
        int found = 0;
        for (const auto& v : kFt8Vectors) {
            uint8_t ref[77];
            for (int i = 0; i < 77; i++) ref[i] = uint8_t(v.bits[i] - '0');
            int tones[79];
            ft8::tonesFromBits(ref, tones);
            auto audio = gfskAudio(tones, 1200.0, 12000.0, 3.0, 12345);
            auto res = ft8::decodeAudio(audio.data(), audio.size(), 12000.0,
                                        200.0, 3000.0);
            bool got = false;
            for (const auto& d : res)
                if (d.message == std::string(v.back)) got = true;
            if (got) found++;
            else std::printf("  (non decodificato: %s)\n", v.msg);
        }
        std::printf("  decodificati %d/%d messaggi GFSK a 3 dB\n", found,
                    int(kFt8Vectors.size()));
        // A SNR alto (+3 dB) devono uscire tutti.
        CHECK(found == int(kFt8Vectors.size()));
    }

    // 4) Due segnali sovrapposti in frequenza nella stessa finestra.
    {
        uint8_t r0[77], r1[77];
        for (int i = 0; i < 77; i++) r0[i] = uint8_t(kFt8Vectors[0].bits[i] - '0');
        for (int i = 0; i < 77; i++) r1[i] = uint8_t(kFt8Vectors[2].bits[i] - '0');
        int t0[79], t1[79];
        ft8::tonesFromBits(r0, t0);
        ft8::tonesFromBits(r1, t1);
        auto a0 = gfskAudio(t0, 800.0, 12000.0, 6.0, 1);
        auto a1 = gfskAudio(t1, 1600.0, 12000.0, 6.0, 2);
        for (size_t i = 0; i < a0.size(); i++) a0[i] += a1[i];
        auto res = ft8::decodeAudio(a0.data(), a0.size(), 12000.0, 200.0, 3000.0);
        bool g0 = false, g1 = false;
        for (const auto& d : res) {
            if (d.message == std::string(kFt8Vectors[0].back)) g0 = true;
            if (d.message == std::string(kFt8Vectors[2].back)) g1 = true;
        }
        CHECK(g0);
        CHECK(g1);
    }

    // 5) OSD: deve recuperare una codeword anche con qualche errore forte
    //    (LLR di segno sbagliato) dove il solo hard-decision fallirebbe.
    {
        uint8_t ref[77];
        for (int i = 0; i < 77; i++) ref[i] = uint8_t(kFt8Vectors[1].bits[i] - '0');
        uint8_t cw[174];
        ft8::encode174(ref, cw);
        float llr[174];
        for (int i = 0; i < 174; i++) llr[i] = cw[i] ? 3.0f : -3.0f;
        // Errori realistici: pochi bit a bassa affidabilita' con segno
        // sbagliato (e' lo scenario che l'OSD sa recuperare, ordinando per
        // affidabilita' e ricodificando dai bit piu' sicuri).
        for (int i = 0; i < 6; i++) llr[i * 23] = cw[i * 23] ? -0.2f : 0.2f;
        uint8_t dec[77];
        bool ok = ft8::osdDecode(llr, dec, 2);
        CHECK(ok);
        bool same = true;
        for (int i = 0; i < 77; i++) if (dec[i] != ref[i]) same = false;
        CHECK(same);
    }

    // 6) FT4: compatibilita' col protocollo (pack/unpack/toni bit-per-bit).
    for (const auto& v : kFt4Vectors) {
        uint8_t bits[77];
        CHECK(ft8::pack77(v.msg, bits));
        std::string mine;
        for (int i = 0; i < 77; i++) mine += char('0' + bits[i]);
        CHECK(mine == std::string(v.bits));

        uint8_t ref[77];
        for (int i = 0; i < 77; i++) ref[i] = uint8_t(v.bits[i] - '0');
        int tones[103];
        ft8::ft4TonesFromBits(ref, tones);
        std::string tstr;
        for (int i = 0; i < 103; i++) tstr += char('0' + tones[i]);
        CHECK(tstr == std::string(v.tones));
    }

    // 7) FT4: catena completa su audio 4-FSK + rumore in una finestra 7.5 s.
    {
        int found = 0;
        for (const auto& v : kFt4Vectors) {
            auto wave = ft8::encodeAudioFt4(v.msg, 1200.0, 12000.0);
            size_t total = size_t(7.5 * 12000.0);
            std::vector<float> audio(total, 0.0f);
            size_t start = size_t(0.5 * 12000.0);
            std::mt19937 rng(777);
            std::normal_distribution<double> nd(0.0, 0.15);
            for (size_t i = 0; i < total; i++) {
                double x = nd(rng);
                if (i >= start && i - start < wave.size()) x += wave[i - start];
                audio[i] = float(x);
            }
            auto res = ft8::decodeAudioFt4(audio.data(), audio.size(), 12000.0,
                                           200.0, 3000.0);
            bool got = false;
            for (const auto& d : res)
                if (d.message == std::string(v.back)) got = true;
            if (got) found++;
            else std::printf("  (FT4 non decodificato: %s)\n", v.msg);
        }
        std::printf("  decodificati %d/%d messaggi FT4\n", found,
                    int(kFt4Vectors.size()));
        CHECK(found == int(kFt4Vectors.size()));
    }

    return testResult("ft8");
}
