#include "sdrjo/util/adpcm.hpp"

#include <algorithm>

namespace sdrjo {

// Tabelle standard IMA ADPCM.
static const int kStepTable[89] = {
    7,     8,     9,     10,    11,    12,    13,    14,    16,    17,
    19,    21,    23,    25,    28,    31,    34,    37,    41,    45,
    50,    55,    60,    66,    73,    80,    88,    97,    107,   118,
    130,   143,   157,   173,   190,   209,   230,   253,   279,   307,
    337,   371,   408,   449,   494,   544,   598,   658,   724,   796,
    876,   963,   1060,  1166,  1282,  1411,  1552,  1707,  1878,  2066,
    2272,  2499,  2749,  3024,  3327,  3660,  4026,  4428,  4871,  5358,
    5894,  6484,  7132,  7845,  8630,  9493,  10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767};

static const int kIndexTable[16] = {-1, -1, -1, -1, 2, 4, 6, 8,
                                    -1, -1, -1, -1, 2, 4, 6, 8};

static uint8_t encodeSample(int sample, int& predictor, int& index)
{
    int step = kStepTable[index];
    int diff = sample - predictor;
    uint8_t code = 0;
    if (diff < 0) {
        code = 8;
        diff = -diff;
    }
    // Quantizzazione: bit 4/2/1 su step, step/2, step/4.
    int delta = step >> 3;
    if (diff >= step) {
        code |= 4;
        diff -= step;
        delta += step;
    }
    if (diff >= (step >> 1)) {
        code |= 2;
        diff -= step >> 1;
        delta += step >> 1;
    }
    if (diff >= (step >> 2)) {
        code |= 1;
        delta += step >> 2;
    }
    predictor += (code & 8) ? -delta : delta;
    predictor = std::clamp(predictor, -32768, 32767);
    index = std::clamp(index + kIndexTable[code], 0, 88);
    return code;
}

static int decodeSample(uint8_t code, int& predictor, int& index)
{
    int step = kStepTable[index];
    int delta = step >> 3;
    if (code & 4) delta += step;
    if (code & 2) delta += step >> 1;
    if (code & 1) delta += step >> 2;
    predictor += (code & 8) ? -delta : delta;
    predictor = std::clamp(predictor, -32768, 32767);
    index = std::clamp(index + kIndexTable[code & 15], 0, 88);
    return predictor;
}

std::vector<uint8_t> adpcmEncodeBlock(const float* samples, size_t n)
{
    n &= ~size_t(1); // pari
    std::vector<uint8_t> out;
    if (n == 0) return out;
    out.reserve(4 + n / 2);

    int predictor = int(std::clamp(samples[0], -1.0f, 1.0f) * 32767.0f);
    int index = 32; // partenza a meta' scala: converge in pochi campioni
    out.push_back(uint8_t(predictor & 0xFF));
    out.push_back(uint8_t((predictor >> 8) & 0xFF));
    out.push_back(uint8_t(index));
    out.push_back(0);

    for (size_t i = 0; i < n; i += 2) {
        int s0 = int(std::clamp(samples[i], -1.0f, 1.0f) * 32767.0f);
        int s1 = int(std::clamp(samples[i + 1], -1.0f, 1.0f) * 32767.0f);
        uint8_t lo = encodeSample(s0, predictor, index);
        uint8_t hi = encodeSample(s1, predictor, index);
        out.push_back(uint8_t(lo | (hi << 4)));
    }
    return out;
}

std::vector<float> adpcmDecodeBlock(const uint8_t* block, size_t bytes)
{
    std::vector<float> out;
    if (bytes < 5) return out;
    int predictor = int(int16_t(block[0] | (block[1] << 8)));
    int index = std::clamp(int(block[2]), 0, 88);
    out.reserve((bytes - 4) * 2);
    for (size_t i = 4; i < bytes; i++) {
        out.push_back(float(decodeSample(block[i] & 0x0F, predictor, index)) /
                      32767.0f);
        out.push_back(float(decodeSample(block[i] >> 4, predictor, index)) /
                      32767.0f);
    }
    return out;
}

} // namespace sdrjo
