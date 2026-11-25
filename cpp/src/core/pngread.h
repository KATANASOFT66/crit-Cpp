// Crit Launcher Native — minimal PNG decode (8-bit RGB/RGBA, no interlace).
// Decodes straight from bytes, so callers never need a scratch file on disk.
#pragma once

#include "core/base.h"

namespace crit {

struct ImageRGBA {
    int w = 0, h = 0;
    std::vector<uint8_t> rgba;
};

bool PngDecode(const std::vector<uint8_t>& file, ImageRGBA& out);
bool PngDecodeFile(const string& path, ImageRGBA& out);

}  // namespace crit
