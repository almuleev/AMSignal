#pragma once
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace lvm {
inline constexpr std::size_t analysis_memory_budget = 384ULL * 1024 * 1024;
// Shared by Spectrum and FRF. Synchronous admission; contention never waits.
class AnalysisExecutor {
    std::mutex admission_, mutex_;
    std::condition_variable ready_, done_;
    std::vector<std::thread> threads_;
    const std::function<void(std::size_t)>* job_ = nullptr;
    std::atomic<std::size_t> next_{0};
    std::size_t count_=0, generation_=0, pending_=0, participants_=1;
    bool stopping_=false;
    std::exception_ptr error_;
    void drain() {
        try {
            for (;;) {
                const auto index=next_.fetch_add(1,std::memory_order_relaxed);
                if (index>=count_) return;
                (*job_)(index);
            }
        } catch (...) {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!error_) error_=std::current_exception();
            next_.store(count_,std::memory_order_relaxed);
        }
    }
    void worker(std::size_t id) {
        std::size_t seen=0;
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            ready_.wait(lock,[&]{return stopping_ || generation_!=seen;});
            if (stopping_) return;
            seen=generation_;
            if (id>=participants_-1) continue;
            lock.unlock(); drain(); lock.lock();
            if (--pending_==0) done_.notify_one();
        }
    }
public:
    AnalysisExecutor() {
        try { for (std::size_t i=0;i<3;++i) threads_.emplace_back([this,i]{worker(i);}); }
        catch (...) {
            { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
            ready_.notify_all();
            for (auto& thread:threads_) thread.join();
            throw;
        }
    }
    ~AnalysisExecutor() {
        std::lock_guard<std::mutex> admission(admission_);
        { std::lock_guard<std::mutex> lock(mutex_); stopping_=true; }
        ready_.notify_all();
        for (auto& thread:threads_) thread.join();
    }
    bool run(std::size_t count,std::size_t participants,const std::function<void(std::size_t)>& job) {
        if(!count)return true;
        std::unique_lock<std::mutex> admission(admission_,std::try_to_lock);
        if (!admission.owns_lock()) return false;
        participants=std::clamp<std::size_t>(participants,1,std::min<std::size_t>(4,count));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_=&job; count_=count; next_=0; error_=nullptr;
            participants_=participants; pending_=participants-1; ++generation_;
        }
        ready_.notify_all(); drain();
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock,[&]{return pending_==0;});
        job_=nullptr;
        if (error_) std::rethrow_exception(error_);
        return true;
    }
};
inline AnalysisExecutor& analysis_executor() { static AnalysisExecutor executor; return executor; }
}
