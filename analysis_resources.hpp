#pragma once
#include "resource_budget.hpp"
#include <cstddef>

namespace lvm {
inline constexpr std::size_t analysis_memory_budget=384ULL*1024*1024;
struct AnalysisResources {
    std::size_t memory_bytes=analysis_memory_budget;
    std::size_t max_participants=4;
    std::shared_ptr<MemoryBudget> budget;
    // Standalone callers keep the historical conservative defaults. GUI sets
    // these from its shared admission ledger; no second fixed numerical cap.
    std::size_t cache_memory_bytes=64ULL*1024*1024;
    std::size_t cache_disk_bytes=256ULL*1024*1024;
    bool cached_plans_accounted=false;
};
inline long double spectrum_workspace_bytes(std::size_t n,std::size_t channels) {
    long double scratch=16.0L*n;
    if(n && (n&(n-1))) {
        std::size_t m=1;
        while(m<2.0L*n-1) {
            if(m>std::numeric_limits<std::size_t>::max()/2)return std::numeric_limits<long double>::infinity();
            m*=2;
        }
        scratch=48.0L*n+32.0L*m;
    }
    return 1024.0L*1024+channels*256.0L+n*(8.0L+sizeof(long double))+
        (n/2+1)*(static_cast<long double>(channels)+1)*8+scratch;
}
} // namespace lvm
