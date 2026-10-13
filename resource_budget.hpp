#pragma once
#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>

namespace lvm {
inline std::uint64_t saturated_add(std::uint64_t a, std::uint64_t b) {
    return b > std::numeric_limits<std::uint64_t>::max()-a ? std::numeric_limits<std::uint64_t>::max() : a+b;
}
struct ResourcePolicy {
    bool automatic = true;
    unsigned manual_mib = 2048;
    static bool valid_manual(unsigned mib) { return mib >= 64 && mib <= 65536; }
};
struct ResourceSnapshot {
    std::uint64_t available_physical = 0, available_commit = 0, resident = 0;
};
inline std::uint64_t resource_headroom(const ResourceSnapshot& memory) {
    const auto free=std::min(memory.available_physical,memory.available_commit);
    // Keep 20% for the OS/other applications, at least 512 MiB when possible.
    // Under pressure keep half, rather than interpreting zero as unlimited.
    const auto reserve=std::max(free/5,std::min<std::uint64_t>(512ULL*1024*1024,free/2));
    return free-reserve;
}
inline std::uint64_t resource_limit(const ResourcePolicy& policy, const ResourceSnapshot& memory) {
    const auto safety=saturated_add(memory.resident,resource_headroom(memory));
    const auto requested = policy.automatic ? safety
        : static_cast<std::uint64_t>(ResourcePolicy::valid_manual(policy.manual_mib) ? policy.manual_mib : 2048)*1024*1024;
    return std::min(requested,safety);
}

// Admission ledger, not a process-memory meter. The caller supplies measured
// resident capacities. Reservations remain owned by the operation, even after
// cancellation, until its buffers have actually been destroyed.
class MemoryBudget : public std::enable_shared_from_this<MemoryBudget> {
public:
    class Reservation {
    public:
        ~Reservation() { std::lock_guard<std::mutex> lock(owner_->mutex_); owner_->reserved_ -= bytes_; }
        std::uint64_t bytes() const { std::lock_guard<std::mutex> lock(owner_->mutex_);return bytes_; }
        bool shrink_to(std::uint64_t bytes) {
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            if(bytes>bytes_)return false;
            owner_->reserved_-=bytes_-bytes;bytes_=bytes;return true;
        }
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
    private:
        friend class MemoryBudget;
        Reservation(std::shared_ptr<MemoryBudget> owner,std::uint64_t bytes) : owner_(std::move(owner)),bytes_(bytes) {}
        std::shared_ptr<MemoryBudget> owner_;
        std::uint64_t bytes_;
    };
    void configure(std::uint64_t limit,std::uint64_t resident,std::uint64_t free_headroom) {
        std::lock_guard<std::mutex> lock(mutex_);
        limit_=limit; resident_=resident; headroom_=free_headroom;
    }
    std::uint64_t available() const {
        std::lock_guard<std::mutex> lock(mutex_); return available_locked();
    }
    std::uint64_t reserved() const {
        std::lock_guard<std::mutex> lock(mutex_); return reserved_;
    }
    std::uint64_t limit() const {std::lock_guard<std::mutex> lock(mutex_);return limit_;}
    std::shared_ptr<Reservation> reserve(std::uint64_t bytes) {
        // Allocate outside the mutex: shared_ptr control-block allocation can
        // fail and destroy Reservation, whose destructor also acquires it.
        auto result=std::shared_ptr<Reservation>(new Reservation(shared_from_this(),0));
        std::lock_guard<std::mutex> lock(mutex_);
        if (!bytes || bytes>available_locked()) return {};
        result->bytes_=bytes;
        reserved_+=bytes; return result;
    }
private:
    std::uint64_t available_locked() const {
        const auto used=saturated_add(resident_,reserved_);
        return std::min(limit_>used ? limit_-used : 0,headroom_>reserved_ ? headroom_-reserved_ : 0);
    }
    mutable std::mutex mutex_;
    std::uint64_t limit_=0,resident_=0,headroom_=0,reserved_=0;
};
} // namespace lvm
