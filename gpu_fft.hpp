#pragma once
#include "analysis_resources.hpp"
#include <atomic>
#include <complex>
#include <string>
#include <vector>

namespace lvm {
enum class ComputeBackend { Auto, Cpu, Gpu };
void set_compute_backend(ComputeBackend backend);
ComputeBackend compute_backend();
struct GpuFftStatus {
    bool attempted=false, available=false;
    std::string device, driver, detail;
    std::size_t gpu_batches=0, calibrations=0;
};
// Does not discover devices or wait for a running GPU job.
GpuFftStatus gpu_fft_status();
bool gpu_fft_candidate(std::size_t length,std::size_t signals);
// Forward C2C, in place, unnormalised, same length for every independent signal.
// true: all signals transformed (GPU, CPU calibration or recovery).
// false: inputs untouched; caller must use its existing CPU path.
// Cancellation throws; no partially downloaded GPU result is published.
// resources.memory_bytes is the remaining host workspace after CPU admission.
bool try_gpu_fft(const std::vector<std::vector<std::complex<double>>*>& signals,
                 const std::atomic<bool>* cancel=nullptr,
                 const AnalysisResources& resources={});
}
