#include "sdrjo/lrpt/ccsds.hpp"

namespace sdrjo::lrpt {

std::vector<uint8_t> pnSequence(size_t numBytes)
{
    std::vector<uint8_t> out(numBytes);
    uint8_t state = 0xFF;
    for (size_t i = 0; i < numBytes; i++) {
        uint8_t byte = 0;
        for (int b = 0; b < 8; b++) {
            uint8_t outBit = (state >> 7) & 1;
            byte = uint8_t((byte << 1) | outBit);
            // x^8 + x^7 + x^5 + x^3 + 1
            uint8_t fb = ((state >> 7) ^ (state >> 6) ^ (state >> 4) ^ (state >> 2)) & 1;
            state = uint8_t((state << 1) | fb);
        }
        out[i] = byte;
    }
    return out;
}

void derandomize(uint8_t* data, size_t numBytes)
{
    static const std::vector<uint8_t> pn = pnSequence(kCaduDataBytes);
    for (size_t i = 0; i < numBytes; i++)
        data[i] ^= pn[i % pn.size()];
}

std::optional<size_t> findSync(const std::vector<uint8_t>& bits, bool& inverted)
{
    if (bits.size() < 32) return std::nullopt;
    for (size_t off = 0; off + 32 <= bits.size(); off++) {
        uint32_t w = 0;
        for (int b = 0; b < 32; b++) w = (w << 1) | (bits[off + b] & 1);
        if (w == kSyncWord) { inverted = false; return off; }
        if (w == ~kSyncWord) { inverted = true; return off; }
    }
    return std::nullopt;
}

std::vector<uint8_t> packBits(const std::vector<uint8_t>& bits, size_t startBit,
                              size_t numBits)
{
    std::vector<uint8_t> out((numBits + 7) / 8, 0);
    for (size_t i = 0; i < numBits && startBit + i < bits.size(); i++) {
        if (bits[startBit + i] & 1)
            out[i / 8] |= uint8_t(0x80 >> (i % 8));
    }
    return out;
}

} // namespace sdrjo::lrpt
