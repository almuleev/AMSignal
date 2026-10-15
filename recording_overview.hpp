#pragma once
#include "recording_source.hpp"
#include "resource_budget.hpp"
#include <array>

namespace lvm {
// Display-only data. Never pass these aggregates to a numerical analysis.
struct OverviewPoint {
    std::uint64_t sample = 0;
    double time = 0, value = 0;
};
struct OverviewTrace {
    OverviewPoint first, last, minimum, maximum;
    std::uint64_t finite_count = 0;
    // Acquisition order, with duplicate extrema/endpoints removed.
    std::pair<std::array<OverviewPoint,4>,std::size_t> ordered_points() const;
};
struct OverviewBucket {
    std::uint64_t first_sample = 0, sample_count = 0;
    double first_time = 0, last_time = 0, maximum_spacing = 0;
    std::vector<OverviewTrace> channels;
    bool aggregated() const { return sample_count > 1; }
    // Mixed finite/missing data or a timing gap must not become a joined curve.
    bool discontinuous(std::size_t channel,double gap_threshold) const {
        return channels.at(channel).finite_count != sample_count || maximum_spacing > gap_threshold;
    }
};
struct OverviewOptions {
    std::size_t base_rows = 1024;
    std::size_t memory_bytes = 16ULL*1024*1024;
    std::uint64_t max_disk_bytes = 512ULL*1024*1024;
    std::shared_ptr<MemoryBudget> budget;
    // Trusted full-timeline values from the opening pass; never the GUI filter Fs.
    // Zero sample step / negative gap step request the original exact inspection.
    double prepared_sample_step=0,prepared_gap_step=-1;
    // Optional resident hierarchy, accounted separately from temporary buffers.
    // Zero keeps the persisted disk path; timestamps remain owned by the source.
    std::size_t resident_index_bytes=0,max_participants=1;
    // Runs on the building thread; counts do not imply that the cache is published.
    std::function<void(std::uint64_t,std::uint64_t)> progress;
};
struct OverviewRequest {
    AnalysisRange range;
    std::vector<std::size_t> channels;
    std::size_t pixel_columns = 1024;
    std::size_t memory_bytes = 8ULL*1024*1024;
    std::shared_ptr<MemoryBudget> budget;
    // Display-only packing within one pixel width, preserving exact extrema.
    bool display_density = false;
};
struct OverviewView {
    OverviewView()=default;
    OverviewView(OverviewView&&)=default;
    OverviewView(const OverviewView&)=delete;
    OverviewView& operator=(const OverviewView&)=delete;
    OverviewView& operator=(OverviewView&& other) noexcept;
    std::shared_ptr<MemoryBudget::Reservation> memory_reservation;
    TimeRange range;
    std::vector<OverviewBucket> buckets;
    std::uint64_t sample_count = 0;
    std::size_t pixel_columns = 0; // Effective display density, not an FFT length.
    std::size_t resident_bytes() const;
};
struct OverviewGap { double left = 0, right = 0; };
// Snap uses the hierarchy only for conservative rejection. The selected value
// is always reread from RecordingSource, never taken from a min/max envelope.
struct RecordingSnapChannel {
    std::size_t channel=0;
    bool affine=false;
    double multiplier=1,offset=0;
};
struct RecordingSnapRequest {
    AnalysisRange range;
    std::vector<RecordingSnapChannel> channels;
    double target_time=0,target_value=0;
    // Screen metric: pixels per time/amplitude unit. Positive finite scales.
    double time_scale=1,value_scale=1;
    std::size_t memory_bytes=2ULL*1024*1024;
    std::shared_ptr<MemoryBudget> budget;
    bool stitched=false;
};
struct RecordingSnapResult {
    double time=0,value=0;
    std::size_t channel=0;
    std::uint64_t sample=0,rows_read=0,leaves_read=0;
    bool found=false;
};

// Immutable disk or admitted resident hierarchy. Reads own temporary buffers.
// Cache format validates source identity, bounds, channel mapping and checksums.
class RecordingOverview {
public:
    // Returns null when unsupported or admission is denied; other failures propagate.
    static std::shared_ptr<const RecordingOverview> try_build_resident(
        std::shared_ptr<const RecordingSource> source,const OverviewOptions& options,
        const std::atomic<bool>* cancel=nullptr);
    static std::shared_ptr<const RecordingOverview> build(
        std::shared_ptr<const RecordingSource> source,const std::filesystem::path& cache,
        const OverviewOptions& options={},const std::atomic<bool>* cancel=nullptr);
    static std::shared_ptr<const RecordingOverview> open(
        std::shared_ptr<const RecordingSource> source,const std::filesystem::path& cache);
    static std::shared_ptr<const RecordingOverview> open_or_build(
        std::shared_ptr<const RecordingSource> source,const std::filesystem::path& cache,
        const OverviewOptions& options={},const std::atomic<bool>* cancel=nullptr);
    OverviewView read_view(const OverviewRequest& request,const std::atomic<bool>* cancel=nullptr) const;
    RecordingSnapResult nearest_sample(const RecordingSnapRequest& request,
                                      const std::atomic<bool>* cancel=nullptr) const;
    // Streams every original interval over the threshold, including tree/block boundaries.
    // Threshold is supplied by the caller's existing gap rule, not estimated from envelopes.
    std::uint64_t visit_gaps(TimeRange range,double threshold,
        const std::function<void(OverviewGap)>& consume,const std::atomic<bool>* cancel=nullptr,
        const OverviewOptions& resources={}) const;
    std::uint64_t sample_count() const { return sample_count_; }
    std::size_t level_count() const { return levels_.size(); }
    std::uint64_t disk_bytes() const { return disk_bytes_; }
    double gap_step() const { return gap_step_; }
    // Exact cached timestamps and reversible display-only gap compression.
    double stitched_time(double time,bool inverse=false) const;
    double stitched_sample_time(std::uint64_t sample) const;
    std::size_t resident_bytes() const;
    std::pair<std::uint64_t,std::uint64_t> sample_range(AnalysisRange range) const;
    void replay_timeline(AnalysisRange range,const std::function<void(const std::vector<double>&)>& consume,
                         const std::atomic<bool>* cancel=nullptr) const;
    const std::shared_ptr<const RecordingSource>& source() const { return source_; }
private:
    struct Level { std::uint64_t offset=0,count=0; };
    struct Storage;
    std::shared_ptr<const RecordingSource> source_;
    std::shared_ptr<Storage> storage_;
    std::vector<Level> levels_;
    std::uint64_t sample_count_=0,disk_bytes_=0;
    std::size_t base_rows_=0;
    double gap_step_=0,sample_step_=0;
    OverviewBucket read_bucket(std::size_t level,std::uint64_t index) const;
    std::vector<double> read_times(std::uint64_t leaf,double* display_first=nullptr,
                                  double* anchor_raw=nullptr,double* anchor_display=nullptr) const;
    void cache_stitched_leaf(std::uint64_t leaf) const;
};
} // namespace lvm
