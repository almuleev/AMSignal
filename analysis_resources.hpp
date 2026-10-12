#pragma once
#include "resource_budget.hpp"
#include <cstddef>

namespace lvm {
inline constexpr std::size_t analysis_memory_budget=384ULL*1024*1024;
struct AnalysisResources {
    std::size_t memory_bytes=analysis_memory_budget;
    std::size_t max_participants=4;
    std::shared_ptr<MemoryBudget> budget;
};
} // namespace lvm
