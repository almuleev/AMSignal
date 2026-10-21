// Optional OpenCL/VkFFT adapter. No OpenCL import library or installed SDK.
#include "gpu_fft.hpp"
#include "fft.hpp"
#include "analysis_executor.hpp"
#include "third_party/NOTICES.inc"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <mutex>
#include <stdexcept>
#include <tuple>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#define CL_TARGET_OPENCL_VERSION 120
#include "third_party/OpenCL-Headers/CL/cl.h"

namespace {
#define OPENCL_FUNCTIONS(X) \
 X(clGetPlatformIDs) X(clGetDeviceIDs) X(clGetDeviceInfo) X(clCreateContext) \
 X(clReleaseContext) X(clCreateCommandQueue) X(clReleaseCommandQueue) \
 X(clCreateBuffer) X(clReleaseMemObject) X(clCreateProgramWithSource) \
 X(clCreateProgramWithBinary) X(clBuildProgram) X(clGetProgramInfo) \
 X(clGetProgramBuildInfo) X(clReleaseProgram) X(clCreateKernel) X(clReleaseKernel) \
 X(clSetKernelArg) X(clEnqueueNDRangeKernel) X(clEnqueueWriteBuffer) \
 X(clEnqueueReadBuffer) X(clFinish)
#define DECLARE_CL(name) decltype(&name) api_##name=nullptr;
OPENCL_FUNCTIONS(DECLARE_CL)
#undef DECLARE_CL
}
// VkFFT calls the dynamically resolved API; its original headers are unmodified.
#define clGetDeviceInfo api_clGetDeviceInfo
#define clCreateCommandQueue api_clCreateCommandQueue
#define clReleaseCommandQueue api_clReleaseCommandQueue
#define clCreateBuffer api_clCreateBuffer
#define clReleaseMemObject api_clReleaseMemObject
#define clCreateProgramWithSource api_clCreateProgramWithSource
#define clCreateProgramWithBinary api_clCreateProgramWithBinary
#define clBuildProgram api_clBuildProgram
#define clGetProgramInfo api_clGetProgramInfo
#define clGetProgramBuildInfo api_clGetProgramBuildInfo
#define clReleaseProgram api_clReleaseProgram
#define clCreateKernel api_clCreateKernel
#define clReleaseKernel api_clReleaseKernel
#define clSetKernelArg api_clSetKernelArg
#define clEnqueueNDRangeKernel api_clEnqueueNDRangeKernel
#define clEnqueueWriteBuffer api_clEnqueueWriteBuffer
#define clEnqueueReadBuffer api_clEnqueueReadBuffer
#define clFinish api_clFinish
#define VKFFT_BACKEND 3
#include "third_party/VkFFT/vkFFT/vkFFT.h"

namespace lvm {
namespace {
using Complex=std::complex<double>;
using Clock=std::chrono::steady_clock;
constexpr std::size_t max_transfer=64ULL*1024*1024;
void cancelled(const std::atomic<bool>* cancel) {
    if(cancel && cancel->load(std::memory_order_relaxed))throw std::runtime_error("Operation cancelled.");
}
ComputeBackend initial_backend() {
    const auto* value=std::getenv("AMSIGNAL_COMPUTE");
    if(value && std::string(value)=="cpu")return ComputeBackend::Cpu;
    if(value && std::string(value)=="gpu")return ComputeBackend::Gpu;
    return ComputeBackend::Auto;
}
std::atomic<ComputeBackend> backend{initial_backend()};
std::atomic<bool> unavailable{false};
struct State {
    std::mutex mutex;
    GpuFftStatus status;
    cl_device_id device=nullptr;
    cl_context context=nullptr;
    cl_command_queue queue=nullptr;
    struct Plan {
        cl_mem buffer=nullptr;
        VkFFTApplication app{};
        bool initialized=false;
        pfUINT bytes=0;
        ~Plan() {
            if(initialized)deleteVkFFT(&app);
            if(buffer)api_clReleaseMemObject(buffer);
        }
    };
    std::map<std::pair<std::size_t,std::size_t>,std::unique_ptr<Plan>> plans;
    cl_ulong global_bytes=0,max_allocation=0;
    // Context identifies device + driver. Decisions are session-local and bounded.
    std::map<std::tuple<std::size_t,std::size_t,std::size_t>,bool> choices;
    std::set<std::tuple<std::size_t,std::size_t,std::size_t>> rejected_plans;
    std::string calibration_detail;
    long long test_failure_after=-1;
    bool test_failure_configured=false;
#ifdef _WIN32
    HMODULE library=nullptr;
#else
    void* library=nullptr;
#endif
    void clear_plan() {
        plans.clear();
    }
    ~State() {
        clear_plan();
        if(queue)api_clReleaseCommandQueue(queue);
        if(context)api_clReleaseContext(context);
#ifdef _WIN32
        if(library)FreeLibrary(library);
#else
        if(library)dlclose(library);
#endif
    }
    void* symbol(const char* name) {
#ifdef _WIN32
        return reinterpret_cast<void*>(GetProcAddress(library,name));
#else
        return dlsym(library,name);
#endif
    }
    std::string text(cl_device_id d,cl_device_info key) {
        std::size_t size=0;
        if(api_clGetDeviceInfo(d,key,0,nullptr,&size)!=CL_SUCCESS || !size || size>65536)return {};
        std::string value(size,'\0');
        if(api_clGetDeviceInfo(d,key,size,value.data(),nullptr)!=CL_SUCCESS)return {};
        while(!value.empty() && !value.back())value.pop_back();
        return value;
    }
    void discover() {
        if(status.attempted)return;
        status.attempted=true;status.detail="No compatible FP64 OpenCL GPU";
        if(std::getenv("AMSIGNAL_OPENCL_DISABLE")) {status.detail="OpenCL disabled";return;}
#ifdef _WIN32
        library=LoadLibraryExW(L"OpenCL.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
#else
        library=dlopen("libOpenCL.so.1",RTLD_NOW|RTLD_LOCAL);
#endif
        if(!library) {status.detail="OpenCL runtime not installed";return;}
#define LOAD_CL(name) api_##name=reinterpret_cast<decltype(api_##name)>(symbol(#name)); if(!api_##name){status.detail="Incomplete OpenCL runtime";return;}
        // Avoid macro expansion of VkFFT's aliases here.
        OPENCL_FUNCTIONS(LOAD_CL)
#undef LOAD_CL
        cl_uint platforms=0;
        if(api_clGetPlatformIDs(0,nullptr,&platforms)!=CL_SUCCESS || !platforms || platforms>64)return;
        std::vector<cl_platform_id> ids(platforms);
        if(api_clGetPlatformIDs(platforms,ids.data(),nullptr)!=CL_SUCCESS)return;
        // Prefer the usable GPU with the largest memory; never choose a CPU ICD.
        for(auto p:ids) {
            cl_uint devices=0;
            if(api_clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,0,nullptr,&devices)!=CL_SUCCESS || !devices || devices>64)continue;
            std::vector<cl_device_id> ds(devices);
            if(api_clGetDeviceIDs(p,CL_DEVICE_TYPE_GPU,devices,ds.data(),nullptr)!=CL_SUCCESS)continue;
            for(auto d:ds) {
                cl_device_fp_config fp64=0;cl_bool ready=CL_FALSE,compiler=CL_FALSE;cl_ulong memory=0,allocation=0;
                api_clGetDeviceInfo(d,CL_DEVICE_DOUBLE_FP_CONFIG,sizeof fp64,&fp64,nullptr);
                api_clGetDeviceInfo(d,CL_DEVICE_AVAILABLE,sizeof ready,&ready,nullptr);
                api_clGetDeviceInfo(d,CL_DEVICE_COMPILER_AVAILABLE,sizeof compiler,&compiler,nullptr);
                api_clGetDeviceInfo(d,CL_DEVICE_GLOBAL_MEM_SIZE,sizeof memory,&memory,nullptr);
                api_clGetDeviceInfo(d,CL_DEVICE_MAX_MEM_ALLOC_SIZE,sizeof allocation,&allocation,nullptr);
                constexpr cl_device_fp_config required=CL_FP_DENORM|CL_FP_INF_NAN|CL_FP_ROUND_TO_NEAREST;
                if((fp64&required)==required && ready && compiler && allocation>=1024*1024 && memory>global_bytes) {
                    device=d;global_bytes=memory;max_allocation=allocation;
                }
            }
        }
        if(!device)return;
        cl_int err=CL_SUCCESS;
        context=api_clCreateContext(nullptr,1,&device,nullptr,nullptr,&err);
        if(err!=CL_SUCCESS || !context)return;
        queue=api_clCreateCommandQueue(context,device,0,&err);
        if(err!=CL_SUCCESS || !queue)return;
        status.available=true;status.device=text(device,CL_DEVICE_NAME);status.driver=text(device,CL_DRIVER_VERSION);
        status.detail="FP64 OpenCL GPU ready";
    }
    Plan* plan(std::size_t n,std::size_t batches) {
        const auto key=std::make_pair(n,batches);
        const auto found=plans.find(key);if(found!=plans.end())return found->second.get();
        const auto bytes=n*batches*sizeof(Complex);
        // Reserve headroom for VkFFT's temporary/LUT/Bluestein allocations and desktop.
        if(bytes>max_allocation/8 || bytes>global_bytes/16)return nullptr;
        // Three bounded plans avoid replanning the tail of every FRF tile.
        std::size_t retained=0;for(const auto& p:plans)retained+=p.second->bytes;
        if(plans.size()>=3 || retained+bytes>2*max_transfer)clear_plan();
        auto owned=std::make_unique<Plan>();auto& p=*owned;p.bytes=bytes;
        cl_int err=0;p.buffer=api_clCreateBuffer(context,CL_MEM_READ_WRITE,bytes,nullptr,&err);
        if(err!=CL_SUCCESS || !p.buffer)return nullptr;
        VkFFTConfiguration config{};
        config.FFTdim=1;config.size[0]=n;config.numberBatches=batches;
        config.doublePrecision=1;config.makeForwardPlanOnly=1;
        config.device=&device;config.context=&context;config.commandQueue=&queue;
        config.buffer=&p.buffer;config.bufferSize=&p.bytes;
        const auto result=initializeVkFFT(&p.app,config);
        if(result!=VKFFT_SUCCESS) {
            // VkFFT cleans partial plans on initialization failure.
            p.app={};status.detail="VkFFT plan rejected: "+std::to_string(result);return nullptr;
        }
        p.initialized=true;auto* result_plan=owned.get();plans.emplace(key,std::move(owned));return result_plan;
    }
};
State& state() {static State value;return value;}
void cpu_batch(const std::vector<std::vector<Complex>*>& signals,const std::atomic<bool>* cancel,
               const AnalysisResources& resources) {
    const auto job=[&](std::size_t i) {
        auto& a=*signals[i];
        if((a.size()&(a.size()-1))==0)fft_radix2(a,false,cancel);
        else a=dft(a,cancel);
    };
    if(!analysis_executor().run(signals.size(),resources.max_participants,job))
        for(std::size_t i=0;i<signals.size();++i)job(i);
}
}
void set_compute_backend(ComputeBackend value) {backend.store(value,std::memory_order_relaxed);}
ComputeBackend compute_backend() {return backend.load(std::memory_order_relaxed);}
bool gpu_fft_candidate(std::size_t n,std::size_t batches) {
    const auto mode=compute_backend();
    if(mode==ComputeBackend::Cpu || unavailable.load(std::memory_order_relaxed) ||
        !batches || n<4096 || n>max_transfer/sizeof(Complex) ||
        (n<65536 && batches<(65536+n-1)/n))return false;
    auto& s=state();std::unique_lock<std::mutex> lock(s.mutex,std::try_to_lock);
    if(!lock.owns_lock())return false;
    // Keep the fused prepare+CPU FFT job after an unsuccessful Auto calibration.
    // A known CPU win is conservative even if the caller later changes CPU count.
    if(mode==ComputeBackend::Auto)for(const auto& choice:s.choices)
        if(std::get<0>(choice.first)==n && std::get<1>(choice.first)==batches && !choice.second)return false;
    return true;
}
GpuFftStatus gpu_fft_status() {
    auto& s=state();std::unique_lock<std::mutex> lock(s.mutex,std::try_to_lock);
    if(!lock.owns_lock()) {GpuFftStatus status;status.detail="GPU job active";return status;}
    return s.status;
}
bool try_gpu_fft(const std::vector<std::vector<Complex>*>& signals,const std::atomic<bool>* cancel,
                 const AnalysisResources& resources) {
    cancelled(cancel);
    const auto mode=compute_backend();
    if(mode==ComputeBackend::Cpu || signals.empty() || unavailable.load(std::memory_order_relaxed))return false;
    if(!signals.front())return false;
    const auto n=signals.front()->size(),batches=signals.size();
    // Admission before discovery makes small operations independent of the runtime.
    if(n<4096 || n>max_transfer/sizeof(Complex) ||
        (n<65536 && batches<(65536+n-1)/n))return false;
    for(auto* a:signals)if(!a || a->size()!=n)return false;
    // Split independent FFTs, never the samples of a single FFT. Reserve enough
    // for both readback and a calibration copy, even before a shape is known.
    const auto per_signal=n*sizeof(Complex);
    auto max_batch=std::min(max_transfer/per_signal,resources.memory_bytes/(2*per_signal));
    if(resources.budget)max_batch=std::min<std::size_t>(max_batch,resources.budget->available()/(2*per_signal));
    if(!max_batch)return false;
    if(batches>max_batch) {
        bool transformed=false;
        for(std::size_t first=0;first<batches;first+=max_batch) {
            cancelled(cancel);
            const auto end=std::min(batches,first+max_batch);
            std::vector<std::vector<Complex>*> part(signals.begin()+first,signals.begin()+end);
            auto part_resources=resources;part_resources.max_participants=std::min(resources.max_participants,part.size());
            if(!try_gpu_fft(part,cancel,part_resources)) {
                // Before any mutation, preserve the caller's fused CPU path.
                if(!transformed)return false;
                // Earlier groups already contain FFTs: finish only this group.
                cpu_batch(part,cancel,part_resources);
            }
            transformed=true;
        }
        return true;
    }
    auto& s=state();std::unique_lock<std::mutex> lock(s.mutex,std::try_to_lock);
    if(!lock.owns_lock())return false;
    const auto key=std::make_tuple(n,batches,resources.max_participants);
    if(s.rejected_plans.count(key))return false;
    const auto found=s.choices.find(key);
    if(mode==ComputeBackend::Auto && found!=s.choices.end() && !found->second)return false;
    const auto size=n*batches*sizeof(Complex);
    const bool calibrate=found==s.choices.end();
    const auto host_bytes=size*(calibrate ? 2 : 1);
    if(host_bytes>resources.memory_bytes)return false;
    // Scratch readback is additional host RAM, admitted against the shared ledger.
    std::shared_ptr<MemoryBudget::Reservation> reservation;
    if(resources.budget) {reservation=resources.budget->reserve(host_bytes);if(!reservation)return false;}
    try {
        s.discover();if(!s.status.available){unavailable.store(true,std::memory_order_relaxed);return false;}
        auto* plan=s.plan(n,batches);
        if(!plan) {
            if(s.rejected_plans.size()>=64)s.rejected_plans.clear();
            s.rejected_plans.insert(key);return false;
        }
        cancelled(cancel);
        std::vector<Complex> output(n*batches);
        const auto gpu_run=[&] {
            // Diagnostic fault injection exercises recovery after completed batches.
            if(!s.test_failure_configured)if(const auto* value=std::getenv("AMSIGNAL_GPU_TEST_FAIL_AFTER")) {
                char* end=nullptr;const auto count=std::strtoll(value,&end,10);
                if(end!=value && !*end && count>=0 && count<=1000000) {
                    s.test_failure_after=count;s.test_failure_configured=true;
                }
            }
            if(s.test_failure_after==0)throw std::runtime_error("Injected OpenCL execution failure");
            const auto started=Clock::now();
            for(std::size_t i=0;i<batches;++i) {
                cancelled(cancel);
                if(api_clEnqueueWriteBuffer(s.queue,plan->buffer,CL_TRUE,i*n*sizeof(Complex),n*sizeof(Complex),
                                           signals[i]->data(),0,nullptr,nullptr)!=CL_SUCCESS)throw std::runtime_error("OpenCL upload failed");
            }
            VkFFTLaunchParams launch{};launch.commandQueue=&s.queue;
            if(VkFFTAppend(&plan->app,-1,&launch)!=VKFFT_SUCCESS || api_clFinish(s.queue)!=CL_SUCCESS)
                throw std::runtime_error("OpenCL FFT failed");
            cancelled(cancel);
            if(api_clEnqueueReadBuffer(s.queue,plan->buffer,CL_TRUE,0,size,output.data(),0,nullptr,nullptr)!=CL_SUCCESS)
                throw std::runtime_error("OpenCL download failed");
            cancelled(cancel);++s.status.gpu_batches;
            if(s.test_failure_after>0)--s.test_failure_after;
            return std::chrono::duration<double>(Clock::now()-started).count();
        };
        // First batch for a shape validates numerics and compares full warm GPU
        // transfer+FFT+readback time with the existing CPU executor.
        if(calibrate) {
            std::vector<std::vector<Complex>> cpu(batches);
            std::vector<std::vector<Complex>*> pointers;for(auto& a:cpu)pointers.push_back(&a);
            double gpu_times[3],cpu_times[3];
            for(std::size_t trial=0;trial<3;++trial) {
                for(std::size_t i=0;i<batches;++i)cpu[i]=*signals[i];
                const auto cpu_run=[&] {
                    const auto started=Clock::now();cpu_batch(pointers,cancel,resources);
                    return std::chrono::duration<double>(Clock::now()-started).count();
                };
                if(trial%2) {cpu_times[trial]=cpu_run();gpu_times[trial]=gpu_run();}
                else {gpu_times[trial]=gpu_run();cpu_times[trial]=cpu_run();}
            }
            ++s.status.calibrations;
            double scale=0,error=0;
            for(std::size_t i=0;i<batches;++i)for(std::size_t j=0;j<n;++j) {
                if((j&65535)==0)cancelled(cancel);
                const auto a=cpu[i][j],b=output[i*n+j];
                if(!std::isfinite(std::abs(a)) || !std::isfinite(std::abs(b))) {error=INFINITY;break;}
                scale=std::max(scale,std::abs(a));error=std::max(error,std::abs(a-b));
            }
            const bool valid=std::isfinite(error) && static_cast<long double>(error)<=1e-9L*scale;
            std::sort(std::begin(gpu_times),std::end(gpu_times));std::sort(std::begin(cpu_times),std::end(cpu_times));
            if(s.choices.size()>=64)s.choices.clear();
            // Preparation and long-double FRF accumulation stay on CPU. Require
            // a large FFT margin to offset lost CPU cache locality and transfers.
            s.choices[key]=valid && gpu_times[1]<cpu_times[1]*0.25;
            s.calibration_detail="N="+std::to_string(n)+", batch="+std::to_string(batches)+
                ", CPU="+std::to_string(cpu_times[1]*1000)+" ms, GPU="+std::to_string(gpu_times[1]*1000)+" ms";
            if(!valid) {s.status.available=false;unavailable=true;s.status.detail="GPU numerical validation failed; CPU retained";s.clear_plan();}
            if(!valid || (mode==ComputeBackend::Auto && !s.choices[key])) {
                cancelled(cancel);for(std::size_t i=0;i<batches;++i)signals[i]->swap(cpu[i]);
                if(valid)s.status.detail="CPU retained: GPU margin below 4x; "+s.calibration_detail;
                return true;
            }
        } else gpu_run();
        // No cancellation/failure points during publication: never expose a mix.
        cancelled(cancel);
        for(std::size_t i=0;i<batches;++i)std::copy_n(output.data()+i*n,n,signals[i]->data());
        s.status.detail="GPU FFT completed; "+s.calibration_detail;return true;
    } catch(const std::exception& e) {
        cancelled(cancel);
        s.status.available=false;unavailable=true;s.status.detail=std::string("GPU disabled after error: ")+e.what();s.clear_plan();
        return false;
    }
}
}
