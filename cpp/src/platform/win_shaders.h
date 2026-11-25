// Crit Launcher Native — HLSL for the DirectX 11 renderer (compiled at runtime by d3dcompiler_47).
#pragma once

namespace crit {

static const char* kShaderSource = R"HLSL(
struct VSIn {
    float2 pos   : POS;
    float2 uv    : UVC;
    float4 color : COL;
    float2 local : LOCAL;
    float2 ext   : EXT;
    float2 rp    : RP;
};

struct PSIn {
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : COLOR0;
    float2 local : TEXCOORD1;
    float2 ext   : TEXCOORD2;
    float2 rp    : TEXCOORD3;
};

cbuffer Params : register(b0) {
    float4 g_params;  // x = shape type (0 fill, 1 shadow, 2 border)
    float4 g_screen;  // xy = viewport size in pixels, zw = unused
};

Texture2D    g_tex : register(t0);
SamplerState g_smp : register(s0);

// Canvas emits device-pixel coordinates with y growing downward; SV_Position
// wants clip space (-1..1, y up). Without this the quads were drawn thousands
// of units off-screen and the window came up completely black.
float2 PixelToClip(float2 px, float2 screen) {
    float2 ndc = px / screen;          // 0..1
    ndc.y = 1.0 - ndc.y;              // flip to y-up
    return ndc * 2.0 - 1.0;           // 0..1 -> -1..1
}

PSIn VSMain(VSIn i) {
    PSIn o;
    float2 screen = max(g_screen.xy, float2(1.0, 1.0));
    o.pos = float4(PixelToClip(i.pos, screen), 0.0, 1.0);
    o.uv = i.uv;
    o.color = i.color;
    o.local = i.local;
    o.ext = i.ext;
    o.rp = i.rp;
    return o;
}

float4 PSImage(PSIn i) : SV_Target {
    float4 c = g_tex.Sample(g_smp, i.uv);
    // PREMULTIPLIED output, matching the ONE / INV_SRC_ALPHA blend state.
    // Multiplying rgb by alpha here is what makes transparent texels contribute
    // nothing; without it a transparent white texel adds full white (solid
    // squares where glyph icons should be, white veils over the panels).
    float4 o = c * i.color;
    o.rgb *= o.a;
    return o;
}

float4 PSText(PSIn i) : SV_Target {
    float a = g_tex.Sample(g_smp, i.uv).a * i.color.a;
    // premultiplied (see PSImage)
    return float4(i.color.rgb * a, a);
}

// exact sdf for a rounded box centered on the pixel
float sdRoundBox(float2 p, float2 b, float r) {
    float2 q = abs(p) - b + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float4 PSShape(PSIn i) : SV_Target {
    float2 p = (i.local * 2.0 - 1.0) * i.ext;
    float d = sdRoundBox(p, i.ext, i.rp.x);
    float cov;
    if (g_params.x < 0.5) {
        cov = saturate(0.5 - d);
    } else if (g_params.x < 1.5) {
        // shadow: gaussian falloff from the shape edge
        float blur = max(i.rp.y, 0.0001);
        float t = saturate(-d / blur + 0.35);
        cov = t * t * (3.0 - 2.0 * t);
    } else {
        float w = max(i.rp.y, 0.5);
        cov = saturate(0.5 - (abs(d + w * 0.5) - w * 0.5));
    }
    // premultiplied (see PSImage)
    float a = i.color.a * cov;
    return float4(i.color.rgb * a, a);
}
)HLSL";

}  // namespace crit
