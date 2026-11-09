#include "DotsCore.h"
struct GPUReader {
    const dots::Pixel* pixels;
    int pitch;
    __device__ __forceinline__ dots::Pixel operator()(int x,int y) const {
        return pixels[size_t(y)*pitch+x];
    }
};
extern "C" __global__ void DotsKernel(const dots::Pixel* src, dots::Pixel* dst, dots::Settings s) {
    int x = blockIdx.x*blockDim.x+threadIdx.x;
    int y = blockIdx.y*blockDim.y+threadIdx.y;
    if (x < s.dstWidth && y < s.dstHeight)
        dst[size_t(y)*s.dstPitch+x] = dots::renderPixel(GPUReader{src,s.srcPitch},s,x,y);
}
