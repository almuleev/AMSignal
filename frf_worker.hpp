#pragma once
#include "frf_analysis.hpp"
#include "frf_stream.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>
#include <memory>
#include <cstdint>

namespace lvm {
class FrfWorker {
public:
    struct Result { std::uint64_t generation; FrfBatchResult frf; };
    FrfWorker() = default;
    ~FrfWorker();
    // File-independent snapshot; preparation and FFT both run in this worker.
    void submit(FrfInput data, FrfOptions options, std::uint64_t generation);
    void submit(FrfBatchInput data, FrfOptions options, std::uint64_t generation);
    void submit(FrfBatchInput data,FrfOptions options,std::uint64_t generation,std::string input_key,
                AnalysisResources resources={},std::shared_ptr<MemoryBudget::Reservation> reservation={});
    void submit(FrfSource source,FrfOptions options,std::uint64_t generation,std::string input_key,
                AnalysisResources resources={},std::shared_ptr<MemoryBudget::Reservation> reservation={});
    bool submit_cached(FrfOptions options,std::uint64_t generation,const std::string& input_key,
                       AnalysisResources resources={});
    void cancel();
    void clear_cache();
    void trim_cache(std::size_t memory_bytes,std::size_t disk_bytes);
    void shutdown();
    std::optional<Result> take_result();
private:
    struct Request {
        std::shared_ptr<MemoryBudget::Reservation> memory_reservation;
        FrfBatchInput data;
        FrfSource source;
        std::string input_key;
        FrfOptions options;
        std::uint64_t generation;
        std::shared_ptr<std::atomic<bool>> cancelled;
        AnalysisResources resources;
        std::uint64_t cache_epoch=0;
        std::shared_ptr<const PreparedFrf> prepared;
    };
    void run();
    std::mutex mutex_;
    std::condition_variable ready_;
    std::thread thread_;
    std::optional<Request> pending_;
    std::optional<Result> result_;
    std::shared_ptr<std::atomic<bool>> active_cancel_;
    bool stopping_ = false;
    std::shared_ptr<const PreparedFrf> cached_input_;
    std::string cached_key_;
    std::uint64_t cache_epoch_=0;
    std::size_t cache_memory_limit_=std::numeric_limits<std::size_t>::max();
    std::size_t cache_disk_limit_=std::numeric_limits<std::size_t>::max();
};
} // namespace lvm
