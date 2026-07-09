#include "sdrjo/util/bmp_writer.hpp"

#include <cstdio>
#include <cstring>
#include <vector>

namespace sdrjo {

static void put16(std::vector<uint8_t>& v, uint16_t x)
{
    v.push_back(uint8_t(x)); v.push_back(uint8_t(x >> 8));
}
static void put32(std::vector<uint8_t>& v, uint32_t x)
{
    for (int i = 0; i < 4; i++) v.push_back(uint8_t(x >> (8 * i)));
}

bool writeGrayscaleBmp(const std::string& path, const uint8_t* data,
                       int width, int height)
{
    if (width <= 0 || height <= 0) return false;

    const int stride = (width + 3) & ~3; // righe allineate a 4 byte
    const uint32_t paletteSize = 256 * 4;
    const uint32_t headerSize = 14 + 40 + paletteSize;
    const uint32_t imageSize = uint32_t(stride) * uint32_t(height);

    std::vector<uint8_t> h;
    h.reserve(headerSize);
    // BITMAPFILEHEADER
    h.push_back('B'); h.push_back('M');
    put32(h, headerSize + imageSize);
    put32(h, 0);
    put32(h, headerSize);
    // BITMAPINFOHEADER
    put32(h, 40);
    put32(h, uint32_t(width));
    put32(h, uint32_t(height)); // positivo = bottom-up
    put16(h, 1);
    put16(h, 8);
    put32(h, 0);                // BI_RGB
    put32(h, imageSize);
    put32(h, 2835); put32(h, 2835);
    put32(h, 256); put32(h, 0);
    // Palette in scala di grigi.
    for (int i = 0; i < 256; i++) {
        h.push_back(uint8_t(i)); h.push_back(uint8_t(i));
        h.push_back(uint8_t(i)); h.push_back(0);
    }

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(h.data(), 1, h.size(), f) == h.size();

    std::vector<uint8_t> row(size_t(stride), 0);
    for (int y = height - 1; y >= 0 && ok; y--) { // bottom-up
        std::memcpy(row.data(), data + size_t(y) * width, size_t(width));
        ok = std::fwrite(row.data(), 1, row.size(), f) == row.size();
    }
    std::fclose(f);
    return ok;
}

} // namespace sdrjo
