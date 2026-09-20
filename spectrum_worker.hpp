#pragma once
#include "analysis.hpp"
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace lvm {
// A single worker with one replaceable pending request, independent of Win32.
class SpectrumWorker {
public:
    struct Result { std::uint64_t generation; Spectrum spectrum; };
    struct AffineTransform { double mul = 1.0, add = 0.0; };
    SpectrumWorker() = default;
    ~SpectrumWorker();
    void submit(Dataset data, std::vector<std::size_t> source_channels, std::uint64_t generation,
                std::vector<AffineTransform> transforms = {});
    void cancel();
    std::optional<Result> take_result();
private:
    struct Request {
        Dataset data;
        std::vector<std::size_t> source_channels;
        std::vector<AffineTransform> transforms;
        std::uint64_t generation;
        std::shared_ptr<std::atomic<bool>> cancelled;
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
