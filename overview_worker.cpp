#include "overview_worker.hpp"
#include "scan_index_cache.hpp"
#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>

namespace lvm {
OverviewWorker::~OverviewWorker() { shutdown(); }
void OverviewWorker::submit(Request request) {
    auto flag=std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    cache_limit_=request.cache_bytes;
    while(!cached_views_.empty() && cached_bytes_>request.cache_bytes) {
        cached_bytes_-=cached_views_.front().bytes;cached_views_.erase(cached_views_.begin());
    }
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
        cached_views_.clear();cached_bytes_=0;
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
void OverviewWorker::trim_cache(std::size_t bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_limit_=bytes;
    while(!cached_views_.empty() && cached_bytes_>bytes) {
        cached_bytes_-=cached_views_.front().bytes;cached_views_.erase(cached_views_.begin());
    }
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
        std::string view_key;
        try {
            if(!request.source)throw RecordingFailure(RecordingError::InvalidRequest,"Missing overview source.");
            request.source->check_current();
            TimeRange range;
            if(!request.view.pixel_columns || !request.view.range.resolve(request.source->bounds(),range))
                throw RecordingFailure(RecordingError::InvalidRequest,"Invalid overview range or width.");
            for(auto channel:request.view.channels)if(channel>=request.source->channel_count())
                throw RecordingFailure(RecordingError::InvalidRequest,"Invalid overview channel.");
            if(request.cache_bytes) {
                std::ostringstream key;key<<request.source.get()<<':'<<std::hexfloat<<range.start<<':'<<range.end<<':'
                    <<request.view.pixel_columns<<':'<<request.view.display_density<<':'<<request.view.memory_bytes<<':'
                    <<request.gap_details<<':'<<request.gap_step<<':'<<request.build.max_disk_bytes;
                for(auto c:request.view.channels)key<<':'<<c;
                view_key=key.str();
                std::lock_guard<std::mutex> lock(mutex_);
                for(std::size_t i=0;i<cached_views_.size();++i)if(cached_views_[i].key==view_key) {
                    auto hit=std::move(cached_views_[i]);cached_views_.erase(cached_views_.begin());
                    cached_bytes_-=hit.bytes;result=std::move(hit.result);result.generation=request.generation;break;
                }
            }
            if(!result.view) {
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
                    result.gap_step=index->gap_step();
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
                    index->visit_gaps(range,result.gap_step*64,collect,job->cancelled.get(),options);
                    for(auto gap:slots)if(gap.right>gap.left)result.gaps.push_back(gap);
                }
            }
            result.view=std::make_shared<const OverviewView>(std::move(view));
            result.index=std::move(index);
            result.channels=std::move(request.view.channels);
            }
            request.source->check_current();
        } catch(const RecordingFailure& e) { result.error=e.error; }
          catch(const std::bad_alloc&) { result.error=RecordingError::ResourceLimit; }
          catch(...) { result.error=RecordingError::ReadFailure; }
        // Never expose an index/view as a successful partial result after an error.
        if(result.error) {result.view.reset();result.index.reset();}
        std::lock_guard<std::mutex> lock(mutex_);
        if(!stopping_ && active_cancel_==job->cancelled && !job->cancelled->load()) {
            if(!result.error && result.view && !view_key.empty()) {
                const auto bytes=result.view->resident_bytes()+(result.details_reservation ? result.details_reservation->bytes() : result.gaps.capacity()*sizeof(OverviewGap));
                for(std::size_t i=0;i<cached_views_.size();++i)if(cached_views_[i].key==view_key) {
                    cached_bytes_-=cached_views_[i].bytes;cached_views_.erase(cached_views_.begin()+i);break;
                }
                const auto allowance=std::min(cache_limit_,request.cache_bytes);
                if(bytes<=allowance) {
                    while(!cached_views_.empty() && (cached_views_.size()>=4 || cached_bytes_+bytes>allowance)) {
                        cached_bytes_-=cached_views_.front().bytes;cached_views_.erase(cached_views_.begin());
                    }
                    try{cached_views_.push_back({view_key,result,bytes});cached_bytes_+=bytes;}
                    catch(const std::bad_alloc&){} // Optional retention cannot reject a completed view.
                }
            } else if(result.error==RecordingError::SourceChanged) {cached_views_.clear();cached_bytes_=0;}
            progress_.phase=Phase::Idle;
            result_.reset();result_.emplace(std::move(result));
        }
    }
}
} // namespace lvm
