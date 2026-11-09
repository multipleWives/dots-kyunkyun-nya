#pragma once
#include <cmath>
#include <cstdint>

#ifdef __CUDACC__
#define DOT_HD __host__ __device__ __forceinline__
#else
#define DOT_HD inline
#endif

namespace dots {
struct Pixel { float b, g, r, a; };
// Plain, fixed-width ABI shared by the CPU and embedded CUDA kernel.
struct Settings {
    int32_t srcWidth, srcHeight, srcPitch, dstWidth, dstHeight, dstPitch;
    int32_t srcX, srcY, dstX, dstY, channel, response;
    float scaleX, scaleY, pixelAspect, size, gap, offsetX, offsetY;
    float red, green, blue, opacity, alphaBlack, alphaWhite, antialias;
    int32_t repeatEdges;
};
static_assert(sizeof(Settings) == 108, "Kernel ABI changed");
DOT_HD float low(float a, float b) { return a < b ? a : b; }
DOT_HD float high(float a, float b) { return a > b ? a : b; }
DOT_HD float unit(float x) { return x > 0 ? low(x, 1.0f) : 0.0f; }
DOT_HD float alphaLevels(float value,const Settings& s) {
    if(value<=s.alphaBlack) return 0;
    if(value>=s.alphaWhite) return 1;
    return (value-s.alphaBlack)/(s.alphaWhite-s.alphaBlack);
}
DOT_HD Pixel mix(Pixel a, Pixel b, float t) {
    return {a.b+(b.b-a.b)*t, a.g+(b.g-a.g)*t,
            a.r+(b.r-a.r)*t, a.a+(b.a-a.a)*t};
}
template<class Reader> DOT_HD Pixel tap(const Reader& read, const Settings& s, int x, int y) {
    return x>=0 && y>=0 && x<s.srcWidth && y<s.srcHeight ? read(x,y) : Pixel{};
}
template<class Reader> DOT_HD Pixel sample(const Reader& read, const Settings& s, float x, float y) {
    if(s.repeatEdges) {
        if(s.srcWidth<=0 || s.srcHeight<=0) return {};
        x=high(0,low(x,float(s.srcWidth-1)));
        y=high(0,low(y,float(s.srcHeight-1)));
        int ix=int(x),iy=int(y);
        int jx=ix+1<s.srcWidth ? ix+1 : ix;
        int jy=iy+1<s.srcHeight ? iy+1 : iy;
        return mix(mix(read(ix,iy),read(jx,iy),x-ix),mix(read(ix,jy),read(jx,jy),x-ix),y-iy);
    }
    // An AE checkout may be smaller than the output. Outside it is transparent,
    // not a repetition of the last row/column. Zero each bilinear tap separately
    // so fractional samples fade smoothly across all four source boundaries.
    if (s.srcWidth<=0 || s.srcHeight<=0 || x<=-1 || y<=-1 || x>=s.srcWidth || y>=s.srcHeight) return {};
    int ix=int(floorf(x)), iy=int(floorf(y));
    return mix(mix(tap(read,s,ix,iy),tap(read,s,ix+1,iy),x-ix),
               mix(tap(read,s,ix,iy+1),tap(read,s,ix+1,iy+1),x-ix),y-iy);
}
DOT_HD float signal(Pixel p, int channel) {
    if (channel == 3) return 1.0f;
    float a = unit(p.a);
    if (channel == 1) return a;
    // AE worlds are premultiplied. Luma alone is independent of nonzero alpha.
    float luma = a > 1e-8f ? unit((p.r*.2126f+p.g*.7152f+p.b*.0722f)/a) : 0;
    return channel == 2 ? luma*a : luma;
}
template<class Reader> DOT_HD Pixel renderPixel(const Reader& read, const Settings& s, int x, int y) {
    if (s.size <= 0 || s.opacity <= 0) return {};
    float pitch = high(.01f, s.size+s.gap);
    float px = (float(x+s.dstX)+.5f)/s.scaleX*s.pixelAspect;
    float py = (float(y+s.dstY)+.5f)/s.scaleY;
    float cx = (floorf((px-s.offsetX)/pitch)+.5f)*pitch+s.offsetX;
    float cy = (floorf((py-s.offsetY)/pitch)+.5f)*pitch+s.offsetY;
    float dx=px-cx,dy=py-cy;
    float distance2=dx*dx+dy*dy;
    if(s.response==0 && s.antialias<=0 && distance2>(.5f*s.size)*(.5f*s.size)) return {};
    float v = 1;
    if (s.channel != 3) {
        Pixel p = sample(read, s, cx/s.pixelAspect*s.scaleX-.5f-s.srcX,
                                  cy*s.scaleY-.5f-s.srcY);
        v = signal(p, s.channel);
    }
    float radius = .5f*s.size*(s.response == 0 ? 1.0f : sqrtf(v));
    if (radius <= 0) return {};
    float coverage;
    if (s.antialias <= 0) coverage = distance2 <= radius*radius ? 1.0f : 0.0f;
    else {
        float pixelWidth = high(s.pixelAspect/s.scaleX, 1.0f/s.scaleY);
        float width = s.antialias*pixelWidth;
        coverage = unit((radius-sqrtf(distance2))/width+.5f);
        // Avoid a nonzero disk as the animated radius approaches zero.
        coverage *= unit(2.0f*radius/width);
    }
    float value = s.response == 1 ? 1.0f : v;
    float a = coverage * s.opacity * alphaLevels(value,s);
    a = unit(a);
    // Keep dot RGB independent of signal, opacity clamps and edge coverage.
    // Raising alpha downstream must reveal the chosen color, not baked-in gray.
    if(a<=0) return {};
    return {s.blue, s.green, s.red, a};
}
}
