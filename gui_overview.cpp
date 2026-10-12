#include "gui_overview.hpp"
#include "gui_state.hpp"
#include "gui_resources.hpp"
#include "gui_render.hpp"
#include "gui_processing.hpp"
#include "gui_recording_cache.hpp"
#include "gui_recording_processing.hpp"
#include <cmath>
#include <algorithm>
#include <limits>

namespace gui {
lvm::OverviewWorker g_overview_worker;
namespace {
std::uint64_t next_generation=0;
bool requires_processed() {
    ensure_channel_formula_vectors();
    return g.noise_threshold_enabled || std::find(g.channel_transform_kind.begin(),g.channel_transform_kind.end(),TransformRuntimeKind::CachedFormula)!=g.channel_transform_kind.end();
}
}
bool request_recording_overview(const std::filesystem::path& cache,lvm::AnalysisRange range,
                               std::vector<std::size_t> channels,std::size_t pixels,bool display_density) {
    cancel_recording_overview();
    g.overview.generation=++next_generation;
    try {
        lvm::OverviewWorker::Request request;
        ensure_channel_formula_vectors();
        bool processed=g.noise_threshold_enabled;
        for(auto kind:g.channel_transform_kind)processed=processed || kind==TransformRuntimeKind::CachedFormula;
        request.source=processed ? processed_recording_source() : g.recording_source;
        request.index=g.overview.index;
        if(request.index && request.index->source()!=request.source)request.index.reset();
        g.overview.source=request.source;g.overview.numerical=g.numerical_revision;
        request.generation=g.overview.generation;request.cache=cache;
        if(cache.empty() || processed){request.cache.clear();request.managed_cache=recording_overview_cache();}
        auto budget=refresh_load_memory_budget();
        request.build.budget=budget;request.view.budget=std::move(budget);
        request.view.range=range;request.view.channels=std::move(channels);request.view.pixel_columns=pixels;
        request.view.display_density=display_density;
        request.gap_details=g.recording_rows>0;
        request.gap_step=g.cached_global_gap_step_ready ? g.cached_global_gap_step : -1;
        lvm::TimeRange resolved;
        if(request.source)range.resolve(request.source->bounds(),resolved);
        g.overview.cache_path=cache;g.overview.requested_range=resolved;
        g.overview.requested_channels=request.view.channels;g.overview.requested_pixels=pixels;
        g.overview.requested_display_density=display_density;
        g.overview.requested_resource_policy=g.resource_policy;
        g_overview_worker.submit(std::move(request));g.overview.pending=true;return true;
    } catch(const lvm::RecordingFailure& e) {g.overview.error=e.error;}
      catch(const std::bad_alloc&) {g.overview.error=lvm::RecordingError::ResourceLimit;}
      catch(...) {g.overview.error=lvm::RecordingError::ReadFailure;}
    return false;
}
void cancel_recording_overview() {
    cancel_overview_snap();
    g_overview_worker.cancel();g.overview.pending=false;
    g.overview.generation=++next_generation;g.overview.error.reset();
}
bool poll_recording_overview() {
    auto result=g_overview_worker.take_result();
    if(!result || result->generation!=g.overview.generation || result->source!=g.overview.source ||
       (!result->source->processing_identity().empty() && g.overview.numerical!=g.numerical_revision))return false;
    if(!result->error) {
        try {result->source->check_current();}
        catch(const lvm::RecordingFailure& e) {result->error=e.error;}
        catch(...) {result->error=lvm::RecordingError::ReadFailure;}
    }
    g.overview.pending=false;g.overview.error=result->error;
    if(!result->error) {
        g.overview.index=std::move(result->index);g.overview.view=std::move(result->view);
        g.overview.channels=std::move(result->channels);
        g.overview.gaps=std::move(result->gaps);
        g.overview.details_reservation=std::move(result->details_reservation);
        if(result->gap_step>=0) {g.cached_global_gap_step=result->gap_step;g.cached_global_gap_step_ready=true;}
    } else if(result->error==lvm::RecordingError::SourceChanged) {
        g.overview.index.reset();g.overview.view.reset();
        g.overview.channels.clear();
        g.overview.gaps.clear();g.overview.details_reservation.reset();
    }
    if(g.main)invalidate_plot();
    return true;
}
bool overview_processing_supported() {
    if(g.stitch_time_gaps)return false;
    ensure_channel_formula_vectors();
    return true;
}
bool synchronize_overview_viewport(std::size_t pixels) {
    if(!g.overview.render_enabled || !overview_processing_supported() || !g.recording_source ||
       !g.recording_source->file_backed() || g.win_end<=g.win_start || !pixels)return false;
    try{g.recording_source->check_current();}
    catch(const lvm::RecordingFailure& e) {
        cancel_recording_overview();g.overview.view.reset();g.overview.index.reset();
        g.overview.gaps.clear();g.overview.details_reservation.reset();g.overview.error=e.error;return false;
    }
    pixels=std::min<std::size_t>(pixels,8192);
    std::vector<std::size_t> channels;
    for(std::size_t c=0;c<g.visible.size();++c)if(g.visible[c])channels.push_back(c);
    if(g.overview.numerical==g.numerical_revision && g.overview.requested_pixels==pixels && g.overview.requested_display_density &&
       g.overview.requested_resource_policy.automatic==g.resource_policy.automatic &&
       g.overview.requested_resource_policy.manual_mib==g.resource_policy.manual_mib &&
       g.overview.requested_range.start==g.win_start && g.overview.requested_range.end==g.win_end &&
       g.overview.requested_channels==channels &&
       (g.overview.pending || overview_view_matches_viewport() || g.overview.error))return false;
    // Complete a first index build even while its desired viewport changes.
    if(g.overview.numerical==g.numerical_revision && g.overview.pending &&
       (!g.overview.index || g.overview.index->source()!=g.overview.source) &&
       g_overview_worker.progress().phase==lvm::OverviewWorker::Phase::Building)return false;
    return request_recording_overview(g.overview.cache_path,lvm::AnalysisRange::selected(g.win_start,g.win_end),
                                      std::move(channels),pixels,true);
}
bool overview_view_matches_viewport() {
    if(!g.overview.view || !g.overview.index || g.overview.index->source()!=g.overview.source ||
       (g.overview.source->processing_identity().empty() ? g.overview.source!=g.recording_source : g.processed_source_origin!=g.recording_source) ||
       requires_processed()!=!g.overview.source->processing_identity().empty() ||
       (!g.overview.source->processing_identity().empty() && g.overview.numerical!=g.numerical_revision) ||
       g.overview.view->range.start!=g.win_start || g.overview.view->range.end!=g.win_end)return false;
    // A hidden or newly shown channel changes the projection, not the numerical input.
    for(std::size_t c=0;c<g.visible.size();++c)if(g.visible[c] &&
       std::find(g.overview.channels.begin(),g.overview.channels.end(),c)==g.overview.channels.end())return false;
    return true;
}
bool overview_time_yrange(double& minimum,double& maximum) {
    minimum=-1;maximum=1;
    if(!overview_processing_supported() || !overview_view_matches_viewport())return false;
    double low=std::numeric_limits<double>::infinity(),high=-low;
    for(const auto& bucket:g.overview.view->buckets)for(std::size_t k=0;k<bucket.channels.size();++k) {
        const auto c=g.overview.channels.at(k);if(c>=g.visible.size() || !g.visible[c])continue;
        const auto& trace=bucket.channels[k];if(!trace.finite_count)continue;
        const bool affine=g.overview.source->processing_identity().empty() && g.channel_transform_kind[c]==TransformRuntimeKind::Affine;
        const double mul=affine ? g.channel_transform_mul[c] : 1,add=affine ? g.channel_transform_add[c] : 0;
        const double a=affine ? trace.minimum.value*mul+add : trace.minimum.value;
        const double b=affine ? trace.maximum.value*mul+add : trace.maximum.value;
        if(!std::isfinite(a) || !std::isfinite(b))return false;
        low=std::min({low,a,b});high=std::max({high,a,b});
    }
    if(low>high)return true;
    if(high-low<1e-12){low-=1;high+=1;}
    const double pad=(high-low)*.05;
    if(!std::isfinite(pad) || !std::isfinite(low-pad) || !std::isfinite(high+pad))return false;
    minimum=low-pad;maximum=high+pad;return true;
}
} // namespace gui
