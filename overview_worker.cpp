#include "overview_worker.hpp"
#include "scan_index_cache.hpp"
#include <algorithm>
#include <cmath>

namespace lvm {
OverviewWorker::~OverviewWorker() { shutdown(); }
void OverviewWorker::submit(Request request) {
    auto flag=std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    if(!thread_.joinable())thread_=std::thread(&OverviewWorker::run,this);
    if(active_cancel_)active_cancel_->store(true);
    pending_.reset();result_.reset();
    active_cancel_=flag;
    progress_={request.generation,0,0,request.index ? Phase::Reading : Phase::Building};
    pending_=Job{std::move(request),flag};
    ready_.notify_one();
}
void OverviewWorker::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(active_cancel_)active_cancel_->store(true);
    pending_.reset();result_.reset();progress_={};
}
void OverviewWorker::submit_snap(SnapRequest request) {
    auto flag=std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);if(stopping_)return;
    if(!thread_.joinable())thread_=std::thread(&OverviewWorker::run,this);
    if(snap_cancel_)snap_cancel_->store(true);
    pending_snap_.reset();snap_result_.reset();snap_cancel_=flag;
    pending_snap_=SnapJob{std::move(request),flag};ready_.notify_one();
}
void OverviewWorker::cancel_snap() {
    std::lock_guard<std::mutex> lock(mutex_);
    if(snap_cancel_)snap_cancel_->store(true);
    pending_snap_.reset();snap_result_.reset();
}
std::optional<OverviewWorker::SnapResult> OverviewWorker::take_snap_result() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result=std::move(snap_result_);snap_result_.reset();return result;
}
void OverviewWorker::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_=true;
        if(active_cancel_)active_cancel_->store(true);
        if(snap_cancel_)snap_cancel_->store(true);
        pending_.reset();result_.reset();progress_={};
        pending_snap_.reset();snap_result_.reset();
    }
    ready_.notify_one();
    if(thread_.joinable())thread_.join();
}
std::optional<OverviewWorker::Result> OverviewWorker::take_result() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result=std::move(result_);result_.reset();return result;
}
OverviewWorker::Progress OverviewWorker::progress() const {
    std::lock_guard<std::mutex> lock(mutex_);return progress_;
}
void OverviewWorker::run() {
    for(;;) {
        std::optional<Job> job;
        std::optional<SnapJob> snap;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock,[&]{return stopping_ || pending_.has_value() || pending_snap_.has_value();});
            if(stopping_)return;
            if(pending_snap_){snap=std::move(pending_snap_);pending_snap_.reset();}
            else {job=std::move(pending_);pending_.reset();}
        }
        if(snap) {
            auto& request=snap->request;SnapResult result;
            result.generation=request.generation;result.source=request.source;
            try {
                if(!request.source || !request.index || request.index->source()!=request.source)
                    throw RecordingFailure(RecordingError::InvalidRequest,"Missing original sample index or source mismatch.");
                result.point=request.index->nearest_sample(request.query,snap->cancelled.get());
            }catch(const RecordingFailure& e){result.error=e.error;}
             catch(const std::bad_alloc&){result.error=RecordingError::ResourceLimit;}
             catch(...){result.error=RecordingError::ReadFailure;}
            if(result.error)result.point={};
            std::lock_guard<std::mutex> lock(mutex_);
            if(!stopping_ && snap_cancel_==snap->cancelled && !snap->cancelled->load()) {
                snap_result_=std::move(result);
            }
            continue;
        }
        auto& request=job->request;
        Result result;result.generation=request.generation;result.source=request.source;
        try {
            if(!request.source)throw RecordingFailure(RecordingError::InvalidRequest,"Missing overview source.");
            request.source->check_current();
            TimeRange range;
            if(!request.view.pixel_columns || !request.view.range.resolve(request.source->bounds(),range))
                throw RecordingFailure(RecordingError::InvalidRequest,"Invalid overview range or width.");
            for(auto channel:request.view.channels)if(channel>=request.source->channel_count())
                throw RecordingFailure(RecordingError::InvalidRequest,"Invalid overview channel.");
            auto index=request.index;
            if(index && index->source().get()!=request.source.get())
                throw RecordingFailure(RecordingError::InvalidRequest,"Overview source mismatch.");
            if(index && index->disk_bytes()>request.build.max_disk_bytes)
                throw RecordingFailure(RecordingError::ResourceLimit,"Overview disk budget exceeded.");
            if(!index) {
                request.build.progress=[this,flag=job->cancelled](std::uint64_t rows,std::uint64_t bytes) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if(!stopping_ && active_cancel_==flag && !flag->load()) {
                        progress_.rows=rows;progress_.disk_bytes=bytes;
                    }
                };
                index=request.managed_cache ? managed_recording_overview(request.source,request.managed_cache,request.build,job->cancelled.get()) :
                    RecordingOverview::open_or_build(request.source,request.cache,request.build,job->cancelled.get());
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(active_cancel_==job->cancelled && !job->cancelled->load())
                    progress_={request.generation,index->sample_count(),index->disk_bytes(),Phase::Reading};
            }
            auto view=index->read_view(request.view,job->cancelled.get());
            if(request.gap_details) {
                if(request.view.budget) {
                    result.details_reservation=request.view.budget->reserve(2ULL*1024*1024);
                    if(!result.details_reservation)throw RecordingFailure(RecordingError::ResourceLimit,"No budget for overview gap details.");
                }
                result.gap_step=request.gap_step;
                if(result.gap_step<0) {
                    // Preserve the existing GUI rule: first 4096 sampled
                    // intervals at stride (N-1)/4096, over the whole source.
                    const auto stride=std::max<std::uint64_t>(1,(index->sample_count()-1)/4096);
                    std::vector<double> diffs;diffs.reserve(4096);
                    std::uint64_t row=0;double previous=0;
                    RecordingReadRequest timeline;timeline.block_bytes=1024*1024;timeline.block_rows=4096;
                    request.source->replay(timeline,[&](const auto& time,const auto&) {
                        for(auto t:time) {
                            if(row && (row-1)%stride==0 && diffs.size()<4096) {
                                const auto d=t-previous;if(std::isfinite(d) && d>0)diffs.push_back(d);
                            }
                            previous=t;++row;
                        }
                    },job->cancelled.get());
                    std::sort(diffs.begin(),diffs.end());
                    const auto mid=diffs.size()/2;
                    result.gap_step=diffs.empty() ? 0 : diffs.size()%2 ? diffs[mid] : .5*(diffs[mid-1]+diffs[mid]);
                }
                if(result.gap_step>0 && range.end>range.start) {
                    const auto pixels=std::min<std::size_t>(8192,request.view.pixel_columns);
                    std::vector<OverviewGap> slots(pixels);
                    auto options=request.build;options.memory_bytes=1024*1024;options.budget.reset();
                    const auto collect=[&](OverviewGap gap) {
                        const auto fraction=(gap.left-range.start)/(range.end-range.start);
                        const auto bin=static_cast<std::size_t>(std::clamp(fraction*static_cast<double>(pixels),0.0,static_cast<double>(pixels-1)));
                        // Multiple gaps in one pixel retain the largest original
                        // gap for its card; never fabricate a merged duration.
                        if(gap.right-gap.left>slots[bin].right-slots[bin].left)slots[bin]=gap;
                    };
                    const auto bounds=request.source->bounds();
                    if(range.start==bounds.start && range.end==bounds.end) {
                        // A complete overview must not repeatedly replay leaf
                        // prefixes. One time-only pass preserves every boundary.
                        bool have_previous=false;double previous=0;
                        RecordingReadRequest timeline;timeline.block_bytes=1024*1024;timeline.block_rows=4096;
                        request.source->replay(timeline,[&](const auto& time,const auto&) {
                            for(auto t:time) {
                                if(have_previous && t-previous>result.gap_step*64)collect({previous,t});
                                previous=t;have_previous=true;
                            }
                        },job->cancelled.get());
                    } else index->visit_gaps(range,result.gap_step*64,collect,job->cancelled.get(),options);
                    for(auto gap:slots)if(gap.right>gap.left)result.gaps.push_back(gap);
                }
            }
            result.view=std::make_shared<const OverviewView>(std::move(view));
            result.index=std::move(index);
            result.channels=std::move(request.view.channels);
            request.source->check_current();
        } catch(const RecordingFailure& e) { result.error=e.error; }
          catch(const std::bad_alloc&) { result.error=RecordingError::ResourceLimit; }
          catch(...) { result.error=RecordingError::ReadFailure; }
        // Never expose an index/view as a successful partial result after an error.
        if(result.error) {result.view.reset();result.index.reset();}
        std::lock_guard<std::mutex> lock(mutex_);
        if(!stopping_ && active_cancel_==job->cancelled && !job->cancelled->load()) {
            progress_.phase=Phase::Idle;
            result_.reset();result_.emplace(std::move(result));
        }
    }
}
} // namespace lvm
