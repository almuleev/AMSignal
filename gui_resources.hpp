#pragma once
#include "resource_budget.hpp"
#include "analysis_resources.hpp"

namespace gui {
std::uint64_t document_data_memory_bytes();
lvm::ResourceSnapshot resource_snapshot();
std::shared_ptr<lvm::MemoryBudget> refresh_load_memory_budget();
bool set_resource_policy(bool automatic,unsigned manual_mib);
lvm::AnalysisResources current_analysis_resources();
std::shared_ptr<lvm::MemoryBudget::Reservation> reserve_analysis_input(
    const lvm::AnalysisResources& resources,std::size_t rows,std::size_t channels,bool frf);
} // namespace gui
