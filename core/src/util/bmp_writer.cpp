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

bool writeRgbBmp(const std::string& path, const uint8_t* rgb, int width,
                 int height, bool bottomUp)
{
    if (width <= 0 || height <= 0) return false;
    const int stride = (width * 3 + 3) & ~3; // righe allineate a 4 byte
    const uint32_t headerSize = 14 + 40;
    const uint32_t imageSize = uint32_t(stride) * uint32_t(height);

    std::vector<uint8_t> h;
    h.push_back('B'); h.push_back('M');
    put32(h, headerSize + imageSize);
    put32(h, 0);
    put32(h, headerSize);
    put32(h, 40);
    put32(h, uint32_t(width));
    put32(h, uint32_t(height)); // positivo = bottom-up nel file
    put16(h, 1);
    put16(h, 24);
    put32(h, 0);
    put32(h, imageSize);
    put32(h, 2835); put32(h, 2835);
    put32(h, 0); put32(h, 0);

    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(h.data(), 1, h.size(), f) == h.size();

    // Il BMP e' bottom-up: scriviamo dall'ultima riga "in alto" alla prima.
    std::vector<uint8_t> row(size_t(stride), 0);
    for (int y = 0; y < height && ok; y++) {
        // Riga sorgente: se i dati sono top-down prendiamo height-1-y.
        int sy = bottomUp ? y : (height - 1 - y);
        const uint8_t* src = rgb + size_t(sy) * size_t(width) * 3;
        for (int x = 0; x < width; x++) {
            row[size_t(x) * 3 + 0] = src[x * 3 + 2]; // B
            row[size_t(x) * 3 + 1] = src[x * 3 + 1]; // G
            row[size_t(x) * 3 + 2] = src[x * 3 + 0]; // R
        }
        ok = std::fwrite(row.data(), 1, row.size(), f) == row.size();
    }
    std::fclose(f);
    return ok;
}

} // namespace sdrjo
