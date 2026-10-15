#include "gui_resources.hpp"
#include "gui_state.hpp"
#include "gui_settings.hpp"
#include "fft.hpp"
#include "frf_stream.hpp"
#include "recording_source.hpp"
#include "gui_frf.hpp"
#include "gui_overview.hpp"
#include "gui_documents.hpp"
#include "gui_loading.hpp"
#include "gui_recording_cache.hpp"
#include "processor_resources.hpp"
#include <limits>
#include <unordered_set>

namespace gui {
namespace {
std::shared_ptr<lvm::MemoryBudget> shared_memory_budget() {
    static auto budget=std::make_shared<lvm::MemoryBudget>();return budget;
}
void trim_processed_caches() {
    const auto trim=[](const auto& document) {
        if(document.processed_source)document.processed_source->trim_processing_cache();
        if(document.overview.source && document.overview.source!=document.processed_source)
            document.overview.source->trim_processing_cache();
    };
    trim(g);for(const auto& document:g.inactive_documents)trim(document);
}
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
    std::unordered_set<const lvm::RecordingOverview*> overviews;
    const auto add_overview=[&](const auto& index){if(index && overviews.insert(index.get()).second)
        total=lvm::saturated_add(total,index->resident_bytes());};
    add_overview(g.overview.index);
    for(const auto& d:g.inactive_documents)add_overview(d.overview.index);
    return total;
}
lvm::ResourceSnapshot resource_snapshot() {
    lvm::ResourceSnapshot result;
    result.shared_resident=shared_memory_budget()->shared_resident();
    result.resident=lvm::saturated_add(document_data_memory_bytes(),result.shared_resident);
    MEMORYSTATUSEX status{};status.dwLength=sizeof(status);
    if(GlobalMemoryStatusEx(&status)) {
        result.available_physical=status.ullAvailPhys;result.available_commit=status.ullAvailPageFile;
    } else {
        result.available_physical=result.available_commit=256ULL*1024*1024;
    }
    return result;
}
std::shared_ptr<lvm::MemoryBudget> refresh_load_memory_budget() {
    auto budget=shared_memory_budget();
    const auto memory=resource_snapshot();
    budget->configure(lvm::resource_limit(g.resource_policy,memory),memory.resident-memory.shared_resident,
                      lvm::resource_headroom(memory));
    return budget;
}
bool set_resource_policy(bool automatic,unsigned manual_mib) {
    if(!lvm::ResourcePolicy::valid_manual(manual_mib))return false;
    g.resource_policy={automatic,manual_mib};
    trim_processed_caches();
    auto budget=refresh_load_memory_budget();g_frf_worker.trim_cache(budget->limit()/3,256ULL*1024*1024);
    g_overview_worker.trim_cache(std::min<std::uint64_t>(128ULL*1024*1024,budget->limit()/8));
    trim_document_analysis_caches(static_cast<std::size_t>(budget->limit()/8));
    lvm::trim_fft_cached_plans(static_cast<std::size_t>(budget->limit()/8));
    if(refresh_load_memory_budget()->available()<32ULL*1024*1024)move_resident_recordings_to_disk();
    refresh_load_memory_budget();save_runtime_settings();return true;
}
bool set_disk_cache_policy(bool unlimited,unsigned manual_mib,bool persist) {
    if(manual_mib<64 || manual_mib>1048576)return false;
    const auto bytes=unlimited ? std::numeric_limits<std::uint64_t>::max()
        : static_cast<std::uint64_t>(manual_mib)*1024*1024;
    try { set_recording_disk_cache_limit(bytes); }
    catch(const lvm::RecordingFailure& e) {
        if(e.error!=lvm::RecordingError::ResourceLimit)throw;
        return false;
    }
    catch(const std::filesystem::filesystem_error&) { return false; }
    g.disk_cache_unlimited=unlimited;g.disk_cache_mib=manual_mib;
    if(persist)save_runtime_settings();
    return true;
}
lvm::AnalysisResources current_analysis_resources(std::uint64_t required) {
    lvm::AnalysisResources result;result.budget=refresh_load_memory_budget();
    g_frf_worker.trim_cache(result.budget->limit()/3,result.cache_disk_bytes);
    g_overview_worker.trim_cache(std::min<std::uint64_t>(128ULL*1024*1024,result.budget->limit()/8));
    trim_document_analysis_caches(static_cast<std::size_t>(result.budget->limit()/8));
    lvm::trim_fft_cached_plans(static_cast<std::size_t>(result.budget->limit()/8));
    if(refresh_load_memory_budget()->available()<std::max<std::uint64_t>(required,32ULL*1024*1024))
        trim_processed_caches();
    if(refresh_load_memory_budget()->available()<32ULL*1024*1024)move_resident_recordings_to_disk();
    if(required && refresh_load_memory_budget()->available()<required) {
        // Caches borrow the shared budget; a new operation has priority. Live
        // requests keep their own shared owners even after idle retention ends.
        g_frf_worker.clear_cache();g_overview_worker.trim_cache(0);
        trim_processed_caches();
        trim_document_analysis_caches(0);lvm::trim_fft_cached_plans(0);
        if(refresh_load_memory_budget()->available()<required) {
            g.spec={};g.spec_valid=false;if(!g.spec_pending)g.spec_attempted=false;
            g.frf.result={};if(!g.frf.pending)g.frf.attempted=false;
        }
    }
    result.budget=refresh_load_memory_budget();
    result.memory_bytes=static_cast<std::size_t>(result.budget->available());
    result.cached_plans_accounted=true;
    result.max_participants=lvm::processor_participants();
    result.cache_memory_bytes=result.budget->limit()/3;
    return result;
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
