#include "frf_worker.hpp"
#include <filesystem>

namespace lvm {
FrfWorker::~FrfWorker() {
    shutdown();
}
void FrfWorker::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (active_cancel_) active_cancel_->store(true);
        pending_.reset();
        result_.reset();cached_input_.reset();cached_key_.clear();
    }
    ready_.notify_one();
    if (thread_.joinable()) thread_.join();
}
void FrfWorker::submit(FrfInput data, FrfOptions options, std::uint64_t generation) {
    FrfBatchInput batch;
    batch.time=std::move(data.time);
    batch.references.push_back(std::move(data.reference));
    batch.responses.push_back(std::move(data.response));
    submit(std::move(batch),options,generation);
}
void FrfWorker::submit(FrfBatchInput data, FrfOptions options, std::uint64_t generation) {
    submit(std::move(data),options,generation,{});
}
void FrfWorker::submit(FrfBatchInput data,FrfOptions options,std::uint64_t generation,std::string key,
                      AnalysisResources resources,std::shared_ptr<MemoryBudget::Reservation> reservation) {
    auto flag = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    if (!thread_.joinable()) thread_ = std::thread(&FrfWorker::run, this);
    if (active_cancel_) active_cancel_->store(true);
    if (pending_) pending_->cancelled->store(true);
    pending_.reset();
    active_cancel_ = flag;
    pending_ = Request{std::move(reservation),std::move(data),{},std::move(key),options,generation,flag,std::move(resources),cache_epoch_,{}};
    result_.reset(); ready_.notify_one();
}
void FrfWorker::submit(FrfSource source,FrfOptions options,std::uint64_t generation,std::string key,
                      AnalysisResources resources,std::shared_ptr<MemoryBudget::Reservation> reservation) {
    auto flag=std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    if(!thread_.joinable())thread_=std::thread(&FrfWorker::run,this);
    if(active_cancel_)active_cancel_->store(true);
    if(pending_)pending_->cancelled->store(true);
    pending_.reset();
    active_cancel_=flag;
    pending_=Request{std::move(reservation),{},std::move(source),std::move(key),options,generation,flag,std::move(resources),cache_epoch_,{}};
    result_.reset();ready_.notify_one();
}
bool FrfWorker::submit_cached(FrfOptions options,std::uint64_t generation,const std::string& key,AnalysisResources resources) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_ || key.empty() || key!=cached_key_ || !cached_input_)return false;
    auto flag=std::make_shared<std::atomic<bool>>(false);
    if(active_cancel_)active_cancel_->store(true);
    if(pending_)pending_->cancelled->store(true);
    pending_.reset();
    active_cancel_=flag;
    // The queued job owns its input even if idle retention is evicted before
    // this worker finishes the preceding cancelled job.
    pending_=Request{{},{},{},key,options,generation,flag,std::move(resources),cache_epoch_,cached_input_};
    result_.reset();ready_.notify_one();return true;
}
void FrfWorker::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_cancel_) active_cancel_->store(true);
    pending_.reset(); result_.reset();
}
void FrfWorker::clear_cache() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++cache_epoch_;
    cached_input_.reset();cached_key_.clear();
}
void FrfWorker::trim_cache(std::size_t memory_bytes,std::size_t disk_bytes) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(memory_bytes<cache_memory_limit_ || disk_bytes<cache_disk_limit_)++cache_epoch_;
    cache_memory_limit_=memory_bytes;cache_disk_limit_=disk_bytes;
    if(cached_input_ && (cached_input_->resident_bytes>memory_bytes || cached_input_->disk_bytes>disk_bytes)) {
        cached_input_.reset();cached_key_.clear();
    }
}
std::optional<FrfWorker::Result> FrfWorker::take_result() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto r = std::move(result_); result_.reset(); return r;
}
void FrfWorker::run() {
    for (;;) {
        std::optional<Request> request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (stopping_) return;
            request = std::move(pending_); pending_.reset();
        }
        FrfBatchResult r; r.options=request->options;
        try {
            auto prepared=std::move(request->prepared);
            if(!prepared) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!request->input_key.empty() && request->input_key==cached_key_)prepared=cached_input_;
                else {cached_input_.reset();cached_key_.clear();}
            }
            if(!prepared) {
                prepared=request->source.replay
                    ? prepare_frf_stream(request->source,request->cancelled.get(),request->memory_reservation,request->resources)
                    : prepare_frf_batch(std::move(request->data),request->cancelled.get(),request->memory_reservation);
                if(request->memory_reservation) {
                    const auto retained=prepared->memory_reservation==request->memory_reservation ? prepared->resident_bytes : 0;
                    if(!request->memory_reservation->shrink_to(retained))throw FrfSourceFailure(FrfError::ResourceLimit);
                }
            } else {
                request->data={};request->source={};request->memory_reservation.reset();
            }
            if(!request->cancelled->load() && prepared->resident_bytes<=request->resources.cache_memory_bytes &&
               prepared->disk_bytes<=request->resources.cache_disk_bytes && !request->input_key.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!stopping_ && !request->cancelled->load() && request->cache_epoch==cache_epoch_ &&
                   prepared->resident_bytes<=cache_memory_limit_ && prepared->disk_bytes<=cache_disk_limit_) {
                    cached_input_=prepared;cached_key_=request->input_key;
                }
            }
            r=compute_prepared_frf(*prepared,request->options,request->cancelled.get(),request->resources);
        }
        catch (const FrfSourceFailure& failure) {r.error=failure.error;}
        catch (const std::bad_alloc&) {r.error=FrfError::ResourceLimit;}
        catch (const std::filesystem::filesystem_error&) {r.error=FrfError::ResourceLimit;}
        catch (...) { r.error = FrfError::Overflow; }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopping_ && !request->cancelled->load()) {
            result_.reset();result_ = Result{request->generation,std::move(r)};
        }
    }
}
} // namespace lvm
