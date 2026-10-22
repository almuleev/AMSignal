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
    std::uint64_t shared_resident = 0;
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
        ~Reservation() { std::lock_guard<std::mutex> lock(owner_->mutex_); owner_->reserved_ -= bytes_;
            release_resident(resident_bytes_); }
        std::uint64_t bytes() const { std::lock_guard<std::mutex> lock(owner_->mutex_);return bytes_; }
        bool shrink_to(std::uint64_t bytes) {
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            if(bytes>bytes_)return false;
            owner_->reserved_-=bytes_-bytes;
            const auto retained=std::min(resident_bytes_,bytes);
            release_resident(resident_bytes_-retained);resident_bytes_=retained;
            bytes_=bytes;return true;
        }
        // Reserve growth before allocating. Failure leaves the old owner intact.
        bool grow_to(std::uint64_t bytes) {
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            if(bytes<bytes_ || bytes-bytes_>owner_->available_locked())return false;
            owner_->reserved_+=bytes-bytes_;bytes_=bytes;return true;
        }
        // Completed buffers are already reflected in OS free memory. Preserve
        // the old admission until the next OS snapshot instead of spending it twice.
        void mark_resident() {
            std::lock_guard<std::mutex> lock(owner_->mutex_);
            const auto added=bytes_-resident_bytes_;
            owner_->shared_resident_+=added;resident_bytes_=bytes_;
            if(charged_snapshot_!=owner_->snapshot_) {charged_bytes_=0;charged_snapshot_=owner_->snapshot_;}
            const auto charge=std::min(owner_->headroom_,added);
            charged_bytes_+=charge;owner_->headroom_-=charge;
        }
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
    private:
        friend class MemoryBudget;
        // Return only the debit made since the current snapshot. After an OS
        // refresh, released resident memory becomes available on the next refresh.
        void release_resident(std::uint64_t bytes) {
            owner_->shared_resident_-=bytes;
            if(charged_snapshot_==owner_->snapshot_) {
                const auto restored=std::min(charged_bytes_,bytes);
                owner_->headroom_=saturated_add(owner_->headroom_,restored);charged_bytes_-=restored;
            }
        }
        Reservation(std::shared_ptr<MemoryBudget> owner,std::uint64_t bytes) : owner_(std::move(owner)),bytes_(bytes) {}
        std::shared_ptr<MemoryBudget> owner_;
        std::uint64_t bytes_;
        std::uint64_t resident_bytes_=0;
        std::uint64_t charged_bytes_=0,charged_snapshot_=0;
    };
    void configure(std::uint64_t limit,std::uint64_t resident,std::uint64_t free_headroom) {
        std::lock_guard<std::mutex> lock(mutex_);
        limit_=limit; resident_=resident; headroom_=free_headroom;++snapshot_;
    }
    // Worker-safe OS refresh: document capacities stay with the GUI snapshot;
    // retained reservations track buffers allocated since that snapshot.
    void refresh_headroom(const ResourcePolicy& policy,std::uint64_t physical,std::uint64_t commit) {
        std::lock_guard<std::mutex> lock(mutex_);
        const ResourceSnapshot memory{physical,commit,saturated_add(resident_,shared_resident_)};
        headroom_=resource_headroom(memory);limit_=resource_limit(policy,memory);++snapshot_;
    }
    std::uint64_t available() const {
        std::lock_guard<std::mutex> lock(mutex_); return available_locked();
    }
    std::uint64_t reserved() const {
        std::lock_guard<std::mutex> lock(mutex_); return reserved_;
    }
    std::uint64_t shared_resident() const {
        std::lock_guard<std::mutex> lock(mutex_);return shared_resident_;
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
    // Transfer already-counted resident arrays to a shared asynchronous owner.
    // This changes ownership accounting, without admitting another allocation.
    std::shared_ptr<Reservation> transfer_resident(std::uint64_t bytes) {
        auto result=std::shared_ptr<Reservation>(new Reservation(shared_from_this(),0));
        std::lock_guard<std::mutex> lock(mutex_);
        if(!bytes || bytes>resident_)return {};
        resident_-=bytes;reserved_+=bytes;shared_resident_+=bytes;
        result->bytes_=bytes;result->resident_bytes_=bytes;return result;
    }
private:
    std::uint64_t available_locked() const {
        const auto used=saturated_add(resident_,reserved_);
        // Transferred arrays are already allocated and reflected in OS free
        // memory. Only prospective working reservations reduce that headroom.
        const auto prospective=reserved_-shared_resident_;
        return std::min(limit_>used ? limit_-used : 0,headroom_>prospective ? headroom_-prospective : 0);
    }
    mutable std::mutex mutex_;
    std::uint64_t limit_=0,resident_=0,headroom_=0,reserved_=0,shared_resident_=0;
    std::uint64_t snapshot_=0;
};
} // namespace lvm
