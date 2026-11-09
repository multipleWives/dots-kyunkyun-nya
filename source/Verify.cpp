// Integration harness: calls the built .aex through its exported Adobe entry point.
#include "Dots.cpp"
#include <vector>
#include <random>
#include <thread>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>

namespace test {
using Entry=PF_Err(*)(PF_Cmd,PF_InData*,PF_OutData*,PF_ParamDef**,PF_LayerDef*,void*);
Entry entry;
PF_ParamDef defs[Count]{};
int added=1, checkouts=0, checkins=0;
PF_EffectWorld input{},output{};
PF_PixelFormat format=PF_PixelFormat_ARGB128;
PF_GPUDeviceInfo gpuInfo{};
PF_GPUDeviceSuite1 gpuSuite{};
PF_WorldSuite2 worldSuite{};
PF_Iterate8Suite1 iterateSuite{};
SPBasicSuite basic{};
void require(bool b,const char* text) {if(!b) throw std::runtime_error(text);}
void ok(PF_Err e) {if(e) throw std::runtime_error("AE error "+std::to_string(e));}
void cu(CUresult r) {if(r) {const char* msg;cuGetErrorString(r,&msg);throw std::runtime_error(msg);}}
SPErr acquire(const char* n,int32,const void** p) {
    if(!strcmp(n,kPFWorldSuite)) *p=&worldSuite;
    else if(!strcmp(n,kPFIterate8Suite)) *p=&iterateSuite;
    else if(!strcmp(n,kPFGPUDeviceSuite)) *p=&gpuSuite;
    else return -1;
    return 0;
}
SPErr release(const char*,int32) {return 0;}
PF_Err add(PF_ProgPtr,A_long,PF_ParamDef* p) {defs[added++]=*p;return 0;}
PF_Err abort(PF_ProgPtr) {return 0;}
PF_Err checkout(PF_ProgPtr,PF_ParamIndex id,A_long,A_long,A_u_long,PF_ParamDef* p) { *p=defs[id];++checkouts;return 0; }
PF_Err checkin(PF_ProgPtr,PF_ParamDef*) {++checkins;return 0;}
PF_RenderRequest lastRequest{};
PF_Err checkoutLayer(PF_ProgPtr,PF_ParamIndex,A_long,const PF_RenderRequest* req,A_long,A_long,A_u_long,PF_CheckoutResult* r) {
    lastRequest=*req;
    r->result_rect=r->max_result_rect={input.origin_x,input.origin_y,input.origin_x+input.width,input.origin_y+input.height};return 0;
}
PF_Err inputPixels(PF_ProgPtr,A_long,PF_EffectWorld** w){*w=&input;return 0;}
PF_Err outputPixels(PF_ProgPtr,PF_EffectWorld** w){*w=&output;return 0;}
PF_Err checkPixels(PF_ProgPtr,A_long){return 0;}
PF_Err getFormat(const PF_EffectWorld*,PF_PixelFormat* f){*f=format;return 0;}
PF_Err device(PF_ProgPtr,A_u_long,PF_GPUDeviceInfo* i){*i=gpuInfo;return 0;}
PF_Err gpuPixels(PF_ProgPtr,PF_EffectWorld* w,void** p){*p=w->data;return 0;}
PF_Err iterate(A_long n,void* p,PF_Err(*fn)(void*,A_long,A_long,A_long)) {
    std::vector<std::thread> workers;
    std::atomic<PF_Err> result{0};
    for(int t=0;t<8;++t) workers.emplace_back([=,&result]{for(int y=t;y<n && !result.load();y+=8) {PF_Err e=fn(p,t,y,n);if(e)result.store(e);}});
    for(auto& w:workers) w.join();return result.load();
}
PF_InData in{};
PF_OutData out{};
PF_SmartRenderCallbacks renderCallbacks{inputPixels,checkPixels,outputPixels};
PF_PreRenderCallbacks preCallbacks{checkoutLayer,nullptr};
PF_GPUDeviceSetupInput setupInput{PF_GPU_Framework_CUDA,0};
PF_GPUDeviceSetupOutput setupOutput{};
PF_GPUDeviceSetupExtra setupExtra{&setupInput,&setupOutput};
void run(dots::Settings s,bool gpu) {
    PF_SmartRenderInput ri{};ri.pre_render_data=&s;ri.what_gpu=gpu ? PF_GPU_Framework_CUDA : PF_GPU_Framework_NONE;
    ri.gpu_data=setupOutput.gpu_data;
    PF_SmartRenderExtra e{&ri,&renderCallbacks};
    ok(entry(gpu ? PF_Cmd_SMART_RENDER_GPU : PF_Cmd_SMART_RENDER,&in,&out,nullptr,nullptr,&e));
}
void init(const char* dllPath) {
    HMODULE dll=LoadLibraryA(dllPath);require(dll!=nullptr,"Cannot load AEX");
    entry=reinterpret_cast<Entry>(GetProcAddress(dll,"EffectMain"));require(entry!=nullptr,"Missing effect entry");
    require(GetProcAddress(dll,"PluginDataEntryFunction2")!=nullptr,"Missing registration entry");
    basic.AcquireSuite=acquire;basic.ReleaseSuite=release;
    worldSuite.PF_GetPixelFormat=getFormat;iterateSuite.iterate_generic=iterate;
    gpuSuite.GetDeviceInfo=device;gpuSuite.GetGPUWorldData=gpuPixels;
    in.pica_basicP=&basic;in.appl_id='FXTC';in.width=3000;in.height=3000;
    in.downsample_x=in.downsample_y=in.pixel_aspect_ratio={1,1};
    in.inter.add_param=add;in.inter.abort=abort;in.inter.checkout_param=checkout;in.inter.checkin_param=checkin;
    ok(entry(PF_Cmd_GLOBAL_SETUP,&in,&out,nullptr,nullptr,nullptr));
    require(out.my_version==529921 && out.out_flags==0x02000000 && out.out_flags2==0x0a001480,"PiPL flags/version mismatch");
    ok(entry(PF_Cmd_PARAMS_SETUP,&in,&out,nullptr,nullptr,nullptr));
    require(added==Count && out.num_params==Count,"Wrong parameter count");
    const int savedIds[]={0,1,2,3,18,4,5,6,7,8,9,10,11,12,13,14,15,16,17};
    for(int i=1;i<Count;++i) require(defs[i].uu.id==savedIds[i],"Unstable parameter ID");
    require(defs[RepeatEdges].param_type==PF_Param_CHECKBOX && !defs[RepeatEdges].u.bd.value,"Repeat Edges must default off");
    require(defs[Antialias].u.fs_d.value==0 && defs[Antialias].u.fs_d.dephault==0,"Antialiasing must default off");
    input.width=input.height=3000;
    PF_PreRenderInput pi{}; pi.output_request.rect={17,23,107,91};pi.what_gpu=PF_GPU_Framework_CUDA;
    PF_PreRenderOutput po{};PF_PreRenderExtra pe{&pi,&po,&preCallbacks};
    ok(entry(PF_Cmd_SMART_PRE_RENDER,&in,&out,nullptr,nullptr,&pe));
    require(checkouts==12 && checkins==12,"Parameter checkout leak");
    require(po.result_rect.left==17 && po.result_rect.bottom==91 && po.max_result_rect.right==3000,"Invalid ROI bounds");
    require(po.pre_render_data && po.delete_pre_render_data_func,"Missing pre-render data");
    po.delete_pre_render_data_func(po.pre_render_data);
    in.downsample_x=in.downsample_y={1,2};po={};
    ok(entry(PF_Cmd_SMART_PRE_RENDER,&in,&out,nullptr,nullptr,&pe));
    require(po.max_result_rect.right==1500 && po.max_result_rect.bottom==1500,"Downsample bounds incorrect");
    po.delete_pre_render_data_func(po.pre_render_data);in.downsample_x=in.downsample_y={1,1};
    input.width=120;input.height=90;input.origin_x=300;input.origin_y=240;
    pi.output_request.rect={0,0,3000,3000};po={};
    ok(entry(PF_Cmd_SMART_PRE_RENDER,&in,&out,nullptr,nullptr,&pe));
    require(po.result_rect.left>0 && po.result_rect.right<500 && po.result_rect.bottom<400,"Sparse source still requests comp-sized output");
    po.delete_pre_render_data_func(po.pre_render_data);
    defs[RepeatEdges].u.bd.value=TRUE;po={};
    ok(entry(PF_Cmd_SMART_PRE_RENDER,&in,&out,nullptr,nullptr,&pe));
    require(po.max_result_rect.right==3000 && po.result_rect.left==0,"Repeat Edges must restore full bounds");
    require(static_cast<dots::Settings*>(po.pre_render_data)->repeatEdges==1,"Repeat Edges checkbox not checked out");
    po.delete_pre_render_data_func(po.pre_render_data);defs[RepeatEdges].u.bd.value=FALSE;
    input.origin_x=input.origin_y=-1000;input.width=input.height=10;po={};
    ok(entry(PF_Cmd_SMART_PRE_RENDER,&in,&out,nullptr,nullptr,&pe));
    require(po.max_result_rect.left==0 && po.max_result_rect.right==0 && po.result_rect.bottom==0,"Off-layer source produced invalid bounds");
    po.delete_pre_render_data_func(po.pre_render_data);
    input.origin_x=input.origin_y=0;input.width=input.height=3000;
    defs[Opacity].u.fs_d.value=0;po={};
    ok(entry(PF_Cmd_SMART_PRE_RENDER,&in,&out,nullptr,nullptr,&pe));
    require(po.max_result_rect.right==0 && po.result_rect.right==0 && lastRequest.rect.right==0,"Invisible effect still requests frame rendering");
    require(!lastRequest.preserve_rgb_of_zero_alpha,"Unneeded hidden RGB request");
    po.delete_pre_render_data_func(po.pre_render_data);defs[Opacity].u.fs_d.value=100;
    cu(cuInit(0));CUdevice device;cu(cuDeviceGet(&device,0));CUcontext context;cu(cuDevicePrimaryCtxRetain(&context,device));
    cu(cuCtxSetCurrent(context));CUstream stream;cu(cuStreamCreate(&stream,CU_STREAM_NON_BLOCKING));
    gpuInfo.contextPV=context;gpuInfo.command_queuePV=stream;gpuInfo.device_framework=PF_GPU_Framework_CUDA;
    char name[256];cu(cuDeviceGetName(name,256,device));std::cout<<"GPU: "<<name<<"\n";
    ok(entry(PF_Cmd_GPU_DEVICE_SETUP,&in,&out,nullptr,nullptr,&setupExtra));
    require(setupOutput.gpu_data && out.out_flags2==PF_OutFlag2_SUPPORTS_GPU_RENDER_F32,"GPU setup failed");
}
dots::Settings defaults() {
    PF_ParamDef* ptr[Count];for(int i=0;i<Count;++i) ptr[i]=&defs[i];return settings(&in,ptr);
}
void correctness() {
    const int w=127,h=93,pitch=136;
    std::vector<PF_PixelFloat> source(pitch*h),cpuOutput(pitch*h);
    std::vector<dots::Pixel> bgra(pitch*h),gpuOutput(pitch*h);
    std::mt19937 rng(1234);std::uniform_real_distribution<float> random(0,1);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
        float a=(x%11==0)?0:random(rng);float r=random(rng)*a,g=random(rng)*a,b=random(rng)*a;
        source[y*pitch+x]={a,r,g,b};bgra[y*pitch+x]={b,g,r,a};
    }
    CUdeviceptr gs,gd;cu(cuMemAlloc(&gs,bgra.size()*16));cu(cuMemAlloc(&gd,bgra.size()*16));
    cu(cuMemcpyHtoD(gs,bgra.data(),bgra.size()*16));
    input.width=output.width=w;input.height=output.height=h;input.rowbytes=output.rowbytes=pitch*16;
    input.origin_x=19;input.origin_y=13;output.origin_x=31;output.origin_y=27;
    float maxError=0;int cases=0;
    for(int channel=0;channel<4;++channel) for(int response=0;response<3;++response) for(int ds=1;ds<=4;ds*=2) {
        auto s=defaults();s.channel=channel;s.response=response;s.scaleX=s.scaleY=1.f/ds;
        s.pixelAspect=1.2f;s.offsetX=-3.25f;s.offsetY=7.75f;s.size=7.3f;s.gap=2.9f;
        s.red=.17f;s.green=.62f;s.blue=.91f;s.opacity=.7f;s.alphaBlack=.1f;s.alphaWhite=.8f;
        input.data=reinterpret_cast<PF_Pixel*>(source.data());output.data=reinterpret_cast<PF_Pixel*>(cpuOutput.data());
        format=PF_PixelFormat_ARGB128;run(s,false);
        input.data=reinterpret_cast<PF_Pixel*>(gs);output.data=reinterpret_cast<PF_Pixel*>(gd);
        format=PF_PixelFormat_GPU_BGRA128;run(s,true);cu(cuStreamSynchronize(static_cast<CUstream>(gpuInfo.command_queuePV)));
        cu(cuMemcpyDtoH(gpuOutput.data(),gd,gpuOutput.size()*16));
        for(int y=0;y<h;++y) for(int x=0;x<w;++x) {
            const auto& a=cpuOutput[y*pitch+x];const auto& b=gpuOutput[y*pitch+x];
            maxError=std::max(maxError,std::max({fabsf(a.alpha-b.a),fabsf(a.red-b.r),fabsf(a.green-b.g),fabsf(a.blue-b.b)}));
            require(std::isfinite(b.a) && b.a>=0 && b.a<=1,"Invalid alpha");
            if(b.a>0)require(b.r==s.red && b.g==s.green && b.b==s.blue,"Opacity altered dot RGB");
        }
        ++cases;
    }
    require(maxError<.0002f,"CPU/GPU disagreement");
    // Independently known signal expectations, including premultiplied alpha.
    require(fabsf(dots::signal({.125f,.125f,.125f,.25f},0)-.5f)<1e-6f,"Luma alpha independence");
    require(fabsf(dots::signal({.125f,.125f,.125f,.25f},2)-.125f)<1e-6f,"Luma+alpha product");
    require(dots::signal({1,1,1,0},0)==0,"Hidden zero-alpha RGB leaked");
    cu(cuMemFree(gs));cu(cuMemFree(gd));
    std::cout<<"PASS: registration, flags, parameter IDs, pre-render ROI, balanced checkouts, "<<cases<<" CPU/GPU cases. Maximum component error "<<maxError<<"\n";
}
template<class P> void integerDepth(PF_PixelFormat fmt,float scale) {
    const int w=61,h=47,pitch=68;
    std::vector<P> src(pitch*h),dst(pitch*h);
    std::vector<PF_PixelFloat> fs(pitch*h),fd(pitch*h);
    std::mt19937 rng(7);using C=decltype(P::alpha);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x){
        C a=C(rng()%int(scale+1)),r=C(rng()%(int(a)+1)),g=C(rng()%(int(a)+1)),b=C(rng()%(int(a)+1));
        src[y*pitch+x]={a,r,g,b};fs[y*pitch+x]={a/scale,r/scale,g/scale,b/scale};
    }
    input={};output={};input.width=output.width=w;input.height=output.height=h;
    auto s=defaults();s.size=5.3f;s.gap=1.2f;s.offsetX=-.6f;
    for(int channel=0;channel<4;++channel)for(int response=0;response<3;++response) {
        s.channel=channel;s.response=response;
        input.rowbytes=output.rowbytes=pitch*sizeof(P);input.data=reinterpret_cast<PF_Pixel*>(src.data());output.data=reinterpret_cast<PF_Pixel*>(dst.data());
        format=fmt;run(s,false);
        input.rowbytes=output.rowbytes=pitch*16;input.data=reinterpret_cast<PF_Pixel*>(fs.data());output.data=reinterpret_cast<PF_Pixel*>(fd.data());
        format=PF_PixelFormat_ARGB128;run(s,false);
        for(int y=0;y<h;++y)for(int x=0;x<w;++x)require(fabsf(dst[y*pitch+x].alpha/scale-fd[y*pitch+x].alpha)<=.51f/scale,"Integer-depth mismatch");
        for(int y=0;y<h;++y)for(int x=w;x<pitch;++x)require(dst[y*pitch+x].alpha==0,"Wrote into row padding");
    }
}
void edgeCases() {
    integerDepth<PF_Pixel8>(PF_PixelFormat_ARGB32,255);
    integerDepth<PF_Pixel16>(PF_PixelFormat_ARGB64,32768);
    const int w=83,h=61;std::vector<PF_PixelFloat> src(w*h,PF_PixelFloat{1,.5f,.5f,.5f}),dst(w*h),tile(17*19);
    input={};output={};input.width=output.width=w;input.height=output.height=h;input.rowbytes=output.rowbytes=w*16;
    input.data=reinterpret_cast<PF_Pixel*>(src.data());output.data=reinterpret_cast<PF_Pixel*>(dst.data());format=PF_PixelFormat_ARGB128;
    auto s=defaults();run(s,false);
    output.width=17;output.height=19;output.rowbytes=17*16;output.origin_x=11;output.origin_y=7;output.data=reinterpret_cast<PF_Pixel*>(tile.data());
    run(s,false);
    for(int y=0;y<19;++y)for(int x=0;x<17;++x)require(tile[y*17+x].alpha==dst[(y+7)*w+x+11].alpha,"ROI seam");
    for(int mode=0;mode<4;++mode){s.channel=mode;s.size=0;run(s,false);for(auto p:tile)require(p.alpha==0,"Zero size not empty");}
    s=defaults();s.opacity=0;run(s,false);for(auto p:tile)require(p.alpha==0,"Zero opacity not empty");
    input.width=input.height=0;input.data=nullptr;s=defaults();run(s,false);for(auto p:tile)require(p.alpha==0,"Empty alpha source not empty");
    s.channel=3;run(s,false);bool any=false;for(auto p:tile)any|=p.alpha>0;require(any,"Solid requires source");
    s=defaults();s.antialias=0;input.width=w;input.height=h;input.data=reinterpret_cast<PF_Pixel*>(src.data());run(s,false);
    for(auto p:tile)require(p.alpha==0 || p.alpha==.5f,"Antialias off not hard edged");
    s.alphaBlack=.8f;s.alphaWhite=.9f;s.opacity=.5f;run(s,false);for(auto p:tile)require(p.alpha==0,"Alpha black point must remove values below it");
    s.alphaBlack=.2f;s.alphaWhite=.8f;run(s,false);for(auto p:tile)require(p.alpha==0 || fabsf(p.alpha-.25f)<1e-6f,"Alpha input levels remapping");
    s.alphaWhite=.4f;run(s,false);for(auto p:tile)require(p.alpha==0 || fabsf(p.alpha-.5f)<1e-6f,"Alpha white point must make values above it fully opaque");
    std::cout<<"PASS: 24 integer-depth comparisons, padded rows, ROI/full-frame identity, zero size/opacity, empty source, solid mode, hard edges, alpha input levels\n";
}
void sourceEdgeRegression() {
    // A small alpha checkout whose visible pixels touch every edge, centered
    // inside a larger output. It must match the same source padded with zeros.
    const int w=120,h=108,cw=23,ch=31,ox=36,oy=24,cp=28;
    std::vector<PF_PixelFloat> cropped(cp*ch),padded(w*h),expected(w*h),actual(w*h);
    std::vector<dots::Pixel> croppedGPU(cp*ch),actualGPU(w*h);
    for(int y=0;y<ch;++y)for(int x=0;x<cw;++x) {
        float a=(x==0 || y==0 || x==cw-1 || y==ch-1 || x==y/2) ? .8f : 0.f;
        cropped[y*cp+x]={a,a*.7f,a*.5f,a*.2f};
        padded[(oy+y)*w+ox+x]=cropped[y*cp+x];
        croppedGPU[y*cp+x]={a*.2f,a*.5f,a*.7f,a};
    }
    CUdeviceptr gs,gd;cu(cuMemAlloc(&gs,croppedGPU.size()*16));cu(cuMemAlloc(&gd,actualGPU.size()*16));
    cu(cuMemcpyHtoD(gs,croppedGPU.data(),croppedGPU.size()*16));
    int cases=0;float maxError=0;
    for(int channel=0;channel<3;++channel)for(int response=0;response<3;++response)
    for(int ds=1;ds<=2;++ds)for(int offset=0;offset<2;++offset) {
        auto s=defaults();s.channel=channel;s.response=response;
        s.size=3.5f;s.gap=1.25f;s.offsetX=offset ? -.75f : 0;s.offsetY=offset ? .35f : 0;
        s.scaleX=s.scaleY=1.f/ds;
        input={};output={};input.width=output.width=w;input.height=output.height=h;
        input.rowbytes=output.rowbytes=w*16;input.data=reinterpret_cast<PF_Pixel*>(padded.data());
        output.data=reinterpret_cast<PF_Pixel*>(expected.data());format=PF_PixelFormat_ARGB128;run(s,false);
        input.width=cw;input.height=ch;input.rowbytes=cp*16;input.origin_x=ox;input.origin_y=oy;
        input.data=reinterpret_cast<PF_Pixel*>(cropped.data());output.data=reinterpret_cast<PF_Pixel*>(actual.data());run(s,false);
        for(size_t i=0;i<actual.size();++i)require(fabsf(actual[i].alpha-expected[i].alpha)<1e-6f,"Source edge repeated outside cropped input (CPU)");
        input.data=reinterpret_cast<PF_Pixel*>(gs);output.data=reinterpret_cast<PF_Pixel*>(gd);
        format=PF_PixelFormat_GPU_BGRA128;run(s,true);
        cu(cuStreamSynchronize(static_cast<CUstream>(gpuInfo.command_queuePV)));cu(cuMemcpyDtoH(actualGPU.data(),gd,actualGPU.size()*16));
        bool visible=false;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            size_t i=size_t(y)*w+x;visible|=actualGPU[i].a>0;
            maxError=std::max(maxError,fabsf(actualGPU[i].a-expected[i].alpha));
            if(x<ox-6 || x>ox+cw+6 || y<oy-6 || y>oy+ch+6)
                require(actualGPU[i].a==0,"Source edge repeated toward output edge (GPU)");
        }
        require(visible,"Regression fixture rendered no visible dots");++cases;
    }
    require(maxError<.0002f,"Cropped source does not match transparent padding (GPU)");
    cu(cuMemFree(gs));cu(cuMemFree(gd));
    std::cout<<"PASS: "<<cases<<" alpha-at-edge regressions on CPU and GPU; all four edges, cropped source origins, three inputs/responses, downsampling and fractional offsets. Max alpha error "<<maxError<<"\n";
}
template<class P> void cpuBenchmark(PF_PixelFormat fmt,float scale,const char* label,float size=8,float gap=4) {
    const int w=3000,h=3000,pitch=3008;
    std::vector<P> source(size_t(pitch)*h),dest(size_t(pitch)*h);
    std::mt19937 rng(42);std::uniform_real_distribution<float> random(0,1);
    for(auto& p:source){using C=decltype(P::alpha);C v=C(random(rng)*scale);p={C(scale),v,v,v};}
    input={};output={};input.width=output.width=w;input.height=output.height=h;
    input.rowbytes=output.rowbytes=pitch*sizeof(P);input.data=reinterpret_cast<PF_Pixel*>(source.data());output.data=reinterpret_cast<PF_Pixel*>(dest.data());format=fmt;
    auto s=defaults();s.antialias=0;s.size=size;s.gap=gap;for(int i=0;i<3;++i)run(s,false);
    std::vector<double> times;
    for(int i=0;i<15;++i){auto begin=std::chrono::steady_clock::now();run(s,false);times.push_back(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());}
    std::sort(times.begin(),times.end());std::cout<<"3000x3000 CPU "<<label<<", 8 workers, 15 frames: median "<<times[7]<<" ms; max "<<times.back()<<" ms\n";
}
void roiAndAlignmentRegression() {
    const int w=200,h=160,pitch=w+1;
    std::vector<PF_PixelFloat> source(pitch*h),full(w*h),tile(43*39);
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)source[y*pitch+x]={1,float((x*17+y*13)%100)/100.f,.6f,.2f};
    const int oldW=in.width,oldH=in.height;in.width=w;in.height=h;
    auto oldSize=defs[Size];auto oldGap=defs[Gap];auto oldAA=defs[Antialias];
    float maxError=0;
    for(float aa:{0.f,1.f})for(float size:{1.6f,7.3f,63.f}) {
        defs[Size].u.fs_d.value=size;defs[Gap].u.fs_d.value=.4f;defs[Antialias].u.fs_d.value=aa;
        input={};output={};input.width=w;input.height=h;input.rowbytes=pitch*16;input.data=reinterpret_cast<PF_Pixel*>(source.data());
        output.width=w;output.height=h;output.rowbytes=w*16;output.data=reinterpret_cast<PF_Pixel*>(full.data());format=PF_PixelFormat_ARGB128;
        auto s=defaults();run(s,false);
        PF_PreRenderInput pi{};pi.output_request.rect={71,63,114,102};pi.what_gpu=PF_GPU_Framework_CUDA;
        PF_PreRenderOutput po{};PF_PreRenderExtra pe{&pi,&po,&preCallbacks};ok(entry(PF_Cmd_SMART_PRE_RENDER,&in,&out,nullptr,nullptr,&pe));
        PF_LRect request=lastRequest.rect;
        require(request.left>0 && request.right<w && request.top>0 && request.bottom<h,"ROI still requests full source");
        int cw=request.right-request.left,ch=request.bottom-request.top;
        // Deliberately 4-byte-aligned, non-16-byte-aligned source rows.
        int stride=cw*16+4;std::vector<float> cropped(size_t(stride/4)*ch+1);
        char* data=reinterpret_cast<char*>(cropped.data()+1);
        for(int y=0;y<ch;++y)memcpy(data+y*stride,&source[(request.top+y)*pitch+request.left],cw*16);
        input.width=cw;input.height=ch;input.origin_x=request.left;input.origin_y=request.top;input.rowbytes=stride;input.data=reinterpret_cast<PF_Pixel*>(data);
        int outStride=43*16+4;std::vector<float> target(size_t(outStride/4)*39+1,-123.f);
        output.width=43;output.height=39;output.rowbytes=outStride;output.origin_x=71;output.origin_y=63;output.data=reinterpret_cast<PF_Pixel*>(target.data()+1);
        run(*static_cast<dots::Settings*>(po.pre_render_data),false);
        for(int y=0;y<39;++y){auto row=reinterpret_cast<PF_PixelFloat*>(reinterpret_cast<char*>(output.data)+y*outStride);
            for(int x=0;x<43;++x)maxError=std::max(maxError,fabsf(row[x].alpha-full[(63+y)*w+71+x].alpha));
            require(*reinterpret_cast<float*>(reinterpret_cast<char*>(row)+43*16)==-123.f,"Row padding overwritten");}
        po.delete_pre_render_data_func(po.pre_render_data);
    }
    require(maxError<.0002f,"ROI sample halo or unaligned output regression");
    defs[Size]=oldSize;defs[Gap]=oldGap;defs[Antialias]=oldAA;in.width=oldW;in.height=oldH;
    std::cout<<"PASS: ROI-only source checkout equals full source; AA on/off, dense/large dots, 4-byte-aligned rows and padding. Max alpha error "<<maxError<<"\n";
}
void colorAndRepeatRegression() {
    const int w=64,h=64,cw=16;
    std::vector<PF_PixelFloat> src(cw*cw,PF_PixelFloat{.25f,.25f,.25f,.25f}),dst(w*h);
    std::vector<dots::Pixel> gsPixels(cw*cw,dots::Pixel{.25f,.25f,.25f,.25f}),got(w*h);
    CUdeviceptr gs,gd;cu(cuMemAlloc(&gs,gsPixels.size()*16));cu(cuMemAlloc(&gd,got.size()*16));cu(cuMemcpyHtoD(gs,gsPixels.data(),gsPixels.size()*16));
    input={};output={};input.width=input.height=cw;input.rowbytes=cw*16;input.origin_x=input.origin_y=24;
    output.width=w;output.height=h;output.rowbytes=w*16;
    auto s=defaults();s.red=1;s.green=.6f;s.blue=.2f;s.size=3;s.gap=2;s.antialias=1;
    for(int repeat=0;repeat<2;++repeat)for(float opacity:{.01f,.1f,.5f,1.f}) {
        s.repeatEdges=repeat;s.opacity=opacity;
        input.data=reinterpret_cast<PF_Pixel*>(src.data());output.data=reinterpret_cast<PF_Pixel*>(dst.data());format=PF_PixelFormat_ARGB128;run(s,false);
        input.data=reinterpret_cast<PF_Pixel*>(gs);output.data=reinterpret_cast<PF_Pixel*>(gd);format=PF_PixelFormat_GPU_BGRA128;run(s,true);
        cu(cuStreamSynchronize(static_cast<CUstream>(gpuInfo.command_queuePV)));cu(cuMemcpyDtoH(got.data(),gd,got.size()*16));
        bool seenEdge=false,seenSoft=false;
        for(int y=0;y<h;++y)for(int x=0;x<w;++x) {
            const auto& p=got[y*w+x];const auto& c=dst[y*w+x];
            require(fabsf(p.a-c.alpha)<1e-6f && p.r==c.red && p.g==c.green && p.b==c.blue,"Repeat/color CPU GPU mismatch");
            if(p.a>0) {
                require(p.r==s.red && p.g==s.green && p.b==s.blue,"Raising alpha would expose dark dot RGB");
                if(p.a<.2f)seenSoft=true;
                if(x<5 || y<5 || x>w-6 || y>h-6)seenEdge=true;
            }
        }
        require(seenSoft,"Missing translucent dot fixture");
        require(seenEdge==(repeat!=0),"Repeat Edges checkbox does not control extension");
    }
    cu(cuMemFree(gs));cu(cuMemFree(gd));
    std::cout<<"PASS: Repeat Edges off/on and fixed RGB through 1%, 10%, 50%, 100% opacity on CPU/GPU; alpha-only Levels cannot expose gray dots\n";
}
void benchmark() {
    const int w=3000,h=3000,pitch=3008;const size_t bytes=size_t(pitch)*h*16;
    std::vector<dots::Pixel> noise(size_t(pitch)*h);
    std::mt19937 rng(42);std::uniform_real_distribution<float> random(0,1);
    for(auto& p:noise){float v=random(rng);p={v,v,v,1};}
    CUdeviceptr gs,gd;cu(cuMemAlloc(&gs,bytes));cu(cuMemAlloc(&gd,bytes));cu(cuMemcpyHtoD(gs,noise.data(),bytes));
    input={};output={};input.width=output.width=w;input.height=output.height=h;
    input.rowbytes=output.rowbytes=pitch*16;input.data=reinterpret_cast<PF_Pixel*>(gs);output.data=reinterpret_cast<PF_Pixel*>(gd);
    format=PF_PixelFormat_GPU_BGRA128;
    CUstream stream=static_cast<CUstream>(gpuInfo.command_queuePV);
    CUevent start,end;cu(cuEventCreate(&start,0));cu(cuEventCreate(&end,0));
    for(int response=0;response<3;++response) {
        auto s=defaults();s.response=response;s.antialias=0;
        for(int i=0;i<30;++i)run(s,true);cu(cuStreamSynchronize(stream));
        std::vector<float> times;
        for(int i=0;i<150;++i) {
            cu(cuEventRecord(start,stream));run(s,true);cu(cuEventRecord(end,stream));cu(cuEventSynchronize(end));
            float ms;cu(cuEventElapsedTime(&ms,start,end));times.push_back(ms);
        }
        std::sort(times.begin(),times.end());
        std::cout<<"3000x3000, response "<<response<<", 150 frames: GPU median "<<times[75]<<" ms; p95 "<<times[142]<<" ms; max "<<times.back()<<" ms\n";
    }
    auto begin=std::chrono::steady_clock::now();
    cu(cuMemcpyHtoD(gs,noise.data(),bytes));run(defaults(),true);cu(cuStreamSynchronize(stream));cu(cuMemcpyDtoH(noise.data(),gd,bytes));
    std::cout<<"Pageable host upload + effect + download: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()<<" ms\n";
    // Actual rendered noise output, for visual inspection. Simple uncompressed 32-bit TGA.
    std::ofstream image("noise-output.tga",std::ios::binary);unsigned char header[18]={0,0,2};
    header[12]=w&255;header[13]=w>>8;header[14]=h&255;header[15]=h>>8;header[16]=32;header[17]=0x28;
    image.write(reinterpret_cast<char*>(header),18);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x){auto p=noise[y*pitch+x];unsigned char c[4]={255,255,255,(unsigned char)(p.a*255+.5f)};image.write(reinterpret_cast<char*>(c),4);}
    cu(cuEventDestroy(start));cu(cuEventDestroy(end));cu(cuMemFree(gs));cu(cuMemFree(gd));
}
}
int main(int argc,char** argv) {
    try {
        test::require(argc==2,"Usage: Verify.exe plugin.aex");test::init(argv[1]);
        test::sourceEdgeRegression();test::roiAndAlignmentRegression();test::colorAndRepeatRegression();test::correctness();test::edgeCases();test::benchmark();test::cpuBenchmark<PF_Pixel8>(PF_PixelFormat_ARGB32,255,"8 bpc");test::cpuBenchmark<PF_Pixel16>(PF_PixelFormat_ARGB64,32768,"16 bpc");test::cpuBenchmark<PF_PixelFloat>(PF_PixelFormat_ARGB128,1,"32 bpc");test::cpuBenchmark<PF_Pixel8>(PF_PixelFormat_ARGB32,255,"dense 8 bpc (size 1.6, gap 0.4)",1.6f,.4f);
        PF_GPUDeviceSetdownInput si{test::setupOutput.gpu_data,PF_GPU_Framework_CUDA,0};PF_GPUDeviceSetdownExtra se{&si};
        test::ok(test::entry(PF_Cmd_GPU_DEVICE_SETDOWN,&test::in,&test::out,nullptr,nullptr,&se));
        std::cout<<"PASS: GPU teardown\n";return 0;
    } catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
