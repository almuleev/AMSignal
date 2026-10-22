#pragma once
#include "resource_budget.hpp"
#include "analysis_resources.hpp"

namespace gui {
std::uint64_t document_data_memory_bytes();
lvm::ResourceSnapshot resource_snapshot();
std::shared_ptr<lvm::MemoryBudget> refresh_load_memory_budget();
void refresh_system_memory_budget(const std::shared_ptr<lvm::MemoryBudget>& budget,const lvm::ResourcePolicy& policy);
bool set_resource_policy(bool automatic,unsigned manual_mib);
bool set_disk_cache_policy(bool unlimited,unsigned manual_mib,bool persist=true);
lvm::AnalysisResources current_analysis_resources(std::uint64_t required=0);
std::shared_ptr<lvm::MemoryBudget::Reservation> reserve_analysis_input(
    const lvm::AnalysisResources& resources,std::size_t rows,std::size_t channels,bool frf);
} // namespace gui
