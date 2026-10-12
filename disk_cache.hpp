#pragma once
#include "recording_source.hpp"
#include <string>

namespace lvm {
enum class CacheKind { ScanIndex,Overview };
// Payload + maximum temporary output, across both kinds. Live leases are pinned.
// One process owns the directory until the last manager/lease has gone away.
class DiskCache {
    struct State;
    struct Entry;
public:
    class Lease {
    public:
        ~Lease();
        Lease(const Lease&)=delete;
        Lease& operator=(const Lease&)=delete;
        const std::filesystem::path& path() const;
        void commit(); // Closed, complete output only; converts quota to actual bytes.
        void discard(); // Invalid artifacts cannot be found again; pins still protect I/O.
    private:
        friend class DiskCache;
        Lease()=default;
        std::shared_ptr<State> state_;
        std::shared_ptr<Entry> entry_;
    };
    explicit DiskCache(const std::filesystem::path& directory,
                       std::uint64_t max_bytes=2ULL*1024*1024*1024,std::size_t max_entries=256);
    // Private bounded fallback. The last manager/lease removes only owned files.
    static std::shared_ptr<DiskCache> temporary(std::uint64_t max_bytes=2ULL*1024*1024*1024,std::size_t max_entries=256);
    std::shared_ptr<Lease> find(const std::string& key,CacheKind kind);
    std::shared_ptr<Lease> begin_write(const std::string& key,CacheKind kind,std::uint64_t maximum_bytes);
    void set_limit(std::uint64_t bytes); // Refuses a limit below live pinned bytes.
    std::uint64_t used_bytes() const; // Includes reserved maximum temporary outputs.
    std::size_t entry_count() const;
    const std::filesystem::path& directory() const;
private:
    std::shared_ptr<State> state_;
};
std::string recording_cache_key(const std::filesystem::path& source,RecordingStamp stamp,const std::string& variant);
} // namespace lvm
