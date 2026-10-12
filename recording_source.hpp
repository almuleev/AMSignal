#pragma once

#include "analysis_range.hpp"
#include "lvm_parser.hpp"
#include <optional>
#include <stdexcept>

namespace lvm {

enum class RecordingError { InvalidRequest, ReadFailure, SourceChanged, Cancelled, ResourceLimit };

class RecordingFailure : public std::runtime_error {
public:
    RecordingError error;
    RecordingFailure(RecordingError error, const std::string& message)
        : std::runtime_error(message), error(error) {}
};

struct RecordingStamp {
    std::uintmax_t bytes = 0;
    std::filesystem::file_time_type modified{};
};

struct RecordingReadRequest {
    AnalysisRange range;
    // Logical channel indices, in the requested order. Empty = timeline only.
    std::vector<std::size_t> channels;
    std::size_t block_rows = 65536;
    // Sample storage, including conservative vector growth. Not a process limit.
    std::size_t block_bytes = 16ULL * 1024 * 1024;
};

using RecordingBlockConsumer = std::function<void(const std::vector<double>&,
                                                 const std::vector<std::vector<double>>&)>;

// Immutable descriptor. Each replay owns its reader and buffers, never GUI state.
class RecordingSource {
public:
    static std::shared_ptr<const RecordingSource> from_file(
        const std::filesystem::path& path, TimeRange bounds,
        std::vector<std::size_t> physical_columns,
        std::shared_ptr<const ScanIndex> index = {},
        std::optional<RecordingStamp> expected = {});
    // Takes ownership without copying sample arrays. Input must already be prepared.
    static std::shared_ptr<const RecordingSource> from_memory(Dataset&& data);
    using Replay = std::function<void(const RecordingReadRequest&,const RecordingBlockConsumer&,const std::atomic<bool>*)>;
    // Immutable processing view. The identity includes the processing recipe;
    // its replay owns causal state and must return only the requested interval.
    static std::shared_ptr<const RecordingSource> processed(std::shared_ptr<const RecordingSource> raw,
                                                          std::string identity,Replay replay);
    std::string cache_identity() const { return path_.lexically_normal().u8string()+processing_identity_; }
    const std::string& processing_identity() const { return processing_identity_; }

    const TimeRange& bounds() const { return bounds_; }
    std::size_t channel_count() const { return physical_columns_.size(); }
    std::optional<std::size_t> sample_count() const {
        return memory_ ? std::optional<std::size_t>(memory_->rows()) : std::nullopt;
    }
    const std::optional<RecordingStamp>& stamp() const { return stamp_; }
    bool file_backed() const { return !memory_; }
    const std::filesystem::path& path() const { return path_; }
    const std::vector<std::size_t>& physical_columns() const { return physical_columns_; }
    // Throws on stale/missing sources. Checks before/after reads and each block.
    void check_current() const;
    std::size_t replay(const RecordingReadRequest& request, const RecordingBlockConsumer& consume,
                       const std::atomic<bool>* cancel = nullptr) const;

private:
    TimeRange bounds_;
    std::filesystem::path path_;
    std::vector<std::size_t> physical_columns_;
    std::shared_ptr<const ScanIndex> index_;
    std::optional<RecordingStamp> stamp_;
    std::shared_ptr<const Dataset> memory_;
    std::shared_ptr<const RecordingSource> raw_;
    std::string processing_identity_;
    Replay replay_;
};

} // namespace lvm
