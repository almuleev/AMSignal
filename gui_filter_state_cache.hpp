#pragma once
#include "disk_cache.hpp"
#include "filter_engine.hpp"
namespace gui {
struct FilterCheckpoint {
    double time=0;
    std::vector<std::size_t> channels;
    std::vector<FilterStream> filters;
};
// Optional 2 MiB artifacts share the existing disk LRU. Invalid cache entries
// are discarded; cancellation and source changes always propagate.
bool load_filter_checkpoints(const std::shared_ptr<lvm::DiskCache>& cache,
    const std::shared_ptr<const lvm::RecordingSource>& source,const std::string& identity,
    FilterSettings& settings,std::size_t maximum,const std::atomic<bool>* cancel,
    std::vector<FilterCheckpoint>& states);
void save_filter_checkpoints(const std::shared_ptr<lvm::DiskCache>& cache,
    const std::shared_ptr<const lvm::RecordingSource>& source,const std::string& identity,
    double sample_step,const std::vector<FilterCheckpoint>& states,const std::atomic<bool>* cancel);
}
