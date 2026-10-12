#include "disk_cache.hpp"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <mutex>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace lvm {
namespace {
[[noreturn]] void disk_fail(const char* text){throw RecordingFailure(RecordingError::ResourceLimit,text);}
std::string hex(std::uint64_t value){std::ostringstream s;s<<std::hex<<std::setfill('0')<<std::setw(16)<<value;return s.str();}
bool hex_key(const std::string& key){return key.size()==16 && std::all_of(key.begin(),key.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');});}
const char* extension(CacheKind kind){return kind==CacheKind::ScanIndex ? ".scan" : ".overview";}
bool owned_name(const std::string& name,std::string& key,std::string& ext) {
    if(name.size()<39 || name.compare(0,5,".ams-") || name[21]!='-')return false;
    key=name.substr(5,16);if(!hex_key(key) || !hex_key(name.substr(22,16)))return false;
    ext=name.substr(38);return ext==".scan" || ext==".overview" || ext==".part";
}
bool reparse(const std::filesystem::path& path) {
#ifdef _WIN32
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT);
#else
    return std::filesystem::is_symlink(std::filesystem::symlink_status(path));
#endif
}
}
struct DiskCache::Entry {
    std::filesystem::path path;
    std::string key;
    CacheKind kind=CacheKind::ScanIndex;
    std::uint64_t bytes=0;
    std::filesystem::file_time_type touched{};
    std::size_t pins=0;
    bool writing=false,invalid=false;
};
struct DiskCache::State {
    std::filesystem::path root;
    mutable std::mutex mutex;
    std::vector<std::shared_ptr<Entry>> entries;
    std::uint64_t limit=0,used=0,serial=0;
    std::size_t maximum_entries=0;
    bool temporary=false;
#ifdef _WIN32
    HANDLE owner=INVALID_HANDLE_VALUE;
#else
    int owner=-1;
#endif
    ~State() {
#ifdef _WIN32
        if(owner!=INVALID_HANDLE_VALUE)CloseHandle(owner);
#else
        if(owner>=0)close(owner);
#endif
        if(!temporary)return;
        try {
            if(reparse(root))return;
            std::error_code error;
            for(const auto& entry:entries)if(!reparse(entry->path) && std::filesystem::is_regular_file(entry->path,error))
                std::filesystem::remove(entry->path,error);
            const auto lock=root/".ams-owner";
            if(!reparse(lock) && std::filesystem::is_regular_file(lock,error))std::filesystem::remove(lock,error);
            // Non-recursive: any foreign file/link keeps the directory intact.
            std::filesystem::remove(root,error);
        }catch(...){}
    }
    bool erase(const std::shared_ptr<Entry>& entry) {
        if(entry->pins)return false;
        std::error_code error;std::filesystem::remove(entry->path,error);
        if(error)return false;
        used-=entry->bytes;entries.erase(std::find(entries.begin(),entries.end(),entry));return true;
    }
    void trim(std::uint64_t additional,std::size_t slots=0) {
        if(additional>limit || slots>maximum_entries)disk_fail("Cache output exceeds the disk allowance.");
        while(used>limit-additional || entries.size()>maximum_entries-slots) {
            std::shared_ptr<Entry> oldest;
            for(const auto& e:entries)if(!e->pins && (!oldest || e->invalid || (!oldest->invalid && e->touched<oldest->touched)))oldest=e;
            if(!oldest || !erase(oldest))disk_fail("Pinned or inaccessible caches leave insufficient disk allowance.");
        }
    }
};
DiskCache::Lease::~Lease() {
    if(!state_)return;
    std::lock_guard<std::mutex> lock(state_->mutex);--entry_->pins;
    if(!entry_->pins && (entry_->writing || entry_->invalid)){entry_->invalid=true;state_->erase(entry_);}
}
const std::filesystem::path& DiskCache::Lease::path() const{return entry_->path;}
void DiskCache::Lease::discard(){std::lock_guard<std::mutex> lock(state_->mutex);entry_->invalid=true;}
void DiskCache::Lease::commit() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if(!entry_->writing || entry_->invalid)disk_fail("Cache output cannot be committed.");
    if(reparse(entry_->path) || !std::filesystem::is_regular_file(entry_->path))disk_fail("Invalid cache output.");
    const auto size=std::filesystem::file_size(entry_->path);
    if(!size || size>entry_->bytes)disk_fail("Cache output exceeded its reserved disk quota.");
    auto final=entry_->path;final.replace_extension(extension(entry_->kind));
    if(std::filesystem::exists(final))disk_fail("Cache publication collision.");
    std::filesystem::rename(entry_->path,final);
    state_->used-=entry_->bytes-size;entry_->bytes=size;entry_->path=std::move(final);entry_->writing=false;
    entry_->touched=std::filesystem::file_time_type::clock::now();
    std::error_code ignored;std::filesystem::last_write_time(entry_->path,entry_->touched,ignored);
}
DiskCache::DiskCache(const std::filesystem::path& directory,std::uint64_t max_bytes,std::size_t max_entries):state_(std::make_shared<State>()) {
    if(directory.empty() || !max_bytes || !max_entries || max_entries>4096)disk_fail("Invalid managed cache allowance.");
    auto& s=*state_;s.root=std::filesystem::absolute(directory).lexically_normal();
    if(reparse(s.root))disk_fail("Managed cache directory must not be a link.");
    std::filesystem::create_directories(s.root);
    s.limit=max_bytes;s.maximum_entries=max_entries;s.entries.reserve(max_entries+1);
    const auto owner=s.root/".ams-owner";
    if(reparse(owner))disk_fail("Managed cache lock must not be a link.");
#ifdef _WIN32
    s.owner=CreateFileW(owner.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_HIDDEN,nullptr);
    if(s.owner==INVALID_HANDLE_VALUE)disk_fail("The cache directory is already owned or cannot be locked.");
#else
    s.owner=::open(owner.c_str(),O_RDWR|O_CREAT|O_NOFOLLOW,0600);
    if(s.owner<0 || flock(s.owner,LOCK_EX|LOCK_NB)!=0)disk_fail("The cache directory is already owned or cannot be locked.");
#endif
    for(const auto& file:std::filesystem::directory_iterator(s.root)) {
        if(reparse(file.path()))continue;
        const auto name=file.path().filename().string();
        const auto suffix=name.size()>13 ? name.substr(13) : std::string{};
        const auto dash=suffix.find('-');
        const auto digits=[](const std::string& text){return !text.empty() && std::all_of(text.begin(),text.end(),[](char c){return c>='0' && c<='9';});};
        if(file.is_directory() && name.compare(0,13,"ams-overview-")==0 && dash!=std::string::npos &&
           digits(suffix.substr(0,dash)) && digits(suffix.substr(dash+1))) {
            // Only our abandoned builder directories, while the process lock
            // proves no active manager can own them. Never traverse link roots.
            if(file.path().parent_path()!=s.root)disk_fail("Invalid scratch path.");
            std::error_code error;std::filesystem::remove_all(file.path(),error);
            if(error)disk_fail("Cannot remove abandoned overview scratch.");
            continue;
        }
        if(!file.is_regular_file())continue;
        std::string key,ext;if(!owned_name(file.path().filename().string(),key,ext))continue;
        if(ext==".part") {std::error_code error;std::filesystem::remove(file.path(),error);if(error)disk_fail("Cannot remove abandoned cache output.");continue;}
        auto e=std::make_shared<Entry>();e->path=file.path();e->key=std::move(key);
        e->kind=ext==".scan" ? CacheKind::ScanIndex : CacheKind::Overview;
        e->bytes=file.file_size();e->touched=file.last_write_time();
        if(e->bytes>std::numeric_limits<std::uint64_t>::max()-s.used)disk_fail("Cache size overflow.");
        s.used+=e->bytes;s.entries.push_back(std::move(e));s.trim(0);
    }
}
std::shared_ptr<DiskCache> DiskCache::temporary(std::uint64_t max_bytes,std::size_t max_entries) {
    static std::atomic<std::uint64_t> serial{0};
    const auto parent=std::filesystem::temp_directory_path();
    for(unsigned attempt=0;attempt<100;++attempt) {
        const auto directory=parent/("AMSignal-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
        if(!std::filesystem::create_directory(directory))continue;
        try {
            auto result=std::make_shared<DiskCache>(directory,max_bytes,max_entries);
            result->state_->temporary=true;return result;
        }catch(...) {
            std::error_code error;const auto lock=directory/".ams-owner";
            if(!reparse(lock))std::filesystem::remove(lock,error);
            std::filesystem::remove(directory,error);throw;
        }
    }
    disk_fail("Cannot create a private temporary recording cache.");
}
std::shared_ptr<DiskCache::Lease> DiskCache::find(const std::string& key,CacheKind kind) {
    if(!hex_key(key))throw RecordingFailure(RecordingError::InvalidRequest,"Invalid cache key.");
    auto lease=std::shared_ptr<Lease>(new Lease);
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);std::shared_ptr<Entry> latest;
    for(const auto& e:s.entries)if(e->key==key && e->kind==kind && !e->writing && !e->invalid && (!latest || latest->touched<e->touched))latest=e;
    if(!latest)return {};
    if(reparse(latest->path) || !std::filesystem::is_regular_file(latest->path) || std::filesystem::file_size(latest->path)!=latest->bytes) {
        latest->invalid=true;s.erase(latest);return {};
    }
    lease->state_=state_;lease->entry_=latest;++latest->pins;
    latest->touched=std::filesystem::file_time_type::clock::now();
    std::error_code ignored;std::filesystem::last_write_time(latest->path,latest->touched,ignored);return lease;
}
std::shared_ptr<DiskCache::Lease> DiskCache::begin_write(const std::string& key,CacheKind kind,std::uint64_t maximum_bytes) {
    if(!hex_key(key))throw RecordingFailure(RecordingError::InvalidRequest,"Invalid cache key.");
    auto lease=std::shared_ptr<Lease>(new Lease);
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);
    if(!maximum_bytes)disk_fail("A cache output needs a positive quota.");
    s.trim(maximum_bytes,1);
    if(std::filesystem::space(s.root).available<maximum_bytes)disk_fail("Insufficient free disk space for cache output.");
    auto e=std::make_shared<Entry>();e->key=key;e->kind=kind;e->bytes=maximum_bytes;e->writing=true;
    for(unsigned attempt=0;attempt<100;++attempt) {
        const auto id=static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count())+(++s.serial);
        e->path=s.root/(".ams-"+key+"-"+hex(id)+".part");
        auto final=e->path;final.replace_extension(extension(kind));
        if(!std::filesystem::exists(e->path) && !std::filesystem::exists(final))break;
        if(attempt==99)disk_fail("Cannot allocate a new cache output path.");
    }
    s.entries.push_back(e);s.used+=maximum_bytes;e->pins=1;lease->state_=state_;lease->entry_=e;return lease;
}
void DiskCache::set_limit(std::uint64_t bytes) {
    auto& s=*state_;std::lock_guard<std::mutex> lock(s.mutex);if(!bytes)disk_fail("Invalid disk cache limit.");
    std::uint64_t pinned=0;for(const auto& e:s.entries)if(e->pins)pinned+=e->bytes;
    if(pinned>bytes)disk_fail("Live caches exceed the new disk allowance.");
    const auto previous=s.limit;s.limit=bytes;try{s.trim(0);}catch(...){s.limit=previous;throw;}
}
std::uint64_t DiskCache::used_bytes() const{std::lock_guard<std::mutex> lock(state_->mutex);return state_->used;}
std::size_t DiskCache::entry_count() const{std::lock_guard<std::mutex> lock(state_->mutex);return state_->entries.size();}
const std::filesystem::path& DiskCache::directory() const{return state_->root;}
std::string recording_cache_key(const std::filesystem::path& source,RecordingStamp stamp,const std::string& variant) {
    std::uint64_t value=14695981039346656037ULL;
    const auto add=[&](const std::string& text){for(unsigned char c:text){value^=c;value*=1099511628211ULL;}value^=0xff;value*=1099511628211ULL;};
    add(std::filesystem::absolute(source).lexically_normal().u8string());add(std::to_string(stamp.bytes));
    add(std::to_string(stamp.modified.time_since_epoch().count()));add(std::to_string(std::filesystem::file_time_type::duration::period::num));
    add(std::to_string(std::filesystem::file_time_type::duration::period::den));add(variant);return hex(value);
}
} // namespace lvm
