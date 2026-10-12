#pragma once
#include "disk_cache.hpp"
#include "recording_overview.hpp"

namespace lvm {
struct ScanIndexCacheOptions {
    std::size_t memory_bytes=16ULL*1024*1024;
    std::uint64_t max_disk_bytes=32ULL*1024*1024;
    std::shared_ptr<MemoryBudget> budget;
};
struct CachedScanIndex {
    std::shared_ptr<ScanIndex> index;
    bool from_cache=false;
    std::optional<RecordingError> cache_error; // Optional cache failure; valid scanning can still succeed.
};
// Saves all checkpoint parser state, labels and export comments. No samples.
// Cache unavailable/corrupt => bounded scan. SourceChanged/cancellation never fall back.
CachedScanIndex scan_index_cached(const std::filesystem::path& path,std::shared_ptr<DiskCache> cache,
                                 const ScanIndexCacheOptions& options={},const std::atomic<bool>* cancel=nullptr);
// Managed overview holds a lease until its file handle and all shared views are gone.
std::shared_ptr<const RecordingOverview> managed_recording_overview(
    std::shared_ptr<const RecordingSource> source,std::shared_ptr<DiskCache> cache,
    const OverviewOptions& options={},const std::atomic<bool>* cancel=nullptr);
} // namespace lvm
