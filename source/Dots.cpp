#include "CudaDriver.h"
#include "AEConfig.h"
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_EffectGPUSuites.h"
#include "AE_EffectSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "DotsCore.h"
#include "KernelData.h"
#include <algorithm>
#include <cstdio>
#include <memory>
#include <new>
#include <type_traits>
#include <vector>
#include <immintrin.h>
#include <atomic>

namespace {
constexpr char kName[] = "dots-kyunkyun-nya";
constexpr char kMatch[] = "kyunkyun.dots.nya";
std::atomic<uint64_t> lastRenderInfo{0};
void noteRender(unsigned backend,const PF_EffectWorld* world) {
    lastRenderInfo.store((uint64_t(backend)<<32) | (uint64_t(uint16_t(world->width))<<16) | uint16_t(world->height),std::memory_order_relaxed);
}
constexpr A_long kFlags = PF_OutFlag_DEEP_COLOR_AWARE;
constexpr A_long kFlags2 = PF_OutFlag2_SUPPORTS_SMART_RENDER | PF_OutFlag2_FLOAT_COLOR_AWARE |
    PF_OutFlag2_REVEALS_ZERO_ALPHA | PF_OutFlag2_SUPPORTS_GPU_RENDER_F32 |
    PF_OutFlag2_SUPPORTS_THREADED_RENDERING;
enum Param { Input, SourceGroup, Channel, Response, RepeatEdges, SourceEnd, DotsGroup, Size, Gap,
    OffsetX, OffsetY, Antialias, DotsEnd, OutputGroup, Color, Opacity, AlphaBlack,
    AlphaWhite, OutputEnd, Count };
// Preserve all 1.0.0 parameter disk IDs when inserting the new Source checkbox.
constexpr int diskId(int index) {return index==RepeatEdges ? Count-1 : index>RepeatEdges ? index-1 : index;}

template<class T> class Suite {
    SPBasicSuite* basic_; const char* name_; int version_; const T* suite_ = nullptr;
public:
    Suite(PF_InData* in,const char* name,int version):basic_(in->pica_basicP),name_(name),version_(version) {
        if (basic_->AcquireSuite(name,version,reinterpret_cast<const void**>(&suite_)) || !suite_)
            throw PF_Err(PF_Err_INVALID_CALLBACK);
    }
    ~Suite() { if (suite_) basic_->ReleaseSuite(name_,version_); }
    const T* operator->() const {return suite_;}
};

PF_Err parameters(PF_InData* in_data, PF_OutData* out) {
    PF_ParamDef def{};
    PF_ADD_TOPICX("Source",0,SourceGroup);
    PF_ADD_POPUPX("Input",4,3,"Luma|Alpha|Luma + Alpha|Solid",0,Channel);
    PF_ADD_POPUPX("Response",3,1,"Opacity|Size|Size + Opacity",0,Response);
    AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Repeat Edges","",FALSE,0,diskId(RepeatEdges));
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(diskId(SourceEnd));
    PF_ADD_TOPICX("Dots",0,diskId(DotsGroup));
    PF_ADD_FLOAT_SLIDERX("Size",0,4096,0,100,8,1,0,0,diskId(Size));
    PF_ADD_FLOAT_SLIDERX("Gap",0,4096,0,100,4,1,0,0,diskId(Gap));
    PF_ADD_FLOAT_SLIDERX("Offset X",-30000,30000,-100,100,0,1,0,0,diskId(OffsetX));
    PF_ADD_FLOAT_SLIDERX("Offset Y",-30000,30000,-100,100,0,1,0,0,diskId(OffsetY));
    PF_ADD_FLOAT_SLIDERX("Antialiasing",0,4,0,2,0,2,0,0,diskId(Antialias));
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(diskId(DotsEnd));
    PF_ADD_TOPICX("Output",0,diskId(OutputGroup));
    AEFX_CLR_STRUCT(def); PF_ADD_COLOR("Color",255,255,255,diskId(Color));
    PF_ADD_FLOAT_SLIDERX("Opacity",0,100,0,100,100,1,PF_ValueDisplayFlag_PERCENT,0,diskId(Opacity));
    PF_ADD_FLOAT_SLIDERX("Alpha Black Point",0,100,0,100,0,1,PF_ValueDisplayFlag_PERCENT,0,diskId(AlphaBlack));
    PF_ADD_FLOAT_SLIDERX("Alpha White Point",0,100,0,100,100,1,PF_ValueDisplayFlag_PERCENT,0,diskId(AlphaWhite));
    AEFX_CLR_STRUCT(def); PF_END_TOPIC(diskId(OutputEnd));
    out->num_params=Count;
    return PF_Err_NONE;
}

float finite(float v,float fallback) {return std::isfinite(v) ? v : fallback;}
dots::Settings settings(PF_InData* in,PF_ParamDef* p[]) {
    dots::Settings s{};
    s.channel=std::clamp(int(p[Channel]->u.pd.value)-1,0,3);
    s.response=std::clamp(int(p[Response]->u.pd.value)-1,0,2);
    s.repeatEdges=p[RepeatEdges]->u.bd.value ? 1 : 0;
    s.size=std::clamp(finite(float(p[Size]->u.fs_d.value),8),0.f,4096.f);
    s.gap=std::clamp(finite(float(p[Gap]->u.fs_d.value),4),0.f,4096.f);
    s.offsetX=finite(float(p[OffsetX]->u.fs_d.value),0);
    s.offsetY=finite(float(p[OffsetY]->u.fs_d.value),0);
    s.antialias=std::clamp(finite(float(p[Antialias]->u.fs_d.value),0),0.f,4.f);
    s.opacity=dots::unit(float(p[Opacity]->u.fs_d.value)/100.f);
    s.alphaBlack=dots::unit(float(p[AlphaBlack]->u.fs_d.value)/100.f);
    s.alphaWhite=dots::unit(float(p[AlphaWhite]->u.fs_d.value)/100.f);
    // Crossed input handles meet at the white point to form a threshold.
    s.alphaBlack=std::min(s.alphaBlack,s.alphaWhite);
    s.red=p[Color]->u.cd.value.red/255.f;
    s.green=p[Color]->u.cd.value.green/255.f;
    s.blue=p[Color]->u.cd.value.blue/255.f;
    s.scaleX=in->downsample_x.den ? float(in->downsample_x.num)/in->downsample_x.den : 1;
    s.scaleY=in->downsample_y.den ? float(in->downsample_y.num)/in->downsample_y.den : 1;
    s.scaleX=std::max(.0001f,s.scaleX); s.scaleY=std::max(.0001f,s.scaleY);
    s.pixelAspect=in->pixel_aspect_ratio.den ? float(in->pixel_aspect_ratio.num)/in->pixel_aspect_ratio.den : 1;
    s.pixelAspect=std::max(.0001f,s.pixelAspect);
    return s;
}
void disposePre(void* p) { delete static_cast<dots::Settings*>(p); }
PF_Err preRender(PF_InData* in,PF_PreRenderExtra* e) {
    PF_ParamDef values[Count]{}; PF_ParamDef* ptr[Count]{};
    const int ids[]={Channel,Response,RepeatEdges,Size,Gap,OffsetX,OffsetY,Antialias,Color,Opacity,AlphaBlack,AlphaWhite};
    PF_Err err=PF_Err_NONE;
    int checked=0;
    for(int id:ids) {
        err=PF_CHECKOUT_PARAM(in,id,in->current_time,in->time_step,in->time_scale,&values[id]);
        if(err) break;
        ptr[id]=&values[id]; ++checked;
    }
    std::unique_ptr<dots::Settings> s;
    if(!err) s.reset(new(std::nothrow) dots::Settings(settings(in,ptr)));
    for(int i=0;i<checked;++i) {
        PF_Err ce=PF_CHECKIN_PARAM(in,&values[ids[i]]); if(!err) err=ce;
    }
    if(err) return err;
    if(!s) return PF_Err_OUT_OF_MEMORY;
    PF_RenderRequest req=e->input->output_request;
    // A dot only samples near its cell center. With transparent borders, request
    // the output ROI plus that sampling halo rather than forcing full upstream frames.
    const A_long width=A_long(std::ceil(double(in->width)*s->scaleX));
    const A_long height=A_long(std::ceil(double(in->height)*s->scaleY));
    req.rect={0,0,width,height};
    if(!s->repeatEdges && s->channel!=3) {
        A_long hx=A_long(std::ceil(.5f*(s->size+s->gap)*s->scaleX/s->pixelAspect))+2;
        A_long hy=A_long(std::ceil(.5f*(s->size+s->gap)*s->scaleY))+2;
        const auto& r=e->input->output_request.rect;
        req.rect={std::max(A_long(0),r.left-hx),std::max(A_long(0),r.top-hy),std::min(width,r.right+hx),std::min(height,r.bottom+hy)};
        if(req.rect.right<=req.rect.left || req.rect.bottom<=req.rect.top) req.rect={0,0,0,0};
    }
    req.channel_mask=s->channel==1 ? PF_ChannelMask_ALPHA : PF_ChannelMask_ARGB;
    // Hidden RGB is discarded by signal(); don't force upstream effects to keep it.
    req.preserve_rgb_of_zero_alpha=FALSE;
    if(s->size<=0 || s->opacity<=0 || s->channel==3) req.rect={0,0,0,0};
    PF_CheckoutResult result{};
    err=e->cb->checkout_layer(in->effect_ref,0,0,&req,in->current_time,in->time_step,in->time_scale,&result);
    if(err) return err;
    PF_LRect bounds={0,0,width,height};
    PF_LRect available=bounds;
    if(!s->repeatEdges && s->channel!=3) {
        // A dot can reach only half a grid cell plus its radius beyond a source
        // sample. Tight bounds avoid transferring a comp-sized transparent GPU
        // buffer for a small text/matte checkout.
        float aaWidth=s->antialias*std::max(s->pixelAspect/s->scaleX,1.f/s->scaleY);
        float reach=.5f*(s->size+s->gap)+.5f*s->size+.5f*aaWidth;
        A_long haloX=A_long(std::ceil(reach*s->scaleX/s->pixelAspect+2));
        A_long haloY=A_long(std::ceil(reach*s->scaleY+2));
        auto expand=[&](PF_LRect source) {
            if(source.right<=source.left || source.bottom<=source.top) return PF_LRect{0,0,0,0};
            PF_LRect r={std::max(A_long(0),source.left-haloX),std::max(A_long(0),source.top-haloY),
                std::min(width,source.right+haloX),std::min(height,source.bottom+haloY)};
            return r.right<=r.left || r.bottom<=r.top ? PF_LRect{0,0,0,0} : r;
        };
        bounds=expand(result.max_result_rect);
        available=expand(result.result_rect);
    }
    if(s->size<=0 || s->opacity<=0) bounds=available={0,0,0,0};
    e->output->max_result_rect=bounds;
    const auto& r=e->input->output_request.rect;
    e->output->result_rect={std::max(r.left,available.left),std::max(r.top,available.top),
                          std::min(r.right,available.right),std::min(r.bottom,available.bottom)};
    if(e->output->result_rect.right<e->output->result_rect.left || e->output->result_rect.bottom<e->output->result_rect.top)
        e->output->result_rect={0,0,0,0};
    if(e->input->what_gpu==PF_GPU_Framework_CUDA)
        e->output->flags|=PF_RenderOutputFlag_GPU_RENDER_POSSIBLE;
    e->output->pre_render_data=s.release();
    e->output->delete_pre_render_data_func=disposePre;
    return PF_Err_NONE;
}
void dimensions(dots::Settings& s,const PF_EffectWorld* src,const PF_EffectWorld* dst) {
    s.srcWidth=src ? src->width : 0; s.srcHeight=src ? src->height : 0;
    s.srcX=src ? src->origin_x : 0; s.srcY=src ? src->origin_y : 0;
    s.dstWidth=dst->width; s.dstHeight=dst->height;
    s.dstX=dst->origin_x; s.dstY=dst->origin_y;
}
template<class P> struct CPUReader {
    const PF_EffectWorld* world;
    dots::Pixel operator()(int x,int y) const {
        const P& p=reinterpret_cast<const P*>(reinterpret_cast<const char*>(world->data)+ptrdiff_t(y)*world->rowbytes)[x];
        constexpr float scale=std::is_same_v<P,PF_Pixel8> ? 1.f/255 : std::is_same_v<P,PF_Pixel16> ? 1.f/32768 : 1;
        return {p.blue*scale,p.green*scale,p.red*scale,p.alpha*scale};
    }
};
struct AxisRun {int start,end;float center;};
struct DotCell {float radius,amplitude;};
struct DotSpan {int left,right;};
struct AxisData {std::vector<AxisRun> runs;std::vector<float> distance2;};
AxisData axisData(int count,int origin,float scale,float aspect,float offset,float pitch) {
    AxisData a;a.distance2.resize(count);
    for(int i=0;i<count;++i) {
        float pos=(float(i+origin)+.5f)/scale*aspect;
        float center=(floorf((pos-offset)/pitch)+.5f)*pitch+offset;
        float d=pos-center;a.distance2[i]=d*d;
        if(a.runs.empty() || center!=a.runs.back().center) a.runs.push_back({i,i+1,center});
        else a.runs.back().end=i+1;
    }
    return a;
}
template<class P> struct FastCPUJob {
    PF_InData* in;const PF_EffectWorld* src;PF_EffectWorld* dst;dots::Settings s;
    AxisData x,y;std::vector<float> stamp;std::vector<DotSpan> hardSpans;
    int periodX=0,periodY=0;float aaWidth=0;
    static constexpr float channelScale=std::is_same_v<P,PF_Pixel8> ? 255.f : std::is_same_v<P,PF_Pixel16> ? 32768.f : 1.f;
    P color{};
    FastCPUJob(PF_InData* host,const PF_EffectWorld* a,PF_EffectWorld* b,dots::Settings c):in(host),src(a),dst(b),s(c) {
        float pitch=std::max(.01f,s.size+s.gap);
        x=axisData(s.dstWidth,s.dstX,s.scaleX,s.pixelAspect,s.offsetX,pitch);
        y=axisData(s.dstHeight,s.dstY,s.scaleY,1,s.offsetY,pitch);
        std::vector<AxisRun> bands;
        for(const auto& run:y.runs)for(int start=run.start;start<run.end;start+=32)
            bands.push_back({start,std::min(start+32,run.end),run.center});
        y.runs=std::move(bands);
        using C=decltype(P::alpha);
        if constexpr(std::is_same_v<P,PF_PixelFloat>) color={0,s.red,s.green,s.blue};
        else color={0,C(s.red*channelScale+.5f),C(s.green*channelScale+.5f),C(s.blue*channelScale+.5f)};
        aaWidth=s.antialias*std::max(s.pixelAspect/s.scaleX,1.f/s.scaleY);
        float px=pitch*s.scaleX/s.pixelAspect,py=pitch*s.scaleY;
        if(s.response==0 && px>=1 && py>=1 && px<=256 && py<=256 &&
           fabsf(px-roundf(px))<1e-6f && fabsf(py-roundf(py))<1e-6f) {
            periodX=int(roundf(px));periodY=int(roundf(py));stamp.resize(size_t(periodX)*periodY);
            auto ax=axisData(periodX,s.dstX,s.scaleX,s.pixelAspect,s.offsetX,pitch);
            auto ay=axisData(periodY,s.dstY,s.scaleY,1,s.offsetY,pitch);
            for(int iy=0;iy<periodY;++iy)for(int ix=0;ix<periodX;++ix)
                stamp[size_t(iy)*periodX+ix]=coverage(.5f*s.size,ax.distance2[ix]+ay.distance2[iy]);
            if(s.antialias<=0) {
                hardSpans.resize(size_t(periodY)*x.runs.size());
                for(int iy=0;iy<periodY;++iy)for(size_t ix=0;ix<x.runs.size();++ix)
                    hardSpans[size_t(iy)*x.runs.size()+ix]=span(x.runs[ix],.25f*s.size*s.size-ay.distance2[iy]);
            }
        }
    }
    DotSpan span(const AxisRun& xr,float reach2) const {
        if(reach2<0) return {xr.start,xr.start};
        float half=sqrtf(reach2);
        int left=std::clamp(int(ceilf((xr.center-half)/s.pixelAspect*s.scaleX-.5f-s.dstX)),xr.start,xr.end);
        int right=std::clamp(int(floorf((xr.center+half)/s.pixelAspect*s.scaleX-.5f-s.dstX))+1,xr.start,xr.end);
        while(left<xr.end && x.distance2[left]>reach2) ++left;
        while(left>xr.start && x.distance2[left-1]<=reach2) --left;
        while(right>xr.start && x.distance2[right-1]>reach2) --right;
        while(right<xr.end && x.distance2[right]<=reach2) ++right;
        return left>=right ? DotSpan{xr.start,xr.start} : DotSpan{left,right};
    }
    float coverage(float radius,float distance2) const {
        if(radius<=0) return 0;
        if(s.antialias<=0) return distance2<=radius*radius ? 1.f : 0.f;
        return dots::unit((radius-sqrtf(distance2))/aaWidth+.5f)*dots::unit(2.f*radius/aaWidth);
    }
    P pixel(float alpha) const {
        if(alpha<=0) return {};
        P p=color;
        if constexpr(std::is_same_v<P,PF_PixelFloat>) p.alpha=dots::unit(alpha);
        else p.alpha=decltype(P::alpha)(dots::unit(alpha)*channelScale+.5f);
        return p;
    }
    static void write(P* dest,P p) {
        if constexpr(std::is_same_v<P,PF_PixelFloat>) {
            if((reinterpret_cast<uintptr_t>(dest)&15)==0) {
                _mm_stream_ps(reinterpret_cast<float*>(dest),_mm_set_ps(p.blue,p.green,p.red,p.alpha));
                return;
            }
        }
        *dest=p;
    }
    static void fill(P* first,P* last,P p) {
        if constexpr(std::is_same_v<P,PF_PixelFloat>) {for(;first!=last;++first)write(first,p);}
        else std::fill(first,last,p);
    }
    static PF_Err band(void* ref,A_long thread,A_long index,A_long) {
        auto& j=*static_cast<FastCPUJob*>(ref);const auto& yr=j.y.runs[index];
        if(thread==0 && index%64==0) {PF_Err err=PF_ABORT(j.in);if(err)return err;}
        // Scratch grows with one row of cells, not width*height. Dense grids no
        // longer trigger a slow renderer or allocate a whole-frame cell cache.
        std::unique_ptr<DotCell[]> cells(new(std::nothrow) DotCell[j.x.runs.size()]);
        if(!cells) return PF_Err_OUT_OF_MEMORY;
        CPUReader<P> read{j.src};
        // Sample once per dot, shared by every pixel in its grid cell.
        for(size_t i=0;i<j.x.runs.size();++i) {
            float v=1;
            if(j.s.channel!=3) v=dots::signal(dots::sample(read,j.s,
                j.x.runs[i].center/j.s.pixelAspect*j.s.scaleX-.5f-j.s.srcX,
                yr.center*j.s.scaleY-.5f-j.s.srcY),j.s.channel);
            cells[i]={.5f*j.s.size*(j.s.response==0 ? 1.f : sqrtf(v)),
                j.s.opacity*dots::alphaLevels(j.s.response==1 ? 1.f : v,j.s)};
        }
        for(int y=yr.start;y<yr.end;++y) {
            P* row=reinterpret_cast<P*>(reinterpret_cast<char*>(j.dst->data)+ptrdiff_t(y)*j.dst->rowbytes);
            const float* stamp=j.stamp.empty() ? nullptr : j.stamp.data()+size_t(y%j.periodY)*j.periodX;
            for(size_t i=0;i<j.x.runs.size();++i) {
                const auto& xr=j.x.runs[i];const auto& cell=cells[i];
                if(cell.radius<=0 || cell.amplitude<=0) {
                    fill(row+xr.start,row+xr.end,P{});
                    continue;
                }
                if(j.s.antialias<=0) {
                    DotSpan span=j.hardSpans.empty() ? j.span(xr,cell.radius*cell.radius-j.y.distance2[y]) : j.hardSpans[size_t(y%j.periodY)*j.x.runs.size()+i];
                    int left=span.left,right=span.right;
                    if(left>=right) {fill(row+xr.start,row+xr.end,P{});continue;}
                    fill(row+xr.start,row+left,P{});
                    fill(row+left,row+right,j.pixel(cell.amplitude));
                    fill(row+right,row+xr.end,P{});
                    continue;
                }
                if(stamp) {
                    int sx=xr.start%j.periodX;
                    for(int x=xr.start;x<xr.end;++x) {write(row+x,j.pixel(stamp[sx]*cell.amplitude));if(++sx==j.periodX)sx=0;}
                } else {
                    for(int x=xr.start;x<xr.end;++x)write(row+x,j.pixel(j.coverage(cell.radius,j.x.distance2[x]+j.y.distance2[y])*cell.amplitude));
                }
            }
        }
        if constexpr(std::is_same_v<P,PF_PixelFloat>) _mm_sfence();
        return PF_Err_NONE;
    }
};
template<class P> PF_Err cpuTyped(PF_InData* in,PF_EffectWorld* src,PF_EffectWorld* dst,dots::Settings s) {
    Suite<PF_Iterate8Suite1> iterate(in,kPFIterate8Suite,kPFIterate8SuiteVersion1);
    PF_Err err=PF_ABORT(in);
    if(err) return err;
    FastCPUJob<P> job(in,src,dst,s);
    err=iterate->iterate_generic(A_long(job.y.runs.size()),&job,FastCPUJob<P>::band);
    if(!err) err=PF_ABORT(in);
    return err;
}
PF_Err cpu(PF_InData* in,PF_EffectWorld* src,PF_EffectWorld* dst,dots::Settings s,PF_PixelFormat fmt) {
    dimensions(s,src,dst);
    noteRender(1,dst);
    if(!dst->width || !dst->height) return PF_Err_NONE;
    if(!dst->data || (src && src->width && src->height && !src->data)) return PF_Err_BAD_CALLBACK_PARAM;
    switch(fmt) {
        case PF_PixelFormat_ARGB32: return cpuTyped<PF_Pixel8>(in,src,dst,s);
        case PF_PixelFormat_ARGB64: return cpuTyped<PF_Pixel16>(in,src,dst,s);
        case PF_PixelFormat_ARGB128: return cpuTyped<PF_PixelFloat>(in,src,dst,s);
        default:return PF_Err_BAD_CALLBACK_PARAM;
    }
}
struct GPUData {
    CudaDriver driver; CUmodule module=nullptr; CUfunction kernel=nullptr; CUcontext context=nullptr;
    ~GPUData() { if(module) { CudaContextScope current(driver,context); if(current.pushed) driver.unload(module); } }
};
PF_Err gpuSetup(PF_InData* in,PF_OutData* out,PF_GPUDeviceSetupExtra* e) {
    out->out_flags2=0; e->output->gpu_data=nullptr;
    if(e->input->what_gpu!=PF_GPU_Framework_CUDA) return PF_Err_NONE;
    Suite<PF_GPUDeviceSuite1> suite(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1);
    PF_GPUDeviceInfo info{};
    PF_Err err=suite->GetDeviceInfo(in->effect_ref,e->input->device_index,&info);
    if(err) return err;
    auto gpu=std::make_unique<GPUData>();
    if(!gpu->driver.open()) return PF_Err_NONE;
    gpu->context=static_cast<CUcontext>(info.contextPV);
    CudaContextScope context(gpu->driver,gpu->context);
    if(!context.pushed || gpu->driver.load(&gpu->module,kKernelData)!=CUDA_SUCCESS ||
       gpu->driver.function(&gpu->kernel,gpu->module,"DotsKernel")!=CUDA_SUCCESS) return PF_Err_NONE;
    out->out_flags2=PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
    e->output->gpu_data=gpu.release();
    return PF_Err_NONE;
}
PF_Err gpuRender(PF_InData* in,PF_OutData* out,PF_SmartRenderExtra* e,PF_EffectWorld* src,PF_EffectWorld* dst,dots::Settings s) {
    if(e->input->what_gpu!=PF_GPU_Framework_CUDA || !e->input->gpu_data) return PF_Err_BAD_CALLBACK_PARAM;
    auto* gpu=const_cast<GPUData*>(static_cast<const GPUData*>(e->input->gpu_data));
    Suite<PF_GPUDeviceSuite1> suite(in,kPFGPUDeviceSuite,kPFGPUDeviceSuiteVersion1);
    PF_GPUDeviceInfo info{};
    PF_Err err=suite->GetDeviceInfo(in->effect_ref,e->input->device_index,&info);
    void* source=nullptr; void* dest=nullptr;
    if(!err && src && src->width && src->height) err=suite->GetGPUWorldData(in->effect_ref,src,&source);
    if(!err) err=suite->GetGPUWorldData(in->effect_ref,dst,&dest);
    if(err) return err;
    dimensions(s,src,dst);
    noteRender(2,dst);
    if(!s.dstWidth || !s.dstHeight) return PF_Err_NONE;
    s.srcPitch=src ? src->rowbytes/16 : 0; s.dstPitch=dst->rowbytes/16;
    if(!dest || (s.srcWidth && s.srcHeight && !source)) return PF_Err_BAD_CALLBACK_PARAM;
    CudaContextScope context(gpu->driver,static_cast<CUcontext>(info.contextPV));
    if(!context.pushed) return PF_Err_INTERNAL_STRUCT_DAMAGED;
    void* args[]={&source,&dest,&s};
    CUresult result=gpu->driver.launch(gpu->kernel,(s.dstWidth+31)/32,(s.dstHeight+7)/8,1,32,8,1,0,
        static_cast<CUstream>(info.command_queuePV),args,nullptr);
    // The host owns the stream and buffers. No allocation, transfer or forced sync per frame.
    if(result!=CUDA_SUCCESS) {
        std::snprintf(out->return_msg,sizeof(out->return_msg),"dots-kyunkyun-nya: GPU render failed (%d).",int(result));
        out->out_flags|=PF_OutFlag_DISPLAY_ERROR_MESSAGE;
        return PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return PF_Err_NONE;
}
PF_Err smartRender(PF_InData* in,PF_OutData* out,PF_SmartRenderExtra* e,bool gpu) {
    if(!e->input->pre_render_data) return PF_Err_BAD_CALLBACK_PARAM;
    PF_EffectWorld* src=nullptr; PF_EffectWorld* dst=nullptr;
    PF_Err err=e->cb->checkout_layer_pixels(in->effect_ref,0,&src);
    if(err) return err;
    try {
        err=e->cb->checkout_output(in->effect_ref,&dst);
        if(!err && !dst) err=PF_Err_BAD_CALLBACK_PARAM;
        if(!err) {
            Suite<PF_WorldSuite2> suite(in,kPFWorldSuite,kPFWorldSuiteVersion2);
            PF_PixelFormat fmt=PF_PixelFormat_INVALID;
            err=suite->PF_GetPixelFormat(dst,&fmt);
            auto s=*static_cast<dots::Settings*>(e->input->pre_render_data);
            if(!err && gpu) err=fmt==PF_PixelFormat_GPU_BGRA128 ? gpuRender(in,out,e,src,dst,s) : PF_Err_BAD_CALLBACK_PARAM;
            if(!err && !gpu) err=cpu(in,src,dst,s,fmt);
        }
    } catch(...) { e->cb->checkin_layer_pixels(in->effect_ref,0); throw; }
    PF_Err checkin=e->cb->checkin_layer_pixels(in->effect_ref,0);
    return err ? err : checkin;
}
}

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr p,PF_PluginDataCB2 cb,SPBasicSuite*,const char*,const char*) {
    PF_Err result=PF_Err_NONE;
    PF_REGISTER_EFFECT_EXT2(p,cb,kName,kMatch,"Stylize",AE_RESERVED_INFO,"EffectMain","");
    return result;
}
extern "C" DllExport PF_Err EffectMain(PF_Cmd cmd,PF_InData* in,PF_OutData* out,PF_ParamDef* params[],PF_LayerDef* output,void* extra) {
    try {
        switch(cmd) {
            case PF_Cmd_ABOUT: {
                uint64_t info=lastRenderInfo.load(std::memory_order_relaxed);
                std::snprintf(out->return_msg,sizeof(out->return_msg),"dots-kyunkyun-nya 1.0.2\rLast render: %s, %u x %u",
                    info>>32==2 ? "CUDA" : info>>32==1 ? "CPU" : "none",unsigned((info>>16)&65535),unsigned(info&65535));
                break;
            }
            case PF_Cmd_GLOBAL_SETUP:
                if(in->appl_id!='FXTC') return PF_Err_BAD_CALLBACK_PARAM;
                out->my_version=PF_VERSION(1,0,2,PF_Stage_RELEASE,1);
                out->out_flags=kFlags; out->out_flags2=kFlags2; break;
            case PF_Cmd_PARAMS_SETUP: return parameters(in,out);
            case PF_Cmd_SMART_PRE_RENDER:return preRender(in,static_cast<PF_PreRenderExtra*>(extra));
            case PF_Cmd_SMART_RENDER:return smartRender(in,out,static_cast<PF_SmartRenderExtra*>(extra),false);
            case PF_Cmd_SMART_RENDER_GPU:return smartRender(in,out,static_cast<PF_SmartRenderExtra*>(extra),true);
            case PF_Cmd_GPU_DEVICE_SETUP:return gpuSetup(in,out,static_cast<PF_GPUDeviceSetupExtra*>(extra));
            case PF_Cmd_GPU_DEVICE_SETDOWN:delete static_cast<GPUData*>(static_cast<PF_GPUDeviceSetdownExtra*>(extra)->input->gpu_data); break;
            case PF_Cmd_RENDER:return cpu(in,&params[Input]->u.ld,output,settings(in,params),PF_WORLD_IS_DEEP(output)?PF_PixelFormat_ARGB64:PF_PixelFormat_ARGB32);
            default:break;
        }
        return PF_Err_NONE;
    } catch(PF_Err e) {return e;}
      catch(const std::bad_alloc&) {return PF_Err_OUT_OF_MEMORY;}
      catch(...) {return PF_Err_INTERNAL_STRUCT_DAMAGED;}
}
