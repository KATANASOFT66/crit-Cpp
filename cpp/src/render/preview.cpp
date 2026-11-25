// Crit Launcher Native — software rasterizer + PNG io for the preview tool.
#include "render/preview.h"

#include "core/pngread.h"
#include "core/zip.h"
#include "ui/ui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace crit {

// no GPU in the preview build: uploads are a no-op
void GpuUploadTexture(Texture&) {}
void GpuReleaseTexture(Texture&) {}

namespace {

inline float Frac(float v) { return v - std::floor(v); }

// ---- SDF for a rounded box centered at the origin
inline float SdRoundBox(float px, float py, float hw, float hh, float r) {
    float qx = std::fabs(px) - hw + r;
    float qy = std::fabs(py) - hh + r;
    float ax = std::max(qx, 0.f), ay = std::max(qy, 0.f);
    return std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.f) - r;
}

inline float ShapeCoverage(const Vertex& v0, const Vertex& v1, const Vertex& v2, float fx, float fy, int shapeType,
                           float param) {
    // interpolate local coords + extents from the triangle
    float d0 = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
    if (std::fabs(d0) < 1e-9f) return 0.f;
    float w0 = ((v1.x - fx) * (v2.y - fy) - (v2.x - fx) * (v1.y - fy)) / d0;
    float w1 = ((v2.x - fx) * (v0.y - fy) - (v0.x - fx) * (v2.y - fy)) / d0;
    float w2 = 1.f - w0 - w1;
    if (w0 < -0.02f || w1 < -0.02f || w2 < -0.02f) return 0.f;
    float lx = w0 * v0.lx + w1 * v1.lx + w2 * v2.lx;
    float ly = w0 * v0.ly + w1 * v1.ly + w2 * v2.ly;
    float hw = w0 * v0.hw + w1 * v1.hw + w2 * v2.hw;
    float hh = w0 * v0.hh + w1 * v1.hh + w2 * v2.hh;
    float radius = w0 * v0.radius + w1 * v1.radius + w2 * v2.radius;
    float px = lx * 2.f * hw - hw;
    float py = ly * 2.f * hh - hh;
    float d = SdRoundBox(px, py, std::max(hw, 0.001f), std::max(hh, 0.001f), std::max(radius, 0.f));
    if (shapeType == 1) {
        // soft shadow: spread blend
        if (param <= 0.01f) return d < 0.f ? 1.f : 0.f;
        float t = clamp01(-d / param + 0.35f);
        return t * t * (3.f - 2.f * t);
    }
    if (shapeType == 2) {
        float w = std::max(param, 0.5f);
        return clamp01(0.5f - (std::fabs(d + w * 0.5f) - w * 0.5f));
    }
    return clamp01(0.5f - d);
}

}  // namespace

void Rasterize(Canvas& canvas, RasterTarget& target) {
    if (target.w <= 0 || target.h <= 0) return;
    target.rgba.assign((size_t)target.w * target.h * 4, 0);
    // background: theme bg (everything draws over it)
    const Theme& th = GetTheme();
    for (size_t i = 0; i < (size_t)target.w * target.h; i++) {
        target.rgba[i * 4 + 0] = (uint8_t)std::lround(th.bg.r * 255.f);
        target.rgba[i * 4 + 1] = (uint8_t)std::lround(th.bg.g * 255.f);
        target.rgba[i * 4 + 2] = (uint8_t)std::lround(th.bg.b * 255.f);
        target.rgba[i * 4 + 3] = 255;
    }
    for (const Batch& b : canvas.batches) {
        if (b.indexCount == 0) continue;
        int x0 = std::max(0, (int)std::floor(b.clip.x));
        int y0 = std::max(0, (int)std::floor(b.clip.y));
        int x1 = std::min(target.w, (int)std::ceil(b.clip.r()));
        int y1 = std::min(target.h, (int)std::ceil(b.clip.b()));
        if (b.clip.w <= 0.f || b.clip.h <= 0.f) continue;
        Texture* tex = b.texture >= 0 ? canvas.textures->Get(b.texture) : nullptr;
        for (uint32_t ii = b.firstIndex; ii + 2 < b.firstIndex + b.indexCount + 1 && ii + 2 < canvas.indices.size(); ii += 3) {
            const Vertex& v0 = canvas.verts[canvas.indices[ii]];
            const Vertex& v1 = canvas.verts[canvas.indices[ii + 1]];
            const Vertex& v2 = canvas.verts[canvas.indices[ii + 2]];
            float minX = std::min({v0.x, v1.x, v2.x}), maxX = std::max({v0.x, v1.x, v2.x});
            float minY = std::min({v0.y, v1.y, v2.y}), maxY = std::max({v0.y, v1.y, v2.y});
            int ix0 = std::max(x0, (int)std::floor(minX) - 1), ix1 = std::min(x1, (int)std::ceil(maxX) + 1);
            int iy0 = std::max(y0, (int)std::floor(minY) - 1), iy1 = std::min(y1, (int)std::ceil(maxY) + 1);
            float d0 = (v1.x - v0.x) * (v2.y - v0.y) - (v2.x - v0.x) * (v1.y - v0.y);
            if (std::fabs(d0) < 1e-9f) continue;
            if (b.kind == BatchKind::Sdf && b.shapeType == 0 && v0.hw == 0.f && v0.hh == 0.f) {
                // fast path: solid gradient/no-SDF quad
            }
            for (int y = iy0; y < iy1; y++) {
                for (int x = ix0; x < ix1; x++) {
                    float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
                    float b0 = ((v1.x - fx) * (v2.y - fy) - (v2.x - fx) * (v1.y - fy)) / d0;
                    float b1 = ((v2.x - fx) * (v0.y - fy) - (v0.x - fx) * (v2.y - fy)) / d0;
                    float b2 = 1.f - b0 - b1;
                    if (b0 < 0.f || b1 < 0.f || b2 < 0.f) continue;
                    float srcA = b0 * v0.a + b1 * v1.a + b2 * v2.a;
                    float sr = b0 * v0.r + b1 * v1.r + b2 * v2.r;
                    float sg = b0 * v0.g + b1 * v1.g + b2 * v2.g;
                    float sb = b0 * v0.b + b1 * v2.b + b2 * v2.b;
                    sb = b0 * v0.b + b1 * v1.b + b2 * v2.b;
                    if (b.kind == BatchKind::Sdf) {
                        float cov = ShapeCoverage(v0, v1, v2, fx, fy, b.shapeType, v0.param);
                        srcA *= cov;
                    } else if (b.kind == BatchKind::Text) {
                        if (!tex) continue;
                        float u = b0 * v0.u + b1 * v1.u + b2 * v2.u;
                        float v = b0 * v0.v + b1 * v1.v + b2 * v2.v;
                        srcA *= tex->SampleAlpha(u, v) / 255.f;
                    } else if (b.kind == BatchKind::Image) {
                        if (!tex) continue;
                        float u = b0 * v0.u + b1 * v1.u + b2 * v2.u;
                        float v = b0 * v0.v + b1 * v1.v + b2 * v2.v;
                        uint32_t c = tex->SampleRgba(u, v);
                        float tr = (float)((c >> 0) & 0xff) / 255.f;
                        float tg = (float)((c >> 8) & 0xff) / 255.f;
                        float tb = (float)((c >> 16) & 0xff) / 255.f;
                        float ta = (float)((c >> 24) & 0xff) / 255.f;
                        sr *= tr;
                        sg *= tg;
                        sb *= tb;
                        srcA *= ta;
                    }
                    if (srcA <= 0.002f) continue;
                    srcA = std::min(srcA, 1.f);
                    uint8_t* dst = &target.rgba[((size_t)y * target.w + x) * 4];
                    dst[0] = (uint8_t)std::lround(sr * 255.f * srcA + dst[0] * (1.f - srcA));
                    dst[1] = (uint8_t)std::lround(sg * 255.f * srcA + dst[1] * (1.f - srcA));
                    dst[2] = (uint8_t)std::lround(sb * 255.f * srcA + dst[2] * (1.f - srcA));
                    dst[3] = 255;
                }
            }
        }
    }
}

// ---------------------------------------------------------------- PNG
static void PutU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back((uint8_t)(v >> 24));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)v);
}

static uint32_t Crc32(const uint8_t* data, size_t len) {
    static uint32_t table[256];
    static bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xedb88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xffffffffu;
    for (size_t i = 0; i < len; i++) c = table[(c ^ data[i]) & 0xff] ^ (c >> 8);
    return c ^ 0xffffffffu;
}

// minimal deflate: stored blocks (no compression is fine for screenshots)
static void DeflateStore(const std::vector<uint8_t>& raw, std::vector<uint8_t>& out) {
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t len = std::min<size_t>(65535, raw.size() - pos);
        bool last = pos + len >= raw.size();
        out.push_back(last ? 1 : 0);
        out.push_back((uint8_t)(len & 0xff));
        out.push_back((uint8_t)(len >> 8));
        out.push_back((uint8_t)(~len & 0xff));
        out.push_back((uint8_t)((~len >> 8) & 0xff));
        out.insert(out.end(), raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
    }
}

static void WriteChunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    PutU32(out, (uint32_t)data.size());
    std::vector<uint8_t> body(type, type + 4);
    body.insert(body.end(), data.begin(), data.end());
    out.insert(out.end(), body.begin(), body.end());
    PutU32(out, Crc32(body.data(), body.size()));
}

bool PngWrite(const string& path, const RasterTarget& t) {
    std::vector<uint8_t> raw;
    raw.reserve((size_t)t.w * t.h * 4 + t.h);
    for (int y = 0; y < t.h; y++) {
        raw.push_back(0);
        raw.insert(raw.end(), t.rgba.begin() + (size_t)y * t.w * 4, t.rgba.begin() + (size_t)(y + 1) * t.w * 4);
    }
    std::vector<uint8_t> z;
    z.push_back(0x78);
    z.push_back(0x01);
    DeflateStore(raw, z);
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) {
        a = (a + byte) % 65521;
        b = (b + a) % 65521;
    }
    PutU32(z, (b << 16) | a);
    std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    std::vector<uint8_t> ihdr;
    PutU32(ihdr, (uint32_t)t.w);
    PutU32(ihdr, (uint32_t)t.h);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.push_back(0);
    ihdr.push_back(0);
    ihdr.push_back(0);
    WriteChunk(png, "IHDR", ihdr);
    WriteChunk(png, "IDAT", z);
    WriteChunk(png, "IEND", {});
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    fwrite(png.data(), 1, png.size(), f);
    fclose(f);
    return true;
}

static uint32_t ReadU32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

bool PngRead(const string& path, RasterTarget& t) {
    ImageRGBA img;
    if (!PngDecodeFile(path, img)) return false;
    t.w = img.w;
    t.h = img.h;
    t.rgba = std::move(img.rgba);
    return true;
}

}  // namespace crit
