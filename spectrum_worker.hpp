#pragma once
#include "analysis.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <functional>

namespace lvm {
// A single worker with one replaceable pending request, independent of Win32.
class SpectrumWorker {
public:
    struct Result { std::uint64_t generation; Spectrum spectrum; };
    struct AffineTransform { double mul = 1.0, add = 0.0; };
    using Prepare = std::function<Dataset(const std::atomic<bool>*,std::shared_ptr<MemoryBudget::Reservation>&)>;
    using Compute = std::function<Spectrum(const std::atomic<bool>*)>;
    SpectrumWorker() = default;
    ~SpectrumWorker();
    void submit(Dataset data, std::vector<std::size_t> source_channels, std::uint64_t generation,
                std::vector<AffineTransform> transforms = {},AnalysisResources resources = {},
                std::shared_ptr<MemoryBudget::Reservation> reservation = {});
    void submit_source(Prepare prepare,std::vector<std::size_t> source_channels,
                       std::uint64_t generation,AnalysisResources resources);
    void submit_computation(Compute compute,std::vector<std::size_t> source_channels,std::uint64_t generation);
    void cancel();
    // Terminal, idempotent cancel+join. Call before destroying process-wide
    // analysis resources; subsequent submissions are ignored.
    void shutdown();
    std::optional<Result> take_result();
private:
    struct Request {
        std::shared_ptr<MemoryBudget::Reservation> memory_reservation;
        Dataset data;
        std::vector<std::size_t> source_channels;
        std::vector<AffineTransform> transforms;
        std::uint64_t generation;
        std::shared_ptr<std::atomic<bool>> cancelled;
        AnalysisResources resources;
        Prepare prepare;
        Compute compute;
    };
    void run();
    std::mutex mutex_;
    std::condition_variable ready_;
    std::thread thread_;
    std::optional<Request> pending_;
    std::optional<Result> result_;
    std::shared_ptr<std::atomic<bool>> active_cancel_;
    bool stopping_ = false;
};
}
