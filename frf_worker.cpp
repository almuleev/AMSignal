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
void FrfWorker::submit(FrfBatchInput data,FrfOptions options,std::uint64_t generation,std::string key) {
    auto flag = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    if (!thread_.joinable()) thread_ = std::thread(&FrfWorker::run, this);
    if (active_cancel_) active_cancel_->store(true);
    if (pending_) pending_->cancelled->store(true);
    active_cancel_ = flag;
    pending_ = Request{std::move(data), {}, std::move(key), options, generation, flag};
    result_.reset(); ready_.notify_one();
}
void FrfWorker::submit(FrfSource source,FrfOptions options,std::uint64_t generation,std::string key) {
    auto flag=std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    if(!thread_.joinable())thread_=std::thread(&FrfWorker::run,this);
    if(active_cancel_)active_cancel_->store(true);
    if(pending_)pending_->cancelled->store(true);
    active_cancel_=flag;
    pending_=Request{{},std::move(source),std::move(key),options,generation,flag};
    result_.reset();ready_.notify_one();
}
bool FrfWorker::submit_cached(FrfOptions options,std::uint64_t generation,const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_ || key.empty() || key!=cached_key_ || !cached_input_)return false;
    auto flag=std::make_shared<std::atomic<bool>>(false);
    if(active_cancel_)active_cancel_->store(true);
    if(pending_)pending_->cancelled->store(true);
    active_cancel_=flag;
    pending_=Request{{},{},key,options,generation,flag};
    result_.reset();ready_.notify_one();return true;
}
void FrfWorker::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_cancel_) active_cancel_->store(true);
    pending_.reset(); result_.reset();
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
            std::shared_ptr<const PreparedFrf> prepared;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!request->input_key.empty() && request->input_key==cached_key_)prepared=cached_input_;
                else {cached_input_.reset();cached_key_.clear();}
            }
            if(!prepared)prepared=request->source.replay
                ? prepare_frf_stream(request->source,request->cancelled.get())
                : prepare_frf_batch(std::move(request->data),request->cancelled.get());
            if(!request->cancelled->load() && prepared->resident_bytes<=64ULL*1024*1024 &&
               prepared->disk_bytes<=256ULL*1024*1024 && !request->input_key.empty()) {
                std::lock_guard<std::mutex> lock(mutex_);
                if(!stopping_ && !request->cancelled->load()) {cached_input_=prepared;cached_key_=request->input_key;}
            }
            r=compute_prepared_frf(*prepared,request->options,request->cancelled.get());
        }
        catch (const FrfSourceFailure& failure) {r.error=failure.error;}
        catch (const std::bad_alloc&) {r.error=FrfError::ResourceLimit;}
        catch (const std::filesystem::filesystem_error&) {r.error=FrfError::ResourceLimit;}
        catch (...) { r.error = FrfError::Overflow; }
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopping_ && !request->cancelled->load()) result_ = Result{request->generation, std::move(r)};
    }
}
} // namespace lvm
