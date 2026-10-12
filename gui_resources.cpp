#include "gui_resources.hpp"
#include "gui_state.hpp"
#include "gui_settings.hpp"
#include "fft.hpp"
#include "frf_stream.hpp"
#include "recording_source.hpp"
#include <limits>
#include <unordered_set>

namespace gui {
namespace {
template<class T> std::uint64_t bytes(const std::vector<T>& v) { return v.capacity()*sizeof(T); }
std::uint64_t arrays(const std::vector<std::vector<double>>& v) {
    auto total=bytes(v);
    for(const auto& a:v) total=lvm::saturated_add(total,bytes(a));
    return total;
}
std::uint64_t document_bytes(const DocumentState& d) {
    auto total=lvm::saturated_add(bytes(d.ds.time),bytes(d.ds.raw_time));
    for(auto value:{arrays(d.ds.channels),arrays(d.transformed_channel_cache),arrays(d.filtered_channel_cache),
                    bytes(d.spec.freqs),arrays(d.spec.amp),bytes(d.stitched_gap_right_indices),bytes(d.stitched_gap_display_right)})
        total=lvm::saturated_add(total,value);
    for(const auto& index:d.envelopes)total=lvm::saturated_add(total,index.memory_bytes());
    for(const auto& r:d.frf.result.responses) {
        for(auto value:{bytes(r.frequencies),bytes(r.transfer),bytes(r.valid),bytes(r.coherence),bytes(r.coherence_valid),
                        bytes(r.reference_amplitude),bytes(r.reference_amplitude_valid)})
            total=lvm::saturated_add(total,value);
    }
    return total;
}
}
std::uint64_t document_data_memory_bytes() {
    auto total=lvm::saturated_add(document_bytes(g),lvm::fft_cached_plan_bytes());
    for(const auto& d:g.inactive_documents)total=lvm::saturated_add(total,document_bytes(d));
    std::unordered_set<const lvm::ScanIndex*> seen;
    const auto add_index=[&](const auto& index) {
        if(index && seen.insert(index.get()).second && !index->memory_reservation)
            total=lvm::saturated_add(total,index->memory_bytes());
    };
    add_index(g.cached_scan_index);add_index(g.source_scan_index);
    for(const auto& d:g.inactive_documents)add_index(d.source_scan_index);
    return total;
}
lvm::ResourceSnapshot resource_snapshot() {
    lvm::ResourceSnapshot result;
    result.resident=document_data_memory_bytes();
    MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
    if(GlobalMemoryStatusEx(&status)) {
        result.available_physical=status.ullAvailPhys;result.available_commit=status.ullAvailPageFile;
    } else {
        result.available_physical=result.available_commit=1024ULL*1024*1024;
    }
    return result;
}
std::shared_ptr<lvm::MemoryBudget> refresh_load_memory_budget() {
    static auto budget=std::make_shared<lvm::MemoryBudget>();
    const auto memory=resource_snapshot();
    budget->configure(lvm::resource_limit(g.resource_policy,memory),memory.resident,
                      std::min(memory.available_physical,memory.available_commit)/2);
    return budget;
}
bool set_resource_policy(bool automatic,unsigned manual_mib) {
    if(!lvm::ResourcePolicy::valid_manual(manual_mib))return false;
    g.resource_policy={automatic,manual_mib};
    refresh_load_memory_budget();save_runtime_settings();return true;
}
lvm::AnalysisResources current_analysis_resources() {
    lvm::AnalysisResources result;result.budget=refresh_load_memory_budget();return result;
}
std::shared_ptr<lvm::MemoryBudget::Reservation> reserve_analysis_input(
    const lvm::AnalysisResources& resources,std::size_t rows,std::size_t channels,bool frf) {
    const long double bytes=1024.0L*1024+channels*256.0L+static_cast<long double>(rows)*(channels+1.0L)*8+
        (frf ? std::min<std::size_t>(rows,1048576)*16.0L : 0);
    const auto failure=[] {throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,
        "Analysis resources are insufficient. Close another recording or increase the data budget.");};
    if(bytes>std::numeric_limits<std::uint64_t>::max())failure();
    auto reservation=resources.budget->reserve(static_cast<std::uint64_t>(bytes));
    if(!reservation)failure();
    return reservation;
}
} // namespace gui
