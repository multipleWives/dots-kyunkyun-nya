#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cuda.h>

// Load only the system driver. The effect has no CUDA Toolkit/runtime DLL dependency.
struct CudaDriver {
    HMODULE dll = nullptr;
    decltype(&cuInit) init = nullptr;
    decltype(&cuCtxPushCurrent) push = nullptr;
    decltype(&cuCtxPopCurrent) pop = nullptr;
    decltype(&cuModuleLoadData) load = nullptr;
    decltype(&cuModuleUnload) unload = nullptr;
    decltype(&cuModuleGetFunction) function = nullptr;
    decltype(&cuLaunchKernel) launch = nullptr;
    decltype(&cuStreamSynchronize) sync = nullptr;
    bool open() {
        dll = LoadLibraryExW(L"nvcuda.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dll) return false;
#define DOT_LOAD(member, name) member = reinterpret_cast<decltype(member)>(GetProcAddress(dll, name)); if (!member) return false
        DOT_LOAD(init,"cuInit");
        DOT_LOAD(push,"cuCtxPushCurrent_v2");
        DOT_LOAD(pop,"cuCtxPopCurrent_v2");
        DOT_LOAD(load,"cuModuleLoadData");
        DOT_LOAD(unload,"cuModuleUnload");
        DOT_LOAD(function,"cuModuleGetFunction");
        DOT_LOAD(launch,"cuLaunchKernel");
        DOT_LOAD(sync,"cuStreamSynchronize");
#undef DOT_LOAD
        return init(0) == CUDA_SUCCESS;
    }
    ~CudaDriver() { if (dll) FreeLibrary(dll); }
    CudaDriver() = default;
    CudaDriver(const CudaDriver&) = delete;
    CudaDriver& operator=(const CudaDriver&) = delete;
};
struct CudaContextScope {
    CudaDriver& driver;
    bool pushed;
    CudaContextScope(CudaDriver& d, CUcontext c) : driver(d), pushed(d.push(c)==CUDA_SUCCESS) {}
    ~CudaContextScope() { if (pushed) { CUcontext c; driver.pop(&c); } }
};
