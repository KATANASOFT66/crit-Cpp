// Crit Launcher Native — PNG decode built on core/zip inflate.
#include "core/pngread.h"

#include "core/zip.h"
#include "platform/platform.h"  // ReadFileBytes

namespace crit {

// A bare abs() binds to the CRT in one place and to a Windows macro in another
// (and C++ can pick a float overload); keep the Paeth predictor unambiguous.
static inline int IAbs(int v) { return v < 0 ? -v : v; }

static uint32_t Be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

bool PngDecode(const std::vector<uint8_t>& file, ImageRGBA& t) {
    if (file.size() < 8 + 25) return false;
    static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    for (int i = 0; i < 8; i++)
        if (file[(size_t)i] != sig[i]) return false;
    size_t pos = 8;
    int w = 0, h = 0, bd = 0, ct = 0;
    std::vector<uint8_t> idat;
    while (pos + 8 <= file.size()) {
        uint32_t ln = Be32(&file[pos]);
        string type((const char*)&file[pos + 4], 4);
        pos += 8;
        if (pos + ln > file.size()) break;
        if (type == "IHDR" && ln >= 13) {
            w = (int)Be32(&file[pos]);
            h = (int)Be32(&file[pos + 4]);
            bd = file[pos + 8];
            ct = file[pos + 9];
        } else if (type == "IDAT") {
            idat.insert(idat.end(), file.begin() + (long)pos, file.begin() + (long)(pos + ln));
        } else if (type == "IEND") {
            break;
        }
        pos += ln + 4;  // skip data + CRC, pos already past length+type
    }
    if (bd != 8 || (ct != 2 && ct != 6) || w <= 0 || h <= 0 || (int64_t)w * h > 64ll * 1024 * 1024) return false;
    std::vector<uint8_t> raw;
    if (!InflateZlib(idat.data(), idat.size(), raw)) return false;
    const int ch = ct == 6 ? 4 : 3;
    const size_t stride = (size_t)w * (size_t)ch;
    if (raw.size() < (stride + 1) * (size_t)h) return false;
    t.w = w;
    t.h = h;
    t.rgba.assign((size_t)w * (size_t)h * 4, 255);
    std::vector<uint8_t> prev(stride, 0), line(stride);
    const uint8_t* src = raw.data();
    for (int y = 0; y < h; y++) {
        uint8_t filter = *src++;
        memcpy(line.data(), src, stride);
        src += stride;
        if (filter == 1) {
            for (size_t i = (size_t)ch; i < stride; i++) line[i] = (uint8_t)(line[i] + line[i - (size_t)ch]);
        } else if (filter == 2) {
            for (size_t i = 0; i < stride; i++) line[i] = (uint8_t)(line[i] + prev[i]);
        } else if (filter == 3) {
            for (size_t i = 0; i < stride; i++) {
                int a = i >= (size_t)ch ? line[i - (size_t)ch] : 0;
                line[i] = (uint8_t)(line[i] + ((a + prev[i]) >> 1));
            }
        } else if (filter == 4) {
            for (size_t i = 0; i < stride; i++) {
                int a = i >= (size_t)ch ? line[i - (size_t)ch] : 0;
                int b = prev[i];
                int c = i >= (size_t)ch ? prev[i - (size_t)ch] : 0;
                int p = a + b - c, pa = IAbs(p - a), pb = IAbs(p - b), pc = IAbs(p - c);
                int pr = (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                line[i] = (uint8_t)(line[i] + pr);
            }
        }
        for (int x = 0; x < w; x++) {
            uint8_t* dst = &t.rgba[((size_t)y * (size_t)w + (size_t)x) * 4];
            dst[0] = line[(size_t)x * (size_t)ch];
            dst[1] = line[(size_t)x * (size_t)ch + 1];
            dst[2] = line[(size_t)x * (size_t)ch + 2];
            if (ch == 4) dst[3] = line[(size_t)x * (size_t)ch + 3];
        }
        prev = line;
    }
    return true;
}

bool PngDecodeFile(const string& path, ImageRGBA& out) {
    std::vector<uint8_t> bytes = ReadFileBytes(path);
    if (bytes.empty()) return false;
    return PngDecode(bytes, out);
}

}  // namespace crit
