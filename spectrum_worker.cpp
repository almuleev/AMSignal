#include "spectrum_worker.hpp"
#include <cmath>
#include "recording_source.hpp"

namespace lvm {
SpectrumWorker::~SpectrumWorker() {
    shutdown();
}

void SpectrumWorker::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        if (active_cancel_) active_cancel_->store(true);
        pending_.reset();
        result_.reset();
    }
    ready_.notify_one();
    if (thread_.joinable()) thread_.join();
}

void SpectrumWorker::submit(Dataset data, std::vector<std::size_t> channels, std::uint64_t generation,
                            std::vector<AffineTransform> transforms,AnalysisResources resources,
                            std::shared_ptr<MemoryBudget::Reservation> reservation,Compute fallback,std::shared_ptr<const RecordingSource> source) {
    auto flag = std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    if (!thread_.joinable()) thread_ = std::thread(&SpectrumWorker::run, this);
    if (active_cancel_) active_cancel_->store(true);
    if (pending_) pending_->cancelled->store(true);
    pending_.reset();
    active_cancel_ = flag;
    pending_ = Request{std::move(reservation),std::move(data),std::move(channels),std::move(transforms),generation,flag,std::move(resources),{}, {},std::move(fallback),std::move(source)};
    result_.reset();
    ready_.notify_one();
}

void SpectrumWorker::submit_source(Prepare prepare,std::vector<std::size_t> channels,
                                   std::uint64_t generation,AnalysisResources resources) {
    auto flag=std::make_shared<std::atomic<bool>>(false);
    std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    if(!thread_.joinable())thread_=std::thread(&SpectrumWorker::run,this);
    if(active_cancel_)active_cancel_->store(true);
    if(pending_)pending_->cancelled->store(true);
    pending_.reset();active_cancel_=flag;
    pending_=Request{{},{},std::move(channels),{},generation,flag,std::move(resources),std::move(prepare),{}, {}, {}};
    result_.reset();ready_.notify_one();
}

void SpectrumWorker::cancel() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_cancel_) active_cancel_->store(true);
    pending_.reset(); result_.reset();
}

void SpectrumWorker::submit_computation(Compute compute,std::vector<std::size_t> channels,std::uint64_t generation) {
    auto flag=std::make_shared<std::atomic<bool>>(false);std::lock_guard<std::mutex> lock(mutex_);
    if(stopping_)return;
    if(!thread_.joinable())thread_=std::thread(&SpectrumWorker::run,this);
    if(active_cancel_)active_cancel_->store(true);
    if(pending_)pending_->cancelled->store(true);
    pending_.reset();active_cancel_=flag;Request request{};request.compute=std::move(compute);
    request.generation=generation;request.cancelled=flag;request.source_channels=std::move(channels);
    pending_=std::move(request);result_.reset();ready_.notify_one();
}

std::optional<SpectrumWorker::Result> SpectrumWorker::take_result() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = std::move(result_); result_.reset(); return result;
}

void SpectrumWorker::run() {
    for (;;) {
        std::optional<Request> request;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            ready_.wait(lock, [&] { return stopping_ || pending_.has_value(); });
            if (stopping_) return;
            request = std::move(pending_); pending_.reset();
        }
        Spectrum spectrum;
        const auto fail=[&](const char* message=nullptr) {
            spectrum={};
            // Error reporting must not terminate the worker during allocation
            // failure; an empty failed result remains publishable.
            if(message)try{spectrum.error=message;}catch(...){}
        };
        bool allocation_failure=false;
        try {
            if(request->source)request->source->check_current();
            if(request->compute)spectrum=request->compute(request->cancelled.get());
            else {
            if(request->prepare)request->data=request->prepare(request->cancelled.get(),request->memory_reservation);
            if (!request->transforms.empty()) {
                if (request->transforms.size() != request->data.channels.size()) throw std::runtime_error("Invalid FFT transforms.");
                for (std::size_t c = 0; c < request->data.channels.size(); ++c) {
                    const auto transform = request->transforms[c];
                    if (transform.mul == 1.0 && transform.add == 0.0) continue;
                    for (std::size_t i = 0; i < request->data.channels[c].size(); ++i) {
                        if ((i & 0xffff) == 0 && request->cancelled->load()) throw std::runtime_error("Operation cancelled.");
                        const double value = request->data.channels[c][i];
                        if (std::isfinite(value)) request->data.channels[c][i] = value * transform.mul + transform.add;
                    }
                }
            }
            spectrum = compute_spectrum(request->data,0,request->cancelled.get(),request->resources);
            if(request->source){request->source->check_current();spectrum.recording_source=request->source;}
            }
        } catch(const std::bad_alloc&) {
            allocation_failure=true;fail("Out of memory");
        } catch (const std::exception& e) {
            fail(e.what());
        } catch (...) {
            fail();
        }
        if(request->fallback && !request->cancelled->load() &&
           (allocation_failure || (!spectrum.ok && spectrum.error.rfind("FFT resources are insufficient",0)==0))) {
            // No live RAM candidate may overlap the disk retry's admission.
            request->data={};request->memory_reservation.reset();spectrum={};
            try{spectrum=request->fallback(request->cancelled.get());}
            catch(const std::exception& e){fail(e.what());}
            catch(...){fail();}
        }
        try{for(auto& c:spectrum.source_channels)c=request->source_channels.at(c);}
        catch(const std::exception& e){fail(e.what());}
        catch(...){fail();}
        std::lock_guard<std::mutex> lock(mutex_);
        if (!stopping_ && !request->cancelled->load()) {
            result_.reset();result_ = Result{request->generation,std::move(spectrum)};
        }
    }
}

} // namespace lvm
