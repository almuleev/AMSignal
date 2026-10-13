#include "gui_recording_processing.hpp"
#include "gui_analysis_source.hpp"
#include "gui_processing.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include "frf_stream.hpp"
#include <sstream>
#include <iomanip>
#include <mutex>

namespace gui {
std::shared_ptr<const lvm::RecordingSource> processed_recording_source(bool apply) {
    const auto raw=current_recording_source();
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
    const FilterSettings filter{g.noise_threshold_mode,g.noise_threshold_topology,g.noise_threshold_min,g.noise_threshold_max,0};
    bool changed=filtering;for(auto kind:kinds)changed=changed || kind!=TransformRuntimeKind::Identity;
    if(!changed)return raw;
    // Text is full precision and includes the recipe, not worker generations or styles.
    std::ostringstream key;key<<std::setprecision(17)<<"recipe2:"<<filtering<<':'<<filter.mode<<':'<<filter.topology<<':'
        <<filter.low_cutoff<<':'<<filter.high_cutoff<<':'<<raw->filter_sample_step()<<':'<<to_utf8(g.global_formula);
    for(const auto& formula:g.channel_formulas) {const auto text=to_utf8(formula);key<<':'<<text.size()<<':'<<text;}
    if(g.processed_source_origin==raw && g.processed_source &&
       g.processed_source->processing_identity()=="\nprocessed-v1:"+key.str())return g.processed_source;
    struct Cadence {std::mutex mutex;double step=0;};
    auto cadence=std::make_shared<Cadence>();
    cadence->step=raw->filter_sample_step();
    auto result=lvm::RecordingSource::processed(raw,key.str(),
        [raw,kinds,mul,add,formulas,global,identities,global_identity,global_affine,global_mul,global_add,filtering,filter,cadence]
        (const lvm::RecordingReadRequest& requested,const lvm::RecordingBlockConsumer& consume,const std::atomic<bool>* cancel) {
            lvm::TimeRange range;requested.range.resolve(raw->bounds(),range);
            auto read=requested;auto effective=filter;
            // Timeline-only callers never run formulas or allocate filter columns.
            const bool filtered=filtering && !read.channels.empty();
            if(filtered) {
                std::lock_guard<std::mutex> lock(cadence->mutex);
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
            }
            std::vector<FilterStream> filters;
            if(filtered)for(std::size_t c=0;c<read.channels.size();++c)filters.emplace_back(effective);
            // Raw + transformed + filtered buffers share the request allowance.
            read.block_rows=std::min<std::size_t>(read.block_rows,4096);
            if(read.block_bytes/4<16*(read.channels.size()+1))throw lvm::RecordingFailure(lvm::RecordingError::ResourceLimit,"No allowance for a processed recording row.");
            read.block_bytes/=4;
            raw->replay(read,[&](const auto& time,const auto& values) {
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
                const auto first=std::lower_bound(time.begin(),time.end(),range.start)-time.begin();
                if(first==static_cast<std::ptrdiff_t>(time.size()))return;
                if(!first)consume(time,processed);
                else {
                    std::vector<double> selected(time.begin()+first,time.end());
                    for(auto& column:processed)column.erase(column.begin(),column.begin()+first);
                    consume(selected,processed);
                }
            },cancel);
        });
    g.processed_source_origin=raw;g.processed_source=result;return result;
}
}
