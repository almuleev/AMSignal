#include "gui_overview.hpp"
#include "gui_resources.hpp"
#include "gui_state.hpp"
#include "gui_render.hpp"
#include "gui_time_axis.hpp"
#include <cmath>

namespace gui {
namespace {
struct PendingSnap {
    std::uint64_t generation=0,numerical=0,project=0;
    std::shared_ptr<const lvm::RecordingSource> source;
    std::shared_ptr<const lvm::RecordingSource> raw;
    double start=0,end=0,x0=0,x1=0,y0=0,y1=0;
    RECT plot{};
    std::vector<char> visible;
    lvm::ResourcePolicy policy;
    bool measure=false,marker=false;
    bool stitched=false;
    bool analysis_range=false;
    int line=0;
    std::function<void(const lvm::RecordingSnapResult&)> accept;
};
std::optional<PendingSnap> pending;
std::uint64_t next_snap=0;
bool matches(const PendingSnap& p) {
    return g.mode==AnalysisMode::Time && g.overview.render_enabled && g.vvalid &&
        (!g.annotations_locked || p.analysis_range) && g.snap_to_data &&
        g.recording_source==p.raw && g.overview.source==p.source && g.numerical_revision==p.numerical && g.project_revision==p.project &&
        g.win_start==p.start && g.win_end==p.end && g.vx0==p.x0 && g.vx1==p.x1 &&
        g.vy0==p.y0 && g.vy1==p.y1 && EqualRect(&g.vrect,&p.plot) && g.visible==p.visible &&
        g.resource_policy.automatic==p.policy.automatic && g.resource_policy.manual_mib==p.policy.manual_mib &&
        g.measure_mode==p.measure && g.pending_marker==p.marker && g.pending_line==p.line && g.stitch_time_gaps==p.stitched;
}
}
bool overview_analysis_range_snap_pending() {
    return pending && pending->analysis_range;
}
void cancel_overview_snap() {
    g_overview_worker.cancel_snap();pending.reset();
    g.overview.snap_pending=false;g.overview.snap_error.reset();g.overview_drag_target_valid=false;
}
bool request_overview_snap(double time,double value,std::function<void(const lvm::RecordingSnapResult&)> accept,bool analysis_range) {
    cancel_overview_snap();
    if(!accept || g.mode!=AnalysisMode::Time || !g.overview.render_enabled || !g.vvalid ||
       (g.annotations_locked && !analysis_range) || !g.snap_to_data ||
       !overview_view_matches_viewport() || !std::isfinite(time) || !std::isfinite(value))return false;
    try {
        lvm::OverviewWorker::SnapRequest request;request.source=g.overview.source;request.index=g.overview.index;
        request.generation=++next_snap;request.query.range=lvm::AnalysisRange::selected(g.win_start,g.win_end);
        request.query.stitched=g.stitch_time_gaps;
        request.query.target_time=stitched_time_from_raw(time);request.query.target_value=value;
        request.query.time_scale=(g.vrect.right-g.vrect.left)/(g.vx1-g.vx0);
        request.query.value_scale=(g.vrect.bottom-g.vrect.top)/(g.vy1-g.vy0);
        request.query.budget=refresh_load_memory_budget();
        for(std::size_t c=0;c<g.visible.size();++c)if(g.visible[c]) {
            const bool affine=request.source->processing_identity().empty() && g.channel_transform_kind[c]==TransformRuntimeKind::Affine;
            request.query.channels.push_back({c,affine,affine ? g.channel_transform_mul[c] : 1,
                                                         affine ? g.channel_transform_add[c] : 0});
        }
        PendingSnap p;p.generation=request.generation;p.source=request.source;p.raw=g.recording_source;
        p.numerical=g.numerical_revision;p.project=g.project_revision;p.start=g.win_start;p.end=g.win_end;
        p.x0=g.vx0;p.x1=g.vx1;p.y0=g.vy0;p.y1=g.vy1;p.plot=g.vrect;p.visible=g.visible;
        p.policy=g.resource_policy;p.accept=std::move(accept);
        p.measure=g.measure_mode;p.marker=g.pending_marker;p.line=g.pending_line;
        p.stitched=g.stitch_time_gaps;
        p.analysis_range=analysis_range;
        g_overview_worker.submit_snap(std::move(request));pending=std::move(p);
        g.overview.snap_pending=true;invalidate_plot();return true;
    }catch(const std::bad_alloc&){g.overview.snap_error=lvm::RecordingError::ResourceLimit;}
     catch(...){g.overview.snap_error=lvm::RecordingError::ReadFailure;}
    invalidate_plot();return false;
}
bool poll_overview_snap() {
    // Cancel stale I/O on the next UI timer, rather than reading a potentially
    // long interval merely to discard it after completion.
    if(pending && !matches(*pending)){cancel_overview_snap();invalidate_plot();return false;}
    auto result=g_overview_worker.take_snap_result();if(!result)return false;
    if(!pending || result->generation!=pending->generation || result->source!=pending->source)return false;
    auto p=std::move(*pending);pending.reset();g.overview.snap_pending=false;
    if(!matches(p)){g.overview.snap_error.reset();invalidate_plot();return false;}
    try {result->source->check_current();}
    catch(const lvm::RecordingFailure& e){result->error=e.error;result->point={};}
    g.overview.snap_error=result->error;
    if(result->error==lvm::RecordingError::SourceChanged) {
        g.overview.error=result->error;g.overview.view.reset();g.overview.index.reset();g.vvalid=false;
    }
    if(!result->error && (result->point.found || p.analysis_range))p.accept(result->point);
    invalidate_plot();return true;
}
} // namespace gui
