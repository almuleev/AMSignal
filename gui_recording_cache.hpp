#pragma once
#include "disk_cache.hpp"
namespace gui {
// Lazy shared manager for loading and overview requests; no control/window calls.
std::shared_ptr<lvm::DiskCache> recording_disk_cache();
// Overview remains usable when the shared directory is unavailable/owned.
std::shared_ptr<lvm::DiskCache> recording_overview_cache();
// Must be set only while loading/overview workers are idle (also used by tests).
void configure_recording_disk_cache(std::filesystem::path directory,std::uint64_t bytes=2ULL*1024*1024*1024);
} // namespace gui
