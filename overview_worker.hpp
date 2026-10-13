#pragma once
#include "recording_overview.hpp"
#include "disk_cache.hpp"
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace lvm {
// One reader/build thread, one replaceable request. No FFT executor or UI calls.
class OverviewWorker {
public:
    enum class Phase { Idle, Building, Reading };
    struct Progress {
        std::uint64_t generation=0, rows=0, disk_bytes=0;
        Phase phase=Phase::Idle;
    };
    struct Request {
        std::uint64_t generation=0;
        std::shared_ptr<const RecordingSource> source;
        std::shared_ptr<const RecordingOverview> index;
        std::filesystem::path cache;
        std::shared_ptr<DiskCache> managed_cache;
        OverviewOptions build;
        OverviewRequest view;
        bool gap_details=false;
        double gap_step=-1;
        std::size_t cache_bytes=0;
    };
    struct Result {
        std::shared_ptr<MemoryBudget::Reservation> details_reservation;
        std::uint64_t generation=0;
        std::shared_ptr<const RecordingSource> source;
        std::shared_ptr<const RecordingOverview> index;
        std::shared_ptr<const OverviewView> view;
        std::vector<std::size_t> channels;
        std::optional<RecordingError> error;
        double gap_step=-1;
        std::vector<OverviewGap> gaps;
    };
    struct SnapRequest {
        std::uint64_t generation=0;
        std::shared_ptr<const RecordingSource> source;
        std::shared_ptr<const RecordingOverview> index;
        RecordingSnapRequest query;
    };
    struct SnapResult {
        std::uint64_t generation=0;
        std::shared_ptr<const RecordingSource> source;
        RecordingSnapResult point;
        std::optional<RecordingError> error;
    };
    OverviewWorker()=default;
    ~OverviewWorker();
    // build.progress is replaced by the thread-safe progress snapshot below.
    // index, when supplied, must have been built from this exact source object.
    void submit(Request request);
    void cancel();
    void shutdown(); // Terminal, idempotent cancel + join.
    std::optional<Result> take_result();
    // An independent replaceable slot, executed by the same I/O thread.
    // View navigation cancels only views; document cancellation cancels both.
    void submit_snap(SnapRequest request);
    void cancel_snap();
    std::optional<SnapResult> take_snap_result();
    Progress progress() const;
    void trim_cache(std::size_t bytes);
private:
    struct Job { Request request; std::shared_ptr<std::atomic<bool>> cancelled; };
    struct SnapJob { SnapRequest request; std::shared_ptr<std::atomic<bool>> cancelled; };
    void run();
    mutable std::mutex mutex_;
    std::condition_variable ready_;
    std::thread thread_;
    std::optional<Job> pending_;
    std::optional<Result> result_;
    std::shared_ptr<std::atomic<bool>> active_cancel_;
    std::optional<SnapJob> pending_snap_;
    std::optional<SnapResult> snap_result_;
    std::shared_ptr<std::atomic<bool>> snap_cancel_;
    Progress progress_;
    bool stopping_=false;
    struct CachedView {std::string key;Result result;std::size_t bytes=0;};
    std::vector<CachedView> cached_views_;
    std::size_t cached_bytes_=0;
    std::size_t cache_limit_=0;
};
} // namespace lvm
