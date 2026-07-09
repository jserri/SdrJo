#pragma once
//
// Strato "bit" dell'RDS (EN 50067): checkword, sincronizzazione dei
// blocchi da 26 bit e decodifica dei gruppi (PI, PTY, PS, RadioText).
//
#include <cstdint>
#include <functional>
#include <string>

namespace sdrjo::rds {

// Checkword a 10 bit: resto di m(x)*x^10 diviso g(x),
// g(x) = x^10 + x^8 + x^7 + x^5 + x^4 + x^3 + 1.
uint16_t checkword(uint16_t info);

// Offset word dei blocchi (da XORare al checkword).
enum Offset : uint16_t {
    kOffsetA = 0x0FC,
    kOffsetB = 0x198,
    kOffsetC = 0x168,
    kOffsetCprime = 0x350,
    kOffsetD = 0x1B4,
};

// Codifica un blocco: 16 bit informativi -> 26 bit (info + crc^offset).
uint32_t encodeBlock(uint16_t info, uint16_t offset);

// Stato della stazione ricostruito dai gruppi.
struct StationInfo {
    uint16_t pi = 0;          // Programme Identification
    int pty = -1;             // Programme Type
    bool tp = false;          // Traffic Programme
    char ps[9] = {0};         // Programme Service name (8 caratteri)
    char radioText[65] = {0}; // RadioText (64 caratteri)
    uint32_t groupCount = 0;
    uint32_t blockErrors = 0;
    bool synced = false;
};

// Decoder di flusso: riceve un bit alla volta (gia' demodulato e
// decodificato differenzialmente), sincronizza i blocchi e aggiorna info.
class RdsDecoder {
public:
    // Callback opzionale a ogni gruppo valido ricevuto.
    using GroupCallback =
        std::function<void(uint16_t a, uint16_t b, uint16_t c, uint16_t d)>;

    explicit RdsDecoder(GroupCallback onGroup = {});

    void pushBit(uint8_t bit);

    const StationInfo& station() const { return info_; }

private:
    void onBlock(uint16_t data, int blockIndex);
    void decodeGroup();

    GroupCallback onGroup_;
    StationInfo info_;

    uint32_t shift_ = 0;   // ultimi 26 bit ricevuti
    int bitCount_ = 0;     // bit accumulati dall'ultimo blocco valido
    int expectedBlock_ = 0;// 0=A 1=B 2=C 3=D; -1 = in ricerca
    int badBlocks_ = 0;
    uint16_t group_[4] = {0};
};

} // namespace sdrjo::rds
