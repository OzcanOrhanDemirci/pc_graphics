#version 460 core
// Scene 8 · the rendered picture to the window's size, top row first (the interface kit's convention): Catmull-Rom
// when enlarging, an area-weighted tent as wide as the reduction when shrinking (4K or 8K shown on a 1440p screen is
// supersampling, not a blur).
in vec2 vUV;
layout(binding = 0) uniform sampler2D src;   // display-encoded, nearest filtering
uniform ivec2 srcSize, dstSize;
layout(location = 0) out vec4 outColor;

float cr(float x) {   // Catmull-Rom
    x = abs(x);
    if (x < 1.0) return 1.5 * x * x * x - 2.5 * x * x + 1.0;
    if (x < 2.0) return -0.5 * x * x * x + 2.5 * x * x - 4.0 * x + 2.0;
    return 0.0;
}

void main() {
    // the output pixel's centre counted from the picture's top: output row 0 (texture v = 0) is the picture's top
    vec2 o = gl_FragCoord.xy;
    vec2 sc = vec2(srcSize) / vec2(dstSize);
    vec2 c = o * sc;                         // in source pixels, from the top
    c.y = float(srcSize.y) - c.y;           // source rows count from the bottom
    if (srcSize == dstSize) {
        outColor = texelFetch(src, ivec2(c), 0);
        return;
    }
    vec2 rad = max(sc, vec2(1.0));          // the filter's width in source pixels
    bool shrink = sc.x > 1.0 || sc.y > 1.0;
    ivec2 lo = ivec2(floor(c - rad * (shrink ? 1.0 : 2.0))), hi = ivec2(ceil(c + rad * (shrink ? 1.0 : 2.0)));
    vec4 sum = vec4(0);
    float wsum = 0.0;
    for (int y = lo.y; y <= hi.y; ++y)
        for (int x = lo.x; x <= hi.x; ++x) {
            vec2 d = (vec2(x, y) + 0.5 - c) / rad;
            float w = shrink ? max(0.0, 1.0 - abs(d.x)) * max(0.0, 1.0 - abs(d.y)) : cr(d.x) * cr(d.y);
            if (w == 0.0) continue;
            ivec2 q = clamp(ivec2(x, y), ivec2(0), srcSize - 1);
            sum += w * texelFetch(src, q, 0);
            wsum += w;
        }
    outColor = sum / max(wsum, 1e-6);
}
