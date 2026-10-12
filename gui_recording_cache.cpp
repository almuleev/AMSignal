#include "gui_recording_cache.hpp"
#include "gui_platform.hpp"
#include <shlobj.h>
#include <mutex>

namespace gui {
namespace {
std::mutex mutex;
std::shared_ptr<lvm::DiskCache> cache;
std::shared_ptr<lvm::DiskCache> temporary_cache;
std::filesystem::path override_directory;
std::uint64_t disk_limit=2ULL*1024*1024*1024;
}
std::shared_ptr<lvm::DiskCache> recording_disk_cache() {
    std::lock_guard<std::mutex> lock(mutex);
    // Once a fallback exists, both kinds share that one process quota.
    if(temporary_cache)return temporary_cache;
    if(!cache) {
        auto directory=override_directory;
        if(directory.empty()) {
            PWSTR local=nullptr;
            if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,KF_FLAG_DEFAULT,nullptr,&local)))
                throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,"Cannot locate the local recording cache.");
            try{directory=std::filesystem::path(local)/L"AMSignal"/L"Cache"/L"v1";}catch(...){CoTaskMemFree(local);throw;}
            CoTaskMemFree(local);
        }
        try{cache=std::make_shared<lvm::DiskCache>(directory,disk_limit);}
        catch(const std::filesystem::filesystem_error&){throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,"Cannot access the local recording cache.");}
    }
    return cache;
}
std::shared_ptr<lvm::DiskCache> recording_overview_cache() {
    try{return recording_disk_cache();}
    catch(const lvm::RecordingFailure& e){if(e.error!=lvm::RecordingError::ResourceLimit)throw;}
    std::lock_guard<std::mutex> lock(mutex);
    if(!temporary_cache)temporary_cache=lvm::DiskCache::temporary(disk_limit);
    return temporary_cache;
}
void configure_recording_disk_cache(std::filesystem::path directory,std::uint64_t bytes) {
    std::lock_guard<std::mutex> lock(mutex);cache.reset();temporary_cache.reset();override_directory=std::move(directory);disk_limit=bytes;
}
} // namespace gui
