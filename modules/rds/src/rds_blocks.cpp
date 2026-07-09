#include "sdrjo/rds/rds_blocks.hpp"

#include <cstring>

namespace sdrjo::rds {

uint16_t checkword(uint16_t info)
{
    // Divisione polinomiale: m(x) * x^10 mod g(x), g = 0x5B9 (11 bit).
    uint32_t reg = uint32_t(info) << 10;
    for (int bit = 25; bit >= 10; bit--) {
        if (reg & (1u << bit))
            reg ^= 0x5B9u << (bit - 10);
    }
    return uint16_t(reg & 0x3FF);
}

uint32_t encodeBlock(uint16_t info, uint16_t offset)
{
    return (uint32_t(info) << 10) | (checkword(info) ^ offset);
}

RdsDecoder::RdsDecoder(GroupCallback onGroup) : onGroup_(std::move(onGroup)) {}

// Verifica se gli ultimi 26 bit formano un blocco valido per l'offset dato.
static bool blockValid(uint32_t bits, uint16_t offset, uint16_t& infoOut)
{
    uint16_t info = uint16_t((bits >> 10) & 0xFFFF);
    uint16_t crc = uint16_t(bits & 0x3FF);
    if ((checkword(info) ^ offset) != crc) return false;
    infoOut = info;
    return true;
}

void RdsDecoder::pushBit(uint8_t bit)
{
    shift_ = ((shift_ << 1) | (bit & 1)) & 0x3FFFFFF;
    bitCount_++;

    static const uint16_t kOffsets[4] = {kOffsetA, kOffsetB, kOffsetC, kOffsetD};

    if (!info_.synced) {
        // Ricerca: un blocco A valido in qualunque posizione fa partire
        // la sincronizzazione.
        uint16_t info;
        if (bitCount_ >= 26 && blockValid(shift_, kOffsetA, info)) {
            group_[0] = info;
            expectedBlock_ = 1;
            bitCount_ = 0;
            info_.synced = true;
            badBlocks_ = 0;
        }
        return;
    }

    if (bitCount_ < 26) return;
    bitCount_ = 0;

    uint16_t info;
    bool ok = blockValid(shift_, kOffsets[expectedBlock_], info);
    // Il blocco C dei gruppi versione B usa l'offset C'.
    if (!ok && expectedBlock_ == 2)
        ok = blockValid(shift_, kOffsetCprime, info);

    if (!ok) {
        info_.blockErrors++;
        if (++badBlocks_ > 8) {           // troppi errori: riparti da capo
            info_.synced = false;
            expectedBlock_ = 0;
            bitCount_ = 0;
        } else {
            expectedBlock_ = (expectedBlock_ + 1) % 4; // salta il blocco
        }
        return;
    }

    badBlocks_ = 0;
    onBlock(info, expectedBlock_);
    expectedBlock_ = (expectedBlock_ + 1) % 4;
}

void RdsDecoder::onBlock(uint16_t data, int blockIndex)
{
    group_[blockIndex] = data;
    if (blockIndex == 3) decodeGroup();
}

void RdsDecoder::decodeGroup()
{
    info_.groupCount++;
    if (onGroup_) onGroup_(group_[0], group_[1], group_[2], group_[3]);

    info_.pi = group_[0];
    int groupType = (group_[1] >> 12) & 0xF;
    bool versionB = (group_[1] >> 11) & 1;
    info_.tp = (group_[1] >> 10) & 1;
    info_.pty = (group_[1] >> 5) & 0x1F;

    if (groupType == 0) {
        // Gruppo 0A/0B: due caratteri del PS per segmento (0..3).
        int seg = group_[1] & 0x3;
        info_.ps[seg * 2] = char((group_[3] >> 8) & 0xFF);
        info_.ps[seg * 2 + 1] = char(group_[3] & 0xFF);
    } else if (groupType == 2) {
        // Gruppo 2A: 4 caratteri di RadioText per segmento (0..15).
        int seg = group_[1] & 0xF;
        if (!versionB) {
            info_.radioText[seg * 4]     = char((group_[2] >> 8) & 0xFF);
            info_.radioText[seg * 4 + 1] = char(group_[2] & 0xFF);
            info_.radioText[seg * 4 + 2] = char((group_[3] >> 8) & 0xFF);
            info_.radioText[seg * 4 + 3] = char(group_[3] & 0xFF);
        } else {
            info_.radioText[seg * 2]     = char((group_[3] >> 8) & 0xFF);
            info_.radioText[seg * 2 + 1] = char(group_[3] & 0xFF);
        }
    }
}

} // namespace sdrjo::rds
