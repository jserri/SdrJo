#include "sdrjo/sensors/nexus_decoder.hpp"

namespace sdrjo::sensors {

void NexusDecoder::onPulse(const Pulse& p)
{
    // Impulso valido: ~500 us (tolleranza larga).
    if (p.highUs < 300.0 || p.highUs > 800.0) {
        flush();
        return;
    }
    if (p.gapUs > 700.0 && p.gapUs < 1400.0) {
        bits_ = (bits_ << 1);
        nBits_++;
    } else if (p.gapUs >= 1600.0 && p.gapUs < 2600.0) {
        bits_ = (bits_ << 1) | 1;
        nBits_++;
    } else {
        // Pausa di sync (~4 ms) o fine trasmissione: chiudi la trama.
        // L'ULTIMO bit della trama sta prima della pausa lunga, quindi
        // il suo valore non e' codificato: le trame Nexus reali chiudono
        // con la pausa del bit, percio' qui arriva gia' completa.
        flush();
        return;
    }
    if (nBits_ >= 36) flush();
}

void NexusDecoder::flush()
{
    if (nBits_ == 36) {
        uint64_t b = bits_;
        NexusReading r;
        r.id = uint8_t((b >> 28) & 0xFF);
        r.batteryOk = ((b >> 27) & 1) != 0;
        r.channel = int((b >> 24) & 0x3) + 1;
        int t = int((b >> 12) & 0xFFF);
        if (t & 0x800) t -= 0x1000; // complemento a due, 12 bit
        r.tempC = t / 10.0;
        int marker = int((b >> 8) & 0xF);
        r.humidity = int(b & 0xFF);
        // La costante 1111 e l'umidita' sensata filtrano il rumore.
        if (marker == 0xF && r.humidity <= 100 && r.tempC > -50.0 &&
            r.tempC < 70.0 && cb_)
            cb_(r);
    }
    bits_ = 0;
    nBits_ = 0;
}

} // namespace sdrjo::sensors
