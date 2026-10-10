#include "frf_stream.hpp"
#include "sampling.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <queue>
#include <stdexcept>

namespace lvm {
namespace {
void check(const std::atomic<bool>* c) { if(c && c->load()) throw std::runtime_error("Operation cancelled."); }
struct Scratch {
    std::filesystem::path directory;
    Scratch() {
        static std::atomic<unsigned long long> counter{0};
        const auto root=std::filesystem::temp_directory_path();
        for(unsigned attempt=0;attempt<100;++attempt) {
            directory=root/("ams-frf-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+
                            "-"+std::to_string(counter.fetch_add(1)));
            if(std::filesystem::create_directory(directory)) return;
        }
        throw FrfSourceFailure(FrfError::ResourceLimit);
    }
    ~Scratch() { std::error_code ec; std::filesystem::remove_all(directory,ec); }
};
// Exact external merge sort: preserves every interval and the old cluster rule.
// Runs are 8 MiB; merge fan-in is capped at 32 (bounded descriptors/buffers).
class Cadence {
    std::unique_ptr<Scratch> scratch;
    std::vector<double> pending;
    std::vector<std::filesystem::path> runs;
    std::size_t count=0, serial=0;
    const std::atomic<bool>* cancel;
    void flush() {
        if(pending.empty())return;
        check(cancel); std::sort(pending.begin(),pending.end()); check(cancel);
        if(!scratch)scratch=std::make_unique<Scratch>();
        const auto path=scratch->directory/std::to_string(serial++);
        std::ofstream out(path,std::ios::binary);
        out.write(reinterpret_cast<const char*>(pending.data()),pending.size()*sizeof(double));
        out.close();
        if(!out)throw FrfSourceFailure(FrfError::ResourceLimit);
        runs.push_back(path); pending.clear();
    }
    std::filesystem::path merge(std::size_t first,std::size_t end) {
        const auto path=scratch->directory/std::to_string(serial++);
        std::vector<std::ifstream> inputs;
        using Entry=std::pair<double,std::size_t>;
        std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
        for(std::size_t i=first;i<end;++i) {
            inputs.emplace_back(runs[i],std::ios::binary);
            double value;
            if(!inputs.back().read(reinterpret_cast<char*>(&value),8))throw FrfSourceFailure(FrfError::ResourceLimit);
            queue.emplace(value,i-first);
        }
        std::ofstream out(path,std::ios::binary);
        std::size_t tick=0;
        while(!queue.empty()) {
            if((tick++ & 65535)==0)check(cancel);
            const auto entry=queue.top();queue.pop();
            out.write(reinterpret_cast<const char*>(&entry.first),8);
            double value;
            if(inputs[entry.second].read(reinterpret_cast<char*>(&value),8))queue.emplace(value,entry.second);
            else if(inputs[entry.second].bad() || !inputs[entry.second].eof() || inputs[entry.second].gcount()!=0)
                throw FrfSourceFailure(FrfError::ResourceLimit);
        }
        out.close();
        if(!out)throw FrfSourceFailure(FrfError::ResourceLimit);
        inputs.clear();
        for(std::size_t i=first;i<end;++i)std::filesystem::remove(runs[i]);
        return path;
    }
public:
    double maximum=0;
    explicit Cadence(const std::atomic<bool>* c,std::size_t expected=1048576):cancel(c) {
        pending.reserve(std::min<std::size_t>(expected,1048576));
    }
    void add(double d) { pending.push_back(d); ++count;maximum=std::max(maximum,d);if(pending.size()==1048576)flush(); }
    double finish() {
        if(runs.empty())return typical_sample_spacing(pending);
        flush();
        while(runs.size()>1) {
            std::vector<std::filesystem::path> next;
            for(std::size_t i=0;i<runs.size();i+=32)next.push_back(merge(i,std::min(runs.size(),i+32)));
            runs=std::move(next);
        }
        std::ifstream sorted(runs.front(),std::ios::binary);
        const auto at=[&](std::size_t index) {
            sorted.clear();sorted.seekg(static_cast<std::streamoff>(index*8));double value;
            sorted.read(reinterpret_cast<char*>(&value),8);
            if(!sorted)throw FrfSourceFailure(FrfError::ResourceLimit);
            return value;
        };
        // Same expressions, first separated cluster and median as sampling.hpp.
        std::size_t kept=count;bool tested=false;
        sorted.seekg(0);double previous=0,value=0;
        for(std::size_t i=0;i<count;++i) {
            if((i & 65535)==0)check(cancel);
            sorted.read(reinterpret_cast<char*>(&value),8);
            if(!sorted)throw FrfSourceFailure(FrfError::ResourceLimit);
            if(i>=2) {
                const double separation=value/previous;
                if(separation>4) { kept=i;break; }
                if(separation>sampling_gap_factor && !tested) {
                    tested=true;
                    const double candidate=i%2 ? at(i/2) : .5*at(i/2)+.5*at(i/2-1);
                    sorted.clear();sorted.seekg(0);bool multiples=true;
                    for(std::size_t j=0;j<count;++j) {
                        if((j & 65535)==0)check(cancel);
                        double interval;sorted.read(reinterpret_cast<char*>(&interval),8);
                        if(!sorted)throw FrfSourceFailure(FrfError::ResourceLimit);
                        const double ratio=interval/candidate;
                        if(!std::isfinite(ratio) || ratio<.95 || std::fabs(ratio-std::round(ratio))>.05) { multiples=false;break; }
                    }
                    if(multiples) {kept=i;break;}
                    sorted.clear();sorted.seekg(static_cast<std::streamoff>((i+1)*8));
                }
            }
            previous=value;
        }
        return kept%2 ? at(kept/2) : .5*at(kept/2)+.5*at(kept/2-1);
    }
};
}
struct PreparedFrf::Storage {
    Scratch scratch;
    std::filesystem::path path=scratch.directory/"samples.bin";
    std::size_t channels=0;
};
FrfSamples inspect_frf_timeline(const FrfSource& source,const std::atomic<bool>* cancel) {
    FrfSamples m;Cadence cadence(cancel);double previous=0;
    source.replay([&](const auto& time,const auto&) {
        for(std::size_t i=0;i<time.size();++i) {
            if((i & 4095)==0)check(cancel);
            const double t=time[i];
            if(!std::isfinite(t))m.error=FrfError::InvalidTime;
            if(m.source_count) {
                const double d=t-previous;
                if(!std::isfinite(d) || d<=0)m.error=FrfError::InvalidTime;
                else cadence.add(d);
            }else m.source_start=t;
            previous=t;m.source_end=t;++m.source_count;
        }
    },cancel);
    if(m.error!=FrfError::None)return m;
    if(m.source_count<4) {m.error=FrfError::TooShort;return m;}
    m.sample_dt=cadence.finish();
    if(!(m.sample_dt>0) || !std::isfinite(.5/m.sample_dt))m.error=FrfError::InvalidTime;
    m.gaps_ignored=cadence.maximum>m.sample_dt*sampling_gap_factor;
    return m;
}
void PreparedFrf::read(std::size_t first,std::size_t count,std::vector<std::vector<double>>& channels,
                       const std::atomic<bool>* cancel) const {
    check(cancel);
    if(first>metadata.source_count || count>metadata.source_count-first)
        throw std::out_of_range("FRF sample range exceeds the prepared recording.");
    channels.resize(response_errors.size()+1);
    for(auto& c:channels)c.resize(count);
    if(memory) {
        const auto copy=[&](const std::vector<double>& src,std::vector<double>& dst) {
            for(std::size_t at=0;first+count<=src.size() && at<count;at+=65536) {
                check(cancel);
                std::copy_n(src.begin()+static_cast<std::ptrdiff_t>(first+at),std::min<std::size_t>(65536,count-at),dst.begin()+at);
            }
        };
        copy(memory->references.front(),channels.front());
        for(std::size_t c=0;c<memory->responses.size();++c)copy(memory->responses[c],channels[c+1]);
    } else {
        if(!storage)throw FrfSourceFailure(FrfError::ResourceLimit);
        std::ifstream in(storage->path,std::ios::binary);
        in.seekg(static_cast<std::streamoff>(first*storage->channels*8));
        // Bounded interleaved I/O, no per-value system calls.
        std::vector<double> row_block(std::min<std::size_t>(count,4096)*storage->channels);
        for(std::size_t at=0;at<count;at+=4096) {
            check(cancel);
            const auto rows=std::min<std::size_t>(4096,count-at);
            in.read(reinterpret_cast<char*>(row_block.data()),rows*storage->channels*8);
            if(!in)throw FrfSourceFailure(FrfError::ResourceLimit);
            for(std::size_t i=0;i<rows;++i)for(std::size_t c=0;c<channels.size();++c)channels[c][at+i]=row_block[i*channels.size()+c];
        }
    }
}

std::shared_ptr<const PreparedFrf> prepare_frf_batch(FrfBatchInput input,const std::atomic<bool>* cancel) {
    auto prepared=std::make_shared<PreparedFrf>();auto& m=prepared->metadata;
    auto memory=std::make_shared<FrfBatchInput>(std::move(input));
    prepared->memory=memory;
    prepared->resident_bytes=memory->time.capacity()*8;
    for(const auto& c:memory->references)prepared->resident_bytes+=c.capacity()*8;
    for(const auto& c:memory->responses)prepared->resident_bytes+=c.capacity()*8;
    const auto n=memory->time.size();
    if(memory->references.empty() || memory->responses.empty()) { m.error=FrfError::InvalidChannels;return prepared; }
    for(const auto& ref:memory->references) {
        if(ref.size()!=n) {m.error=FrfError::InvalidChannels;return prepared;}
        for(std::size_t i=0;i<n;++i) {
            if((i & 65535)==0)check(cancel);
            if(!std::isfinite(ref[i])) {m.error=FrfError::MissingValues;return prepared;}
        }
    }
    if(memory->references.size()>1) {
        for(std::size_t i=0;i<n;++i) {
            if((i & 65535)==0)check(cancel);
            long double sum=0;
            for(const auto& ref:memory->references)sum+=static_cast<long double>(ref[i])/memory->references.size();
            memory->references.front()[i]=static_cast<double>(sum);
            if(!std::isfinite(memory->references.front()[i])) {m.error=FrfError::Overflow;return prepared;}
        }
        memory->references.resize(1);
    }
    if(n<4) {m.error=FrfError::TooShort;return prepared;}
    if(n>static_cast<std::size_t>(std::numeric_limits<int>::max()/2)) {m.error=FrfError::Overflow;return prepared;}
    Cadence cadence(cancel,n-1);
    for(std::size_t i=0;i<n;++i) {
        if((i & 65535)==0)check(cancel);
        if(!std::isfinite(memory->time[i])) {m.error=FrfError::InvalidTime;return prepared;}
        if(i) {
            const double d=memory->time[i]-memory->time[i-1];
            if(!std::isfinite(d) || d<=0) {m.error=FrfError::InvalidTime;return prepared;}
            cadence.add(d);
        }
    }
    m.sample_dt=cadence.finish();
    if(!(m.sample_dt>0) || !std::isfinite(.5/m.sample_dt)) {m.error=FrfError::InvalidTime;return prepared;}
    m.source_start=memory->time.front();m.source_end=memory->time.back();m.source_count=n;
    m.gaps_ignored=cadence.maximum>m.sample_dt*sampling_gap_factor;
    std::vector<double>().swap(memory->time);
    prepared->response_errors.resize(memory->responses.size(),FrfError::None);
    prepared->resident_bytes=memory->references.front().capacity()*8;
    for(std::size_t c=0;c<memory->responses.size();++c) {
        const auto& values=memory->responses[c];prepared->resident_bytes+=values.capacity()*8;
        if(values.size()!=n)prepared->response_errors[c]=FrfError::InvalidChannels;
        else for(std::size_t i=0;i<n;++i) {
            if((i & 65535)==0)check(cancel);
            if(!std::isfinite(values[i])) {prepared->response_errors[c]=FrfError::MissingValues;break;}
        }
    }
    return prepared;
}

std::shared_ptr<const PreparedFrf> prepare_frf_stream(const FrfSource& source,const std::atomic<bool>* cancel) {
    auto p=std::make_shared<PreparedFrf>();auto& m=p->metadata;
    if(!source.references || !source.responses || !source.replay) {m.error=FrfError::InvalidChannels;return p;}
    p->response_errors.resize(source.responses,FrfError::None);
    p->storage=std::make_shared<PreparedFrf::Storage>();p->storage->channels=1+source.responses;
    std::ofstream out(p->storage->path,std::ios::binary);
    Cadence cadence(cancel);double previous=0;
    FrfError reference_error=FrfError::None;
    source.replay([&](const auto& time,const auto& channels) {
        check(cancel);
        if(channels.size()!=source.references+source.responses)throw std::invalid_argument("Unaligned FRF block.");
        for(const auto& c:channels)if(c.size()!=time.size())throw std::invalid_argument("Unaligned FRF block.");
        std::vector<double> rows;rows.reserve(time.size()*p->storage->channels);
        for(std::size_t i=0;i<time.size();++i) {
            if((i & 4095)==0)check(cancel);
            const double t=time[i];
            if(!std::isfinite(t))m.error=FrfError::InvalidTime;
            if(m.source_count) {
                const double d=t-previous;
                if(!std::isfinite(d) || d<=0)m.error=FrfError::InvalidTime;
                else cadence.add(d);
            } else m.source_start=t;
            previous=t;m.source_end=t;++m.source_count;
            long double average=0;
            for(std::size_t c=0;c<source.references;++c) {
                if(!std::isfinite(channels[c][i]))reference_error=FrfError::MissingValues;
                average+=static_cast<long double>(channels[c][i])/source.references;
            }
            // A single reference must preserve its original bit pattern.
            const double ref=source.references==1 ? channels[0][i] : static_cast<double>(average);
            if(!std::isfinite(ref) && reference_error==FrfError::None)reference_error=FrfError::Overflow;
            rows.push_back(ref);
            for(std::size_t c=0;c<source.responses;++c) {
                const auto value=channels[c+source.references][i];
                if(!std::isfinite(value))p->response_errors[c]=FrfError::MissingValues;
                rows.push_back(value);
            }
        }
        if(std::filesystem::space(p->storage->scratch.directory).available<rows.size()*8+64ULL*1024*1024)
            throw FrfSourceFailure(FrfError::ResourceLimit);
        out.write(reinterpret_cast<const char*>(rows.data()),rows.size()*8);
        if(!out)throw FrfSourceFailure(FrfError::ResourceLimit);
        p->disk_bytes+=rows.size()*8;
    },cancel);
    out.close();
    if(!out)throw FrfSourceFailure(FrfError::ResourceLimit);
    if(reference_error!=FrfError::None) {m.error=reference_error;return p;}
    if(m.source_count<4) {m.error=FrfError::TooShort;return p;}
    if(m.source_count>static_cast<std::size_t>(std::numeric_limits<int>::max()/2)) {m.error=FrfError::Overflow;return p;}
    if(m.error!=FrfError::None)return p;
    m.sample_dt=cadence.finish();
    if(!(m.sample_dt>0) || !std::isfinite(.5/m.sample_dt))m.error=FrfError::InvalidTime;
    m.gaps_ignored=cadence.maximum>m.sample_dt*sampling_gap_factor;
    return p;
}
}
