#include "gui_recording_processing.hpp"
#include "gui_analysis_source.hpp"
#include "gui_processing.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "frf_stream.hpp"
#include "gui_recording_cache.hpp"
#include "scan_index_cache.hpp"
#include "gui_resources.hpp"
#include "gui_filter_state_cache.hpp"
#include "gui_time_axis.hpp"
#include <sstream>
#include <iomanip>
#include <mutex>

namespace gui {
bool share_resident_recording() {
    if(g.current_file_partial || g.ds.frequency_axis || g.ds.rows()<3 || !g.recording_source ||
       !g.recording_source->file_backed() || g.recording_source->resident_backed())return false;
    try {
        const auto step=current_filter_sample_step();
        lvm::Dataset seed;seed.ok=g.ds.ok;seed.names=g.ds.names;seed.generated_names=g.ds.generated_names;
        seed.source_columns=g.ds.source_columns;seed.export_comments=g.ds.export_comments;seed.stats=g.ds.stats;
        seed.time.assign(g.ds.time.begin(),g.ds.time.begin()+2);
        if(g.ds.raw_time.size()>=2)seed.raw_time.assign(g.ds.raw_time.begin(),g.ds.raw_time.begin()+2);
        for(const auto& column:g.ds.channels)seed.channels.emplace_back(column.begin(),column.begin()+2);
        const auto rows=g.ds.rows();
        const auto source=lvm::RecordingSource::from_resident(g.ds,g.recording_source,refresh_load_memory_budget(),step);
        g.ds=std::move(seed);g.recording_source=source;g.recording_rows=rows;
        g.processed_source.reset();g.processed_source_origin.reset();
        for(auto& values:g.transformed_channel_cache)std::vector<double>().swap(values);
        std::fill(g.transformed_channel_cache_valid.begin(),g.transformed_channel_cache_valid.end(),0);
        for(auto& values:g.filtered_channel_cache)std::vector<double>().swap(values);
        std::fill(g.filtered_channel_cache_valid.begin(),g.filtered_channel_cache_valid.end(),0);
        g.envelopes.clear();g.envelope_serial.clear();g.time_yrange_cache.valid=false;
        invalidate_stitched_time_cache();g.overview.render_enabled=true;
        return true;
    }catch(const std::exception&){return false;}
}
std::shared_ptr<const lvm::RecordingSource> processed_recording_source(bool apply,double display_filter_step,
    std::shared_ptr<lvm::MemoryBudget> checkpoint_budget) {
    const auto raw=current_recording_source();
    if(raw->resident_backed() && !checkpoint_budget)checkpoint_budget=refresh_load_memory_budget();
    if(!apply)return raw;
    ensure_channel_formula_vectors();
    const auto kinds=g.channel_transform_kind;
    const auto mul=g.channel_transform_mul,add=g.channel_transform_add;
    const auto formulas=g.channel_formula_rpn;
    const auto global=g.global_formula_rpn;
    const auto identities=g.channel_formula_identity;
    const bool global_identity=g.global_formula_identity,global_affine=g.global_formula_affine;
    const double global_mul=g.global_formula_mul,global_add=g.global_formula_add;
    const bool filtering=g.noise_threshold_enabled;
    const double sample_step=std::isfinite(display_filter_step) && display_filter_step>0 ? display_filter_step : raw->filter_sample_step();
    const FilterSettings filter{g.noise_threshold_mode,g.noise_threshold_topology,g.noise_threshold_min,g.noise_threshold_max,0};
    bool changed=filtering;for(auto kind:kinds)changed=changed || kind!=TransformRuntimeKind::Identity;
    if(!changed)return raw;
    // Text is full precision and includes the recipe, not worker generations or styles.
    std::ostringstream key;key<<std::setprecision(17)<<"recipe2:"<<filtering<<':'<<filter.mode<<':'<<filter.topology<<':'
        <<filter.low_cutoff<<':'<<filter.high_cutoff<<':'<<sample_step<<':'<<to_utf8(g.global_formula);
    for(const auto& formula:g.channel_formulas) {const auto text=to_utf8(formula);key<<':'<<text.size()<<':'<<text;}
    if(checkpoint_budget)key<<":continuation";
    if(g.processed_source_origin==raw && g.processed_source &&
       g.processed_source->processing_identity()=="\nprocessed-v1:"+key.str())return g.processed_source;
    // FFT/export may have replaced the common recipe slot. The displayed
    // source still owns its continuation states; retain them across mode changes.
    if(checkpoint_budget && g.processed_source_origin==raw && g.overview.source &&
       g.overview.source->processing_identity()=="\nprocessed-v1:"+key.str()) {
        g.processed_source=g.overview.source;return g.processed_source;
    }
    using Checkpoint=FilterCheckpoint;
    struct Cadence {
        std::mutex mutex;double step=0;
        std::shared_ptr<lvm::MemoryBudget::Reservation> reservation;
        std::shared_ptr<const lvm::RecordingSource> indexed;
        std::vector<Checkpoint> checkpoints;
        bool loaded=false;
    };
    auto cadence=std::make_shared<Cadence>();
    cadence->step=sample_step;
    constexpr std::size_t checkpoint_bytes=2ULL*1024*1024;
    if(filtering && checkpoint_budget)cadence->reservation=checkpoint_budget->reserve(checkpoint_bytes);
    const auto cache=cadence->reservation ? recording_overview_cache() : std::shared_ptr<lvm::DiskCache>{};
    const auto max_checkpoints=std::min<std::size_t>(512,checkpoint_bytes/
        (4*(sizeof(Checkpoint)+raw->channel_count()*(FilterStream::snapshot_bytes()+sizeof(std::size_t)))));
    const auto total_rows=g.recording_rows ? g.recording_rows : g.ds.rows();
    const auto checkpoint_rows=std::max<std::size_t>(4096,total_rows/(max_checkpoints ? max_checkpoints : 1)+1);
    std::ostringstream state_identity;state_identity<<std::setprecision(17)<<"filter-cont-v1:"<<raw->cache_identity()<<':'
        <<raw->bounds().start<<':'<<raw->bounds().end<<':'<<key.str();
    for(auto column:raw->physical_columns())state_identity<<':'<<column;
    const auto persistence=state_identity.str();
    auto result=lvm::RecordingSource::processed(raw,key.str(),
        [raw,kinds,mul,add,formulas,global,identities,global_identity,global_affine,global_mul,global_add,filtering,filter,cadence,
         cache,checkpoint_budget,max_checkpoints,checkpoint_rows,persistence]
        (const lvm::RecordingReadRequest& requested,const lvm::RecordingBlockConsumer& consume,const std::atomic<bool>* cancel) {
            lvm::TimeRange range;requested.range.resolve(raw->bounds(),range);
            auto read=requested;auto effective=filter;
            // Timeline-only callers never run formulas or allocate filter columns.
            const bool filtered=filtering && !read.channels.empty();
            auto input=raw;
            // Live replays reserve their own pending/restored snapshots.
            // Failure disables optional retention, never the actual filter.
            auto continuation_work=filtered && cadence->reservation
                ? checkpoint_budget->reserve(checkpoint_bytes) : nullptr;
            std::vector<FilterStream> filters;
            if(filtered) {
                std::lock_guard<std::mutex> lock(cadence->mutex);
                if(!cadence->loaded && cache && continuation_work) {
                    effective.sample_step=cadence->step;
                    if(load_filter_checkpoints(cache,raw,persistence,effective,max_checkpoints,cancel,cadence->checkpoints))
                        cadence->step=effective.sample_step;
                    cadence->loaded=true;
                }
                if(!cadence->step) {
                    lvm::FrfSource timeline;
                    timeline.replay=[raw](const auto& visitor,const auto* flag) {
                        lvm::RecordingReadRequest time;time.block_bytes=1024*1024;time.block_rows=4096;raw->replay(time,visitor,flag);
                    };
                    const auto inspected=lvm::inspect_frf_timeline(timeline,cancel);
                    if(inspected.error!=lvm::FrfError::None)throw lvm::FrfSourceFailure(inspected.error);
                    cadence->step=inspected.sample_dt;
                }
                effective.sample_step=cadence->step;
                read.range=lvm::AnalysisRange::selected(raw->bounds().start,range.end);
                if(cache && !cadence->indexed) {
                    if(raw->resident_backed() || raw->scan_index())cadence->indexed=raw;
                    else {
                        lvm::ScanIndexCacheOptions options;options.memory_bytes=1024*1024;options.budget=checkpoint_budget;
                        const auto scanned=lvm::scan_index_cached(raw->path(),cache,options,cancel);
                        cadence->indexed=lvm::RecordingSource::from_file(raw->path(),raw->bounds(),raw->physical_columns(),
                            scanned.index,raw->stamp(),raw->filter_sample_step());
                    }
                }
                if(cadence->indexed)input=cadence->indexed;
                for(auto at=cadence->checkpoints.rbegin();at!=cadence->checkpoints.rend();++at) {
                    if(at->time>=range.start)continue;
                    bool covered=true;
                    for(auto channel:read.channels)covered=covered && std::find(at->channels.begin(),at->channels.end(),channel)!=at->channels.end();
                    if(!covered)continue;
                    filters.reserve(read.channels.size());
                    for(auto channel:read.channels)filters.push_back(at->filters[std::find(at->channels.begin(),at->channels.end(),channel)-at->channels.begin()].clone());
                    read.range=lvm::AnalysisRange::selected(std::nextafter(at->time,std::numeric_limits<double>::infinity()),range.end);
                    break;
                }
            }
            const bool collect=filtered && cadence->reservation && continuation_work && max_checkpoints && read.channels.size()<=raw->channel_count();
            if(filtered && filters.empty())for(std::size_t c=0;c<read.channels.size();++c)filters.emplace_back(effective);
            std::vector<Checkpoint> pending;
            if(collect)pending.reserve(max_checkpoints);
            std::size_t since_checkpoint=0;
            // Raw + transformed + filtered buffers share the request allowance.
            read.block_rows=std::min<std::size_t>(read.block_rows,4096);
            if(read.block_bytes/4<16*(read.channels.size()+1))throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,"No allowance for a processed recording row.");
            read.block_bytes/=4;
            input->replay(read,[&](const auto& time,const auto& values) {
                auto processed=values;
                for(std::size_t c=0;c<processed.size();++c) {
                    const auto ci=read.channels[c];
                    for(std::size_t i=0;i<processed[c].size();++i) {
                        if((i&4095)==0 && cancel && cancel->load())throw lvm::RecordingFailure(lvm::RecordingError::Cancelled,"Operation cancelled.");
                        const double x=processed[c][i];double y=x;
                        if(kinds[ci]==TransformRuntimeKind::Affine)y=x*mul[ci]+add[ci];
                        else if(kinds[ci]==TransformRuntimeKind::CachedFormula) {
                            y=!std::isfinite(x) ? std::numeric_limits<double>::quiet_NaN() : global_identity ? x : global_affine ? x*global_mul+global_add : eval_formula_rpn(global,x);
                            y=std::isfinite(y) ? identities[ci] ? y : eval_formula_rpn(formulas[ci],y) : std::numeric_limits<double>::quiet_NaN();
                            if(!std::isfinite(y))y=std::numeric_limits<double>::quiet_NaN();
                        }
                        processed[c][i]=y;
                    }
                    if(filtered)processed[c]=filters[c].process(time,processed[c],cancel);
                }
                since_checkpoint+=time.size();
                if(collect && !time.empty() && since_checkpoint>=checkpoint_rows && pending.size()<max_checkpoints) {
                    Checkpoint saved;saved.time=time.back();saved.filters.reserve(filters.size());saved.channels.reserve(filters.size());
                    for(std::size_t c=0;c<filters.size();++c)if(std::find(saved.channels.begin(),saved.channels.end(),read.channels[c])==saved.channels.end()) {
                        saved.channels.push_back(read.channels[c]);saved.filters.push_back(filters[c].clone());
                    }
                    pending.push_back(std::move(saved));since_checkpoint=0;
                }
                const auto first=std::lower_bound(time.begin(),time.end(),range.start)-time.begin();
                if(first==static_cast<std::ptrdiff_t>(time.size()))return;
                if(!first)consume(time,processed);
                else {
                    std::vector<double> selected(time.begin()+first,time.end());
                    for(auto& column:processed)column.erase(column.begin(),column.begin()+first);
                    consume(selected,processed);
                }
            },cancel);
            // Publish only a fully successful pass. A cancelled/changed source
            // cannot leave partial continuation states in the shared recipe.
            if(!pending.empty()) {
                std::lock_guard<std::mutex> lock(cadence->mutex);
                for(auto& saved:pending) {
                    auto at=std::lower_bound(cadence->checkpoints.begin(),cadence->checkpoints.end(),saved.time,
                        [](const auto& state,double time){return state.time<time;});
                    bool duplicate=false;
                    for(auto same=at;same!=cadence->checkpoints.end() && same->time==saved.time;++same)
                        duplicate=duplicate || same->channels==saved.channels;
                    if(duplicate)continue;
                    cadence->checkpoints.insert(at,std::move(saved));
                    if(cadence->checkpoints.size()>max_checkpoints) {
                        // Thin the most densely covered interior interval; keep
                        // continuation coverage near both ends of the record.
                        std::size_t remove=0;double span=std::numeric_limits<double>::infinity();
                        for(std::size_t i=1;i+1<cadence->checkpoints.size();++i) {
                            const auto width=cadence->checkpoints[i+1].time-cadence->checkpoints[i-1].time;
                            if(width<span){span=width;remove=i;}
                        }
                        cadence->checkpoints.erase(cadence->checkpoints.begin()+remove);
                    }
                }
                save_filter_checkpoints(cache,raw,persistence,cadence->step,cadence->checkpoints,cancel);
            }
        });
    g.processed_source_origin=raw;g.processed_source=result;return result;
}
}
