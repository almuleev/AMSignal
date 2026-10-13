#include "recording_overview.hpp"
#include "frf_stream.hpp"
#include "sampling.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <mutex>
#include <numeric>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace lvm {
namespace {
constexpr std::size_t header_bytes=65536;
using Bytes=std::vector<unsigned char>;
[[noreturn]] void fail(RecordingError e,const char* text) {throw RecordingFailure(e,text);}
void check(const std::atomic<bool>* cancel) {
    if(cancel && cancel->load(std::memory_order_relaxed))fail(RecordingError::Cancelled,"Operation cancelled.");
}
std::uint64_t hash(const unsigned char* bytes,std::size_t n) {
    std::uint64_t h=14695981039346656037ULL;
    for(std::size_t i=0;i<n;++i) {h^=bytes[i];h*=1099511628211ULL;}
    return h;
}
struct Codec {
    Bytes& b;std::size_t at=0;
    void put(std::uint64_t value) {
        if(at>b.size() || b.size()-at<8)fail(RecordingError::ResourceLimit,"Overview metadata is too large.");
        for(int i=0;i<8;++i)b[at++]=static_cast<unsigned char>(value>>(8*i));
    }
    std::uint64_t get() {
        if(at>b.size() || b.size()-at<8)fail(RecordingError::ReadFailure,"Invalid overview index.");
        std::uint64_t value=0;for(int i=0;i<8;++i)value|=std::uint64_t(b[at++])<<(8*i);return value;
    }
    void put_double(double value) {std::uint64_t bits;std::memcpy(&bits,&value,8);put(bits);}
    double get_double() {const auto bits=get();double value;std::memcpy(&value,&bits,8);return value;}
};
std::size_t record_bytes(std::size_t channels) {
    if(channels>1024)fail(RecordingError::ResourceLimit,"Too many overview channels.");
    return 48+104*channels;
}
void seal(Bytes& b) {Codec c{b,b.size()-8};c.put(hash(b.data(),b.size()-8));}
void verify(Bytes& b) {
    Codec c{b,b.size()-8};
    if(c.get()!=hash(b.data(),b.size()-8))fail(RecordingError::ReadFailure,"Overview checksum mismatch.");
}
void read_at(std::istream& in,std::uint64_t offset,Bytes& b) {
    if(offset>static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
        fail(RecordingError::ReadFailure,"Invalid overview offset.");
    in.clear();in.seekg(static_cast<std::streamoff>(offset));
    in.read(reinterpret_cast<char*>(b.data()),static_cast<std::streamsize>(b.size()));
    if(!in)fail(RecordingError::ReadFailure,"Cannot read overview index.");
    verify(b);
}
void encode(const OverviewBucket& node,Bytes& b) {
    Codec c{b};c.put(node.first_sample);c.put(node.sample_count);
    c.put_double(node.first_time);c.put_double(node.last_time);c.put_double(node.maximum_spacing);
    for(const auto& t:node.channels) {
        c.put(t.finite_count);
        for(const auto* p:{&t.first,&t.last,&t.minimum,&t.maximum}) {
            c.put(p->sample);c.put_double(p->time);c.put_double(p->value);
        }
    }
    seal(b);
}
OverviewBucket decode(Bytes& b,std::size_t channels,std::uint64_t total) {
    Codec c{b};OverviewBucket node;
    node.first_sample=c.get();node.sample_count=c.get();node.first_time=c.get_double();
    node.last_time=c.get_double();node.maximum_spacing=c.get_double();
    if(!node.sample_count || node.first_sample>=total || node.sample_count>total-node.first_sample ||
       !std::isfinite(node.first_time) || !std::isfinite(node.last_time) || node.first_time>node.last_time ||
       !std::isfinite(node.maximum_spacing) || node.maximum_spacing<0)
        fail(RecordingError::ReadFailure,"Invalid overview bucket.");
    node.channels.resize(channels);
    for(auto& t:node.channels) {
        t.finite_count=c.get();
        if(t.finite_count>node.sample_count)fail(RecordingError::ReadFailure,"Invalid overview trace.");
        for(auto* p:{&t.first,&t.last,&t.minimum,&t.maximum}) {
            p->sample=c.get();p->time=c.get_double();p->value=c.get_double();
            if(t.finite_count && (p->sample<node.first_sample || p->sample-node.first_sample>=node.sample_count ||
                !std::isfinite(p->time) || p->time<node.first_time || p->time>node.last_time || !std::isfinite(p->value)))
                fail(RecordingError::ReadFailure,"Invalid overview extrema.");
        }
        if(t.finite_count && (t.minimum.value>t.maximum.value || t.first.sample>t.last.sample))
            fail(RecordingError::ReadFailure,"Invalid overview extrema order.");
    }
    return node;
}
void add(OverviewTrace& trace,OverviewPoint point) {
    if(!std::isfinite(point.value))return;
    if(!trace.finite_count)trace.first=trace.minimum=trace.maximum=point;
    if(point.value<trace.minimum.value)trace.minimum=point;
    if(point.value>trace.maximum.value)trace.maximum=point;
    trace.last=point;++trace.finite_count;
}
OverviewBucket merge(OverviewBucket left,const OverviewBucket& right) {
    if(left.first_sample+left.sample_count!=right.first_sample || left.last_time>=right.first_time)
        fail(RecordingError::ReadFailure,"Noncontiguous overview buckets.");
    left.maximum_spacing=std::max({left.maximum_spacing,right.maximum_spacing,right.first_time-left.last_time});
    left.sample_count+=right.sample_count;left.last_time=right.last_time;
    for(std::size_t i=0;i<left.channels.size();++i) {
        auto& a=left.channels[i];const auto& b=right.channels[i];
        if(!b.finite_count)continue;
        if(!a.finite_count) {a=b;continue;}
        a.last=b.last;a.finite_count+=b.finite_count;
        if(b.minimum.value<a.minimum.value)a.minimum=b.minimum;
        if(b.maximum.value>a.maximum.value)a.maximum=b.maximum;
    }
    return left;
}
std::shared_ptr<MemoryBudget::Reservation> admit(std::size_t bytes,const std::shared_ptr<MemoryBudget>& budget) {
    if(!budget)return {};
    auto guard=budget->reserve(bytes);
    if(!guard)fail(RecordingError::ResourceLimit,"Overview exceeds the data budget.");
    return guard;
}
std::size_t work_bytes(std::size_t memory,std::size_t channels) {
    // A traversal can retain a node and its sibling at each of at most 64 levels.
    const auto fixed=header_bytes+136*record_bytes(channels)+65536;
    if(memory<=fixed+16*(channels+1))fail(RecordingError::ResourceLimit,"Overview buffers exceed the memory allowance.");
    return (memory-fixed)/2;
}
struct Scratch {
    std::filesystem::path directory,file;
    explicit Scratch(const std::filesystem::path& parent) {
        static std::atomic<std::uint64_t> serial{0};
        for(int attempt=0;attempt<100;++attempt) {
            directory=parent/("ams-overview-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+
                              "-"+std::to_string(serial.fetch_add(1)));
            if(std::filesystem::create_directory(directory)) {file=directory/"index.bin";return;}
        }
        fail(RecordingError::ResourceLimit,"Cannot create overview scratch directory.");
    }
    ~Scratch() {std::error_code ec;std::filesystem::remove_all(directory,ec);}
};
void publish(const std::filesystem::path& from,const std::filesystem::path& to) {
#ifdef _WIN32
    if(!MoveFileExW(from.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        fail(RecordingError::ResourceLimit,"Cannot publish overview cache.");
#else
    std::error_code ec;std::filesystem::rename(from,to,ec);
    if(ec)fail(RecordingError::ResourceLimit,"Cannot publish overview cache.");
#endif
}
} // namespace

struct RecordingOverview::Storage {
    std::ifstream in;
    std::mutex mutex;
    std::mutex map_mutex;
    std::array<double,1025> raw{},shown{};
    std::size_t mapped=0;
    std::uint64_t mapped_first=0;
    explicit Storage(const std::filesystem::path& path):in(path,std::ios::binary) {
        if(!in)fail(RecordingError::ReadFailure,"Cannot open overview cache.");
    }
};
std::pair<std::array<OverviewPoint,4>,std::size_t> OverviewTrace::ordered_points() const {
    std::array<OverviewPoint,4> points{first,minimum,maximum,last};
    if(!finite_count)return {points,0};
    std::sort(points.begin(),points.end(),[](const auto& a,const auto& b){return a.sample<b.sample;});
    const auto last_point=std::unique(points.begin(),points.end(),[](const auto& a,const auto& b){return a.sample==b.sample;});
    return {points,static_cast<std::size_t>(last_point-points.begin())};
}
std::size_t OverviewView::resident_bytes() const {
    std::size_t bytes=sizeof(*this)+buckets.capacity()*sizeof(OverviewBucket);
    for(const auto& b:buckets)bytes+=b.channels.capacity()*sizeof(OverviewTrace);
    return bytes;
}
OverviewView& OverviewView::operator=(OverviewView&& other) noexcept {
    if(this!=&other) {
        // The previous guard must outlive the previous buffers on assignment too.
        OverviewView previous(std::move(*this));
        memory_reservation=std::move(other.memory_reservation);range=other.range;
        buckets=std::move(other.buckets);sample_count=other.sample_count;
        pixel_columns=other.pixel_columns;other.pixel_columns=0;
        other.sample_count=0;
    }
    return *this;
}

std::shared_ptr<const RecordingOverview> RecordingOverview::build(
    std::shared_ptr<const RecordingSource> source,const std::filesystem::path& cache,
    const OverviewOptions& options,const std::atomic<bool>* cancel) {
    try {
        check(cancel);
        if(!source || !source->file_backed() || cache.empty() || !options.base_rows || options.base_rows>65536)
            fail(RecordingError::InvalidRequest,"Invalid file overview request.");
        source->check_current();
        const auto target=std::filesystem::absolute(cache).lexically_normal();
        std::error_code ec;
        if(target==source->path().lexically_normal() ||
           (std::filesystem::exists(target) && std::filesystem::equivalent(target,source->path(),ec)) ||
           std::filesystem::is_symlink(std::filesystem::symlink_status(target)))
            fail(RecordingError::InvalidRequest,"Overview cache must not overwrite a recording or a symlink.");
        const auto channels=source->channel_count(),size=record_bytes(channels);
        const auto block_bytes=work_bytes(options.memory_bytes,channels);
        const auto timeline_size=options.base_rows*8+24;
        if(timeline_size*2>block_bytes)fail(RecordingError::ResourceLimit,"Timeline buffers exceed the overview allowance.");
        if(options.max_disk_bytes<header_bytes+3*size ||
           options.max_disk_bytes>static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()))
            fail(RecordingError::ResourceLimit,"Invalid overview disk allowance.");
        auto reservation=admit(options.memory_bytes,options.budget);
        if(std::filesystem::space(target.parent_path()).available<header_bytes+3*size)
            fail(RecordingError::ResourceLimit,"Not enough space for overview cache.");
        Scratch scratch(target.parent_path());
        std::ofstream out(scratch.file,std::ios::binary);Bytes header(header_bytes,0),buffer(size);
        out.write(reinterpret_cast<const char*>(header.data()),header.size());
        std::uint64_t written=header_bytes,total=0;
        std::vector<Level> levels{{written,0}};
        const auto append=[&](const OverviewBucket& node) {
            check(cancel);
            if(size>options.max_disk_bytes-written)fail(RecordingError::ResourceLimit,"Overview exceeds the disk allowance.");
            encode(node,buffer);out.write(reinterpret_cast<const char*>(buffer.data()),buffer.size());
            if(!out)fail(RecordingError::ResourceLimit,"Cannot write overview cache.");
            written+=size;
        };
        OverviewBucket node;node.channels.resize(channels);double previous=0;
        Bytes timeline(timeline_size);Codec times{timeline};
        const auto append_leaf=[&] {
            append(node);
            Bytes payload(node.sample_count*8+24);
            std::copy_n(timeline.begin(),node.sample_count*8,payload.begin());
            seal(payload);
            if(payload.size()>options.max_disk_bytes-written)fail(RecordingError::ResourceLimit,"Timeline exceeds the disk allowance.");
            out.write(reinterpret_cast<const char*>(payload.data()),payload.size());
            if(!out)fail(RecordingError::ResourceLimit,"Cannot write overview timeline.");
            written+=payload.size();++levels[0].count;times.at=0;
            node=OverviewBucket{};node.channels.resize(channels);
        };
        RecordingReadRequest read;read.block_bytes=block_bytes;
        read.channels.resize(channels);std::iota(read.channels.begin(),read.channels.end(),std::size_t{0});
        source->replay(read,[&](const auto& time,const auto& values) {
            for(std::size_t i=0;i<time.size();++i) {
                if((i & 4095)==0)check(cancel);
                if(!std::isfinite(time[i]) || (total && (time[i]<=previous || !std::isfinite(time[i]-previous))) ||
                   total==std::numeric_limits<std::uint64_t>::max())
                    fail(RecordingError::ReadFailure,"Overview needs strictly increasing finite time.");
                if(!node.sample_count) {node.first_sample=total;node.first_time=time[i];}
                else node.maximum_spacing=std::max(node.maximum_spacing,time[i]-previous);
                node.last_time=time[i];++node.sample_count;
                times.put_double(time[i]);
                for(std::size_t c=0;c<channels;++c)add(node.channels[c],{total,time[i],values[c][i]});
                previous=time[i];++total;
                if(node.sample_count==options.base_rows) {
                    append_leaf();
                }
            }
            if(options.progress)options.progress(total,written);
        },cancel);
        if(node.sample_count)append_leaf();
        // Compute both established cadence rules from the binary timeline,
        // never by parsing the text recording again. One bounded external sort.
        out.flush();if(!out)fail(RecordingError::ResourceLimit,"Cannot flush overview timeline.");
        std::ifstream timeline_in(scratch.file,std::ios::binary);
        const auto leaf_stride=size+timeline_size;
        std::vector<double> sampled;sampled.reserve(4096);
        const auto stride=std::max<std::uint64_t>(1,(total-1)/4096);
        std::vector<double> time_block;time_block.reserve(options.base_rows);
        const auto replay_times=[&](const FrfBlockConsumer& consume,const std::atomic<bool>* flag) {
            std::uint64_t row=0;double previous_time=0;
            for(std::uint64_t leaf=0;leaf<levels[0].count;++leaf) {
                check(flag);const auto count=std::min<std::uint64_t>(options.base_rows,total-row);
                Bytes payload(count*8+24);read_at(timeline_in,header_bytes+leaf*leaf_stride+size,payload);
                Codec codec{payload};time_block.clear();
                for(std::uint64_t i=0;i<count;++i,++row) {
                    const auto t=codec.get_double();time_block.push_back(t);
                    if(row && (row-1)%stride==0 && sampled.size()<4096)sampled.push_back(t-previous_time);
                    previous_time=t;
                }
                consume(time_block,{});
            }
        };
        if(8*std::min<std::uint64_t>(total,1048576)+timeline_size*2+header_bytes>options.memory_bytes)
            fail(RecordingError::ResourceLimit,"Cadence exceeds the overview memory allowance.");
        FrfSource cadence_source;cadence_source.replay=replay_times;
        double sample_step=0;
        if(total>=4)sample_step=inspect_frf_timeline(cadence_source,cancel,static_cast<std::size_t>(total)).sample_dt;
        else {std::vector<double> intervals;double prev=0;bool have=false;
            replay_times([&](const auto& ts,const auto&){for(auto t:ts){if(have)intervals.push_back(t-prev);prev=t;have=true;}},cancel);
            sample_step=typical_sample_spacing(intervals);}
        std::sort(sampled.begin(),sampled.end());const auto mid=sampled.size()/2;
        const double gap_step=sampled.empty() ? 0 : sampled.size()%2 ? sampled[mid] : .5*(sampled[mid-1]+sampled[mid]);
        // Store one prefix per leaf. Raw timestamps remain exact; no full RAM map.
        double previous_raw=0,anchor_raw=0,anchor_display=0;bool have=false;
        for(std::uint64_t leaf=0,row=0;leaf<levels[0].count;++leaf) {
            check(cancel);const auto count=std::min<std::uint64_t>(options.base_rows,total-row);row+=count;
            Bytes payload(count*8+24);const auto offset=header_bytes+leaf*leaf_stride+size;
            read_at(timeline_in,offset,payload);Codec codec{payload};double first_anchor=0,first_display=0;
            for(std::uint64_t i=0;i<count;++i) {
                const auto t=codec.get_double();
                if(!have){anchor_raw=t;anchor_display=t;}
                else if(sample_step>0 && t-previous_raw>sample_step*sampling_gap_factor) {
                    anchor_display+=(previous_raw-anchor_raw)+sample_step;anchor_raw=t;
                }
                previous_raw=t;have=true;if(!i){first_anchor=anchor_raw;first_display=anchor_display;}
            }
            Codec prefix{payload,static_cast<std::size_t>(count*8)};
            prefix.put_double(first_anchor);prefix.put_double(first_display);seal(payload);
            out.seekp(static_cast<std::streamoff>(offset));out.write(reinterpret_cast<const char*>(payload.data()),payload.size());
            if(!out)fail(RecordingError::ResourceLimit,"Cannot write stitched timeline.");
        }
        timeline_in.close();out.seekp(static_cast<std::streamoff>(written));
        while(levels.back().count>1) {
            out.flush();if(!out)fail(RecordingError::ResourceLimit,"Cannot flush overview cache.");
            std::ifstream in(scratch.file,std::ios::binary);
            const auto previous_level=levels.back();Level next{written,0};
            for(std::uint64_t i=0;i<previous_level.count;i+=2) {
                const auto stride_bytes=levels.size()==1 ? leaf_stride : size;
                check(cancel);read_at(in,previous_level.offset+i*stride_bytes,buffer);
                auto combined=decode(buffer,channels,total);
                if(i+1<previous_level.count) {
                    read_at(in,previous_level.offset+(i+1)*stride_bytes,buffer);
                    combined=merge(std::move(combined),decode(buffer,channels,total));
                }
                append(combined);++next.count;
            }
            levels.push_back(next);
            if(options.progress)options.progress(total,written);
            check(cancel);source->check_current();
        }
        Codec h{header};
        h.put(0x325844494F534D41ULL); // AMSOIDX2: exact timeline and stitched prefixes.
        h.put(options.base_rows);h.put(channels);h.put(total);
        h.put_double(source->bounds().start);h.put_double(source->bounds().end);
        h.put(source->stamp()->bytes);h.put(static_cast<std::uint64_t>(source->stamp()->modified.time_since_epoch().count()));
        h.put(std::filesystem::file_time_type::duration::period::num);h.put(std::filesystem::file_time_type::duration::period::den);
        const auto path=source->cache_identity();h.put(path.size());
        if(path.size()>32768)fail(RecordingError::ResourceLimit,"Overview source path is too long.");
        for(unsigned char ch:path)header.at(h.at++)=ch;
        for(auto col:source->physical_columns())h.put(col);
        h.put(levels.size());for(const auto& level:levels) {h.put(level.offset);h.put(level.count);}
        h.put_double(gap_step);h.put_double(sample_step);
        if(h.at>header.size()-8)fail(RecordingError::ResourceLimit,"Overview header is too large.");
        seal(header);out.seekp(0);out.write(reinterpret_cast<const char*>(header.data()),header.size());out.close();
        if(!out)fail(RecordingError::ResourceLimit,"Cannot close overview cache.");
        check(cancel);source->check_current();publish(scratch.file,target);
        source->check_current();auto result=open(std::move(source),target);check(cancel);return result;
    }catch(const FrfSourceFailure&) {check(cancel);fail(RecordingError::ResourceLimit,"Cannot prepare cached timeline cadence. Check temporary disk space.");}
    catch(const RecordingFailure&) {throw;}
    catch(const std::bad_alloc&) {fail(RecordingError::ResourceLimit,"Not enough memory for overview.");}
    catch(const std::filesystem::filesystem_error&) {fail(RecordingError::ResourceLimit,"Cannot access overview scratch disk.");}
    catch(const std::runtime_error&) {check(cancel);throw;}
}

std::shared_ptr<const RecordingOverview> RecordingOverview::open(
    std::shared_ptr<const RecordingSource> source,const std::filesystem::path& cache) {
    if(!source || !source->file_backed())fail(RecordingError::InvalidRequest,"Overview cache needs a file source.");
    source->check_current();
    auto result=std::shared_ptr<RecordingOverview>(new RecordingOverview);
    result->source_=std::move(source);result->storage_=std::make_shared<Storage>(cache);
    Bytes header(header_bytes);read_at(result->storage_->in,0,header);Codec h{header};
    if(h.get()!=0x325844494F534D41ULL)fail(RecordingError::ReadFailure,"Unknown overview format.");
    result->base_rows_=static_cast<std::size_t>(h.get());const auto channels=h.get();result->sample_count_=h.get();
    const auto first=h.get_double(),last=h.get_double();const auto bytes=h.get(),modified=h.get(),num=h.get(),den=h.get();
    const auto& s=*result->source_;
    if(!result->base_rows_ || result->base_rows_>65536 || channels!=s.channel_count() || !result->sample_count_ ||
       first!=s.bounds().start || last!=s.bounds().end || bytes!=s.stamp()->bytes ||
       modified!=static_cast<std::uint64_t>(s.stamp()->modified.time_since_epoch().count()) ||
       num!=std::filesystem::file_time_type::duration::period::num || den!=std::filesystem::file_time_type::duration::period::den)
        fail(RecordingError::ReadFailure,"Overview does not match the source.");
    const auto path_size=h.get();const auto path=s.cache_identity();
    if(path_size!=path.size() || path_size>32768 || h.at+path_size>header.size()-8 ||
       std::memcmp(path.data(),header.data()+h.at,path.size())!=0)
        fail(RecordingError::ReadFailure,"Overview source path mismatch.");
    h.at+=static_cast<std::size_t>(path_size);
    for(auto col:s.physical_columns())if(h.get()!=col)fail(RecordingError::ReadFailure,"Overview channel mapping mismatch.");
    const auto count=h.get();if(!count || count>64)fail(RecordingError::ReadFailure,"Invalid overview levels.");
    const auto size=record_bytes(s.channel_count());
    std::uint64_t offset=header_bytes,expected=(result->sample_count_-1)/result->base_rows_+1;
    for(std::uint64_t i=0;i<count;++i) {
        const auto position=h.get(),nodes=h.get();
        if(position!=offset || nodes!=expected || nodes>(std::uint64_t(std::numeric_limits<std::streamoff>::max())-offset)/size ||
           (i+1<count && nodes==1))fail(RecordingError::ReadFailure,"Invalid overview layout.");
        result->levels_.push_back({position,nodes});offset+=nodes*size;
        if(!i) {
            if(result->sample_count_>(std::uint64_t(std::numeric_limits<std::streamoff>::max())-offset-nodes*24)/8)
                fail(RecordingError::ReadFailure,"Invalid timeline size.");
            offset+=result->sample_count_*8+nodes*24;
        }
        expected=(nodes-1)/2+1;
    }
    result->gap_step_=h.get_double();result->sample_step_=h.get_double();
    if(!std::isfinite(result->gap_step_) || result->gap_step_<0 || !std::isfinite(result->sample_step_) || result->sample_step_<0)
        fail(RecordingError::ReadFailure,"Invalid cached cadence.");
    result->storage_->in.seekg(0,std::ios::end);
    const auto length=result->storage_->in.tellg();
    if(length<0 || static_cast<std::uint64_t>(length)!=offset || result->levels_.back().count!=1)
        fail(RecordingError::ReadFailure,"Truncated or oversized overview cache.");
    result->disk_bytes_=offset;
    const auto root=result->read_bucket(result->levels_.size()-1,0);
    if(root.first_sample!=0 || root.sample_count!=result->sample_count_ || root.first_time<first || root.last_time>last)
        fail(RecordingError::ReadFailure,"Invalid overview root.");
    s.check_current();return result;
}
std::shared_ptr<const RecordingOverview> RecordingOverview::open_or_build(
    std::shared_ptr<const RecordingSource> source,const std::filesystem::path& cache,
    const OverviewOptions& options,const std::atomic<bool>* cancel) {
    check(cancel);
    try {
        auto result=open(source,cache);check(cancel);
        if(result->disk_bytes()>options.max_disk_bytes)fail(RecordingError::ResourceLimit,"Cached overview exceeds the disk allowance.");
        return result;
    }
    catch(const RecordingFailure& e) {if(e.error!=RecordingError::ReadFailure)throw;}
    return build(std::move(source),cache,options,cancel);
}
OverviewBucket RecordingOverview::read_bucket(std::size_t level,std::uint64_t index) const {
    if(level>=levels_.size() || index>=levels_[level].count)fail(RecordingError::ReadFailure,"Invalid overview node address.");
    Bytes buffer(record_bytes(source_->channel_count()));
    const auto stride=buffer.size()+(level==0 ? base_rows_*8+24 : 0);
    {std::lock_guard<std::mutex> lock(storage_->mutex);read_at(storage_->in,levels_[level].offset+index*stride,buffer);}
    return decode(buffer,source_->channel_count(),sample_count_);
}

std::vector<double> RecordingOverview::read_times(std::uint64_t leaf,double* display_first,double* anchor_raw,double* anchor_display) const {
    if(leaf>=levels_[0].count)fail(RecordingError::ReadFailure,"Invalid timeline leaf.");
    const auto count=std::min<std::uint64_t>(base_rows_,sample_count_-leaf*base_rows_);
    Bytes buffer(count*8+24);const auto size=record_bytes(source_->channel_count());
    {std::lock_guard<std::mutex> lock(storage_->mutex);
        read_at(storage_->in,levels_[0].offset+leaf*(size+base_rows_*8+24)+size,buffer);}
    Codec codec{buffer};std::vector<double> time;time.reserve(count);
    for(std::uint64_t i=0;i<count;++i)time.push_back(codec.get_double());
    const auto raw=codec.get_double(),shown=codec.get_double();
    const auto display=raw==shown ? time.front() : shown+(time.front()-raw);
    if(display_first)*display_first=display;
    if(anchor_raw)*anchor_raw=raw;
    if(anchor_display)*anchor_display=shown;
    if(!std::isfinite(display) || std::any_of(time.begin(),time.end(),[](double t){return !std::isfinite(t);}) ||
       std::adjacent_find(time.begin(),time.end(),std::greater_equal<double>())!=time.end())
        fail(RecordingError::ReadFailure,"Invalid cached timeline.");
    return time;
}

void RecordingOverview::cache_stitched_leaf(std::uint64_t leaf) const {
    double display=0,a=0,d=0;const auto times=read_times(leaf,&display,&a,&d);
    storage_->mapped=times.size();storage_->mapped_first=leaf*base_rows_;
    for(std::size_t i=0;i<times.size();++i) {
        if(i && sample_step_>0 && times[i]-times[i-1]>sample_step_*sampling_gap_factor){d+=(times[i-1]-a)+sample_step_;a=times[i];}
        storage_->raw[i]=times[i];storage_->shown[i]=a==d ? times[i] : d+(times[i]-a);
    }
    if(leaf+1<levels_[0].count){double next=0;const auto t=read_times(leaf+1,&next);
        storage_->raw[storage_->mapped]=t.front();storage_->shown[storage_->mapped++]=next;}
}
double RecordingOverview::stitched_sample_time(std::uint64_t sample) const {
    if(sample>=sample_count_)fail(RecordingError::InvalidRequest,"Invalid stitched sample address.");
    if(base_rows_>1024){const auto times=read_times(sample/base_rows_);return stitched_time(times[sample%base_rows_]);}
    std::lock_guard<std::mutex> lock(storage_->map_mutex);
    if(!storage_->mapped || sample<storage_->mapped_first || sample-storage_->mapped_first>=storage_->mapped)
        cache_stitched_leaf(sample/base_rows_);
    return storage_->shown[sample-storage_->mapped_first];
}
double RecordingOverview::stitched_time(double time,bool inverse) const {
    std::lock_guard<std::mutex> map_lock(storage_->map_mutex);
    const auto cached=[&](double& result) {
        const auto& from=inverse ? storage_->shown : storage_->raw;
        const auto& to=inverse ? storage_->raw : storage_->shown;
        if(!storage_->mapped || time<from[0] || time>from[storage_->mapped-1])return false;
        const auto right=std::lower_bound(from.begin(),from.begin()+storage_->mapped,time)-from.begin();
        if(!right || time==from[right])result=to[right];
        else result=to[right-1]+(time-from[right-1])/(from[right]-from[right-1])*(to[right]-to[right-1]);
        return true;
    };
    double cached_result=0;if(cached(cached_result))return cached_result;
    // Binary search leaves, then at most base_rows original timestamps.
    // No text parsing, timestamp array or gap list proportional to N in RAM.
    const auto display_leaf=[&](std::uint64_t leaf) {double first=0;read_times(leaf,&first);return first;};
    std::uint64_t lo=0,hi=levels_[0].count;
    while(lo<hi) {
        const auto mid=lo+(hi-lo)/2;
        const auto first=inverse ? display_leaf(mid) : read_bucket(0,mid).first_time;
        if(first<=time)lo=mid+1;else hi=mid;
    }
    const auto leaf=lo ? lo-1 : 0;double display=0;
    if(base_rows_<=1024){cache_stitched_leaf(leaf);if(cached(cached_result))return cached_result;}
    double anchor_raw=0,anchor_display=0;
    const auto times=read_times(leaf,&display,&anchor_raw,&anchor_display);double previous_raw=times.front(),previous_display=display;
    if(time<=(inverse ? display : previous_raw))return inverse ? previous_raw : display;
    const auto convert=[&](double raw,double shown) {
        const auto left=inverse ? previous_display : previous_raw,right=inverse ? shown : raw;
        const auto fraction=right>left ? std::clamp((time-left)/(right-left),0.0,1.0) : 0;
        return inverse ? previous_raw+fraction*(raw-previous_raw) : previous_display+fraction*(shown-previous_display);
    };
    for(std::size_t i=1;i<times.size();++i) {
        const auto dt=times[i]-previous_raw;
        if(sample_step_>0 && dt>sample_step_*sampling_gap_factor){anchor_display+=(previous_raw-anchor_raw)+sample_step_;anchor_raw=times[i];}
        display=anchor_raw==anchor_display ? times[i] : anchor_display+(times[i]-anchor_raw);
        if(time<=(inverse ? display : times[i]))return convert(times[i],display);
        previous_raw=times[i];previous_display=display;
    }
    if(leaf+1<levels_[0].count) {
        double next_display=0;const auto next=read_times(leaf+1,&next_display);
        return convert(next.front(),next_display);
    }
    return inverse ? previous_raw : previous_display;
}
std::size_t RecordingOverview::resident_bytes() const {
    return sizeof(*this)+sizeof(Storage)+8192+levels_.capacity()*sizeof(Level);
}

std::pair<std::uint64_t,std::uint64_t> RecordingOverview::sample_range(AnalysisRange requested) const {
    source_->check_current();TimeRange range;
    if(!requested.resolve(source_->bounds(),range))fail(RecordingError::InvalidRequest,"Invalid timeline range.");
    const auto bound=[&](double t,bool upper) {
        std::uint64_t lo=0,hi=levels_[0].count;
        while(lo<hi){const auto mid=lo+(hi-lo)/2;const auto last=read_bucket(0,mid).last_time;
            if(last<t || (upper && last==t))lo=mid+1;else hi=mid;}
        if(lo==levels_[0].count)return sample_count_;
        const auto times=read_times(lo);
        const auto at=upper ? std::upper_bound(times.begin(),times.end(),t) : std::lower_bound(times.begin(),times.end(),t);
        return lo*base_rows_+static_cast<std::uint64_t>(at-times.begin());
    };
    const auto result=std::make_pair(bound(range.start,false),bound(range.end,true));
    source_->check_current();return result;
}
void RecordingOverview::replay_timeline(AnalysisRange range,const std::function<void(const std::vector<double>&)>& consume,
                                       const std::atomic<bool>* cancel) const {
    check(cancel);if(!consume)fail(RecordingError::InvalidRequest,"Missing timeline consumer.");
    const auto rows=sample_range(range);
    for(auto row=rows.first;row<rows.second;) {
        check(cancel);auto times=read_times(row/base_rows_);
        const auto offset=static_cast<std::size_t>(row%base_rows_);
        const auto count=std::min<std::uint64_t>(times.size()-offset,rows.second-row);
        if(offset)times.erase(times.begin(),times.begin()+offset);
        times.resize(count);
        consume(times);row+=count;
    }
    check(cancel);source_->check_current();
}

OverviewView RecordingOverview::read_view(const OverviewRequest& request,const std::atomic<bool>* cancel) const {
    check(cancel);source_->check_current();OverviewView view;
    if(!request.range.resolve(source_->bounds(),view.range) || !request.pixel_columns ||
       std::any_of(request.channels.begin(),request.channels.end(),[&](auto c){return c>=source_->channel_count();}))
        fail(RecordingError::InvalidRequest,"Invalid overview viewport.");
    // Selected channels may contain aliases. Keep space for the selected raw
    // group and the incoming node in addition to source traversal/reader buffers.
    if(request.channels.size()>request.memory_bytes/(2*sizeof(OverviewTrace)))
        fail(RecordingError::ResourceLimit,"Requested overview channels exceed the memory allowance.");
    const auto selected_work=2*request.channels.size()*sizeof(OverviewTrace);
    const auto block_bytes=work_bytes(request.memory_bytes-selected_work,source_->channel_count());
    if(block_bytes<sizeof(OverviewBucket) ||
       request.channels.size()>(block_bytes-sizeof(OverviewBucket))/sizeof(OverviewTrace))
        fail(RecordingError::ResourceLimit,"Requested overview channels exceed the memory allowance.");
    view.memory_reservation=admit(request.memory_bytes,request.budget);
    try {
        // Reserve the bounded result once: vector growth cannot exceed the allowance.
        const auto cost=sizeof(OverviewBucket)+request.channels.size()*sizeof(OverviewTrace);
        const auto limit=block_bytes/cost;
        view.pixel_columns=request.display_density ?
            std::min(request.pixel_columns,limit>4 ? (limit-2)/2 : std::size_t(1)) : request.pixel_columns;
        const double width=(view.range.end-view.range.start)/view.pixel_columns;
        view.buckets.reserve(limit);
        const auto can_merge=[&](const OverviewBucket& node) {
            return request.display_density && !view.buckets.empty() &&
                view.buckets.back().first_sample+view.buckets.back().sample_count==node.first_sample &&
                node.last_time-view.buckets.back().first_time<=width;
        };
        const auto append=[&](OverviewBucket node) {
            const auto count=node.sample_count;
            if(can_merge(node))view.buckets.back()=merge(std::move(view.buckets.back()),node);
            else {
                if(view.buckets.size()==limit)fail(RecordingError::ResourceLimit,"Viewport is too detailed for its memory allowance.");
                view.buckets.push_back(std::move(node));
            }
            view.sample_count+=count;
        };
        const auto select=[&](OverviewBucket node) {
            if(view.buckets.size()==limit && !can_merge(node))fail(RecordingError::ResourceLimit,"Viewport is too detailed for its memory allowance.");
            std::vector<OverviewTrace> traces;traces.reserve(request.channels.size());
            for(auto c:request.channels)traces.push_back(node.channels[c]);
            node.channels=std::move(traces);return node;
        };
        const auto read_raw=[&](const OverviewBucket& node) {
            RecordingReadRequest read;read.range=AnalysisRange::selected(node.first_time,node.last_time);
            read.channels=request.channels;read.block_bytes=block_bytes;
            std::uint64_t seen=0;
            OverviewBucket group;
            source_->replay(read,[&](const auto& time,const auto& values) {
                for(std::size_t i=0;i<time.size();++i,++seen) {
                    if((i & 4095)==0)check(cancel);
                    if(time[i]<view.range.start || time[i]>view.range.end)continue;
                    if(request.display_density) {
                        if(group.sample_count && time[i]-group.first_time>width) {
                            append(std::move(group));group=OverviewBucket{};
                        }
                        if(!group.sample_count) {
                            group.first_sample=node.first_sample+seen;group.first_time=time[i];
                            group.channels.resize(request.channels.size());
                        } else group.maximum_spacing=std::max(group.maximum_spacing,time[i]-group.last_time);
                        group.last_time=time[i];++group.sample_count;
                        for(std::size_t c=0;c<values.size();++c)add(group.channels[c],{node.first_sample+seen,time[i],values[c][i]});
                        continue;
                    }
                    if(view.buckets.size()==limit)fail(RecordingError::ResourceLimit,"Viewport is too detailed for its memory allowance.");
                    OverviewBucket point;point.first_sample=node.first_sample+seen;point.sample_count=1;
                    point.first_time=point.last_time=time[i];point.channels.resize(request.channels.size());
                    for(std::size_t c=0;c<values.size();++c)add(point.channels[c],{point.first_sample,time[i],values[c][i]});
                    append(std::move(point));
                }
            },cancel);
            if(group.sample_count)append(std::move(group));
            if(seen!=node.sample_count)fail(RecordingError::ReadFailure,"Overview sample layout changed.");
        };
        // Adjacent detailed leaves share one reader. Restarting at a ScanIndex
        // checkpoint for every 1024 rows repeatedly parses the same prefix.
        // Only the span is retained; sample buffers still obey block_bytes.
        OverviewBucket pending_raw;
        const auto flush_raw=[&] {
            if(pending_raw.sample_count){read_raw(pending_raw);pending_raw=OverviewBucket{};}
        };
        const auto raw=[&](const OverviewBucket& node) {
            if(pending_raw.sample_count && (pending_raw.first_sample+pending_raw.sample_count!=node.first_sample ||
               pending_raw.sample_count+node.sample_count>65536))flush_raw();
            if(!pending_raw.sample_count){pending_raw.first_sample=node.first_sample;pending_raw.first_time=node.first_time;}
            pending_raw.sample_count+=node.sample_count;pending_raw.last_time=node.last_time;
        };
        const auto visit=[&](auto&& self,std::size_t level,std::uint64_t index)->void {
            check(cancel);auto node=read_bucket(level,index);
            if(node.last_time<view.range.start || node.first_time>view.range.end)return;
            const bool contained=node.first_time>=view.range.start && node.last_time<=view.range.end;
            if(contained && (node.last_time-node.first_time<=width || node.sample_count==1)) {
                // Small cached leaves interspersed with detailed leaves join
                // the same bounded read, avoiding repeated checkpoint prefixes.
                if(pending_raw.sample_count && pending_raw.first_sample+pending_raw.sample_count==node.first_sample &&
                   node.sample_count<=base_rows_*4 && pending_raw.sample_count+node.sample_count<=65536)raw(node);
                else {flush_raw();append(select(std::move(node)));}
            }
            else if(!level)raw(node);
            else {
                self(self,level-1,index*2);
                if(index*2+1<levels_[level-1].count)self(self,level-1,index*2+1);
            }
        };
        visit(visit,levels_.size()-1,0);
        flush_raw();
        check(cancel);source_->check_current();
    }catch(const std::bad_alloc&) {fail(RecordingError::ResourceLimit,"Not enough memory for overview viewport.");}
    if(view.memory_reservation && !view.memory_reservation->shrink_to(view.resident_bytes()))
        fail(RecordingError::ResourceLimit,"Overview result exceeded its reservation.");
    return view;
}

RecordingSnapResult RecordingOverview::nearest_sample(const RecordingSnapRequest& request,
                                                     const std::atomic<bool>* cancel) const {
    check(cancel);source_->check_current();TimeRange range;
    if(!request.range.resolve(source_->bounds(),range) || !std::isfinite(request.target_time) ||
       !std::isfinite(request.target_value) || !std::isfinite(request.time_scale) || request.time_scale<=0 ||
       !std::isfinite(request.value_scale) || request.value_scale<=0)
        fail(RecordingError::InvalidRequest,"Invalid original sample query.");
    for(const auto& c:request.channels)if(c.channel>=source_->channel_count() ||
        (c.affine && (!std::isfinite(c.multiplier) || !std::isfinite(c.offset))))
        fail(RecordingError::InvalidRequest,"Invalid original sample channel.");
    if(request.channels.size()>request.memory_bytes/(sizeof(RecordingSnapChannel)+sizeof(std::size_t)))
        fail(RecordingError::ResourceLimit,"Snap channels exceed the memory allowance.");
    const auto selected_bytes=request.channels.size()*(sizeof(RecordingSnapChannel)+sizeof(std::size_t));
    const auto block_bytes=work_bytes(request.memory_bytes-selected_bytes,source_->channel_count());
    if(request.channels.size()+1>block_bytes/16)
        fail(RecordingError::ResourceLimit,"Snap reader exceeds the memory allowance.");
    auto reservation=admit(request.memory_bytes,request.budget);
    RecordingSnapResult result;long double best=std::numeric_limits<long double>::infinity();
    std::size_t best_rank=0;
    const auto distance=[&](double time,double value) {
        const long double x=(static_cast<long double>(time)-request.target_time)*request.time_scale;
        const long double y=(static_cast<long double>(value)-request.target_value)*request.value_scale;
        return x*x+y*y;
    };
    const auto axis_distance=[](double target,double low,double high,double scale) {
        // Outward endpoints and a rejection margin make pruning conservative.
        low=std::nextafter(low,-std::numeric_limits<double>::infinity());
        high=std::nextafter(high,std::numeric_limits<double>::infinity());
        const long double delta=target<low ? static_cast<long double>(low)-target :
                                target>high ? static_cast<long double>(target)-high : 0;
        const long double scaled=delta*scale;return scaled*scaled;
    };
    try {
        std::vector<std::size_t> channels;channels.reserve(request.channels.size());
        for(const auto& c:request.channels)channels.push_back(c.channel);
        const auto bound=[&](const OverviewBucket& node) {
            if(node.last_time<range.start || node.first_time>range.end)return std::numeric_limits<long double>::infinity();
            const auto map=[&](double t){return request.stitched ? stitched_time(t) : t;};
            const auto x=axis_distance(request.target_time,map(std::max(node.first_time,range.start)),
                                        map(std::min(node.last_time,range.end)),request.time_scale);
            long double lower=std::numeric_limits<long double>::infinity();
            for(const auto& c:request.channels) {
                const auto& trace=node.channels[c.channel];if(!trace.finite_count)continue;
                double a=trace.minimum.value,b=trace.maximum.value;
                if(c.affine){a=a*c.multiplier+c.offset;b=b*c.multiplier+c.offset;}
                const auto y=std::isfinite(a) && std::isfinite(b) ?
                    axis_distance(request.target_value,std::min(a,b),std::max(a,b),request.value_scale) : 0;
                lower=std::min(lower,x+y);
            }
            return lower;
        };
        const auto rejected=[&](long double lower) {
            if(!std::isfinite(lower))return true;
            return result.found && lower>best+64*std::numeric_limits<long double>::epsilon()*std::max(1.0L,best);
        };
        const auto visit=[&](auto&& self,std::size_t level,std::uint64_t index)->void {
            check(cancel);auto node=read_bucket(level,index);if(rejected(bound(node)))return;
            if(level) {
                // Visit the half near the cursor first, without changing tie rules.
                const auto left=read_bucket(level-1,index*2);
                const bool right_exists=index*2+1<levels_[level-1].count;
                const bool right_first=right_exists && request.target_time>(request.stitched ? stitched_time(left.last_time) : left.last_time);
                if(right_first)self(self,level-1,index*2+1);
                self(self,level-1,index*2);
                if(right_exists && !right_first)self(self,level-1,index*2+1);
                return;
            }
            RecordingReadRequest read;read.range=AnalysisRange::selected(node.first_time,node.last_time);
            read.channels=channels;read.block_bytes=block_bytes;std::uint64_t seen=0;++result.leaves_read;
            source_->replay(read,[&](const auto& time,const auto& values) {
                for(std::size_t i=0;i<time.size();++i,++seen) {
                    if((i&4095)==0)check(cancel);
                    if(time[i]<range.start || time[i]>range.end)continue;
                    for(std::size_t rank=0;rank<request.channels.size();++rank) {
                        const auto& c=request.channels[rank];const auto raw=values[rank][i];
                        if(!std::isfinite(raw))continue;
                        const auto value=c.affine ? raw*c.multiplier+c.offset : raw;
                        if(!std::isfinite(value))continue;
                        const auto d=distance(request.stitched ? stitched_time(time[i]) : time[i],value);const auto sample=node.first_sample+seen;
                        if(!result.found || d<best || (d==best &&
                           (rank<best_rank || (rank==best_rank && sample<result.sample)))) {
                            best=d;best_rank=rank;result.found=true;result.time=time[i];result.value=value;
                            result.channel=c.channel;result.sample=sample;
                        }
                    }
                }
            },cancel);
            result.rows_read+=seen;
            if(seen!=node.sample_count)fail(RecordingError::ReadFailure,"Snap sample layout changed.");
        };
        if(!channels.empty())visit(visit,levels_.size()-1,0);
        check(cancel);source_->check_current();
    }catch(const std::bad_alloc&){fail(RecordingError::ResourceLimit,"Not enough memory for original sample query.");}
    return result;
}

std::uint64_t RecordingOverview::visit_gaps(TimeRange range,double threshold,
    const std::function<void(OverviewGap)>& consume,const std::atomic<bool>* cancel,const OverviewOptions& resources) const {
    check(cancel);source_->check_current();
    TimeRange resolved;
    if(!AnalysisRange::selected(range.start,range.end).resolve(source_->bounds(),resolved) ||
       !std::isfinite(threshold) || threshold<=0 || !consume)
        fail(RecordingError::InvalidRequest,"Invalid overview gap query.");
    const auto block_bytes=work_bytes(resources.memory_bytes,source_->channel_count());
    if(base_rows_*16+32>block_bytes)fail(RecordingError::ResourceLimit,"Gap timeline exceeds its memory allowance.");
    auto reservation=admit(resources.memory_bytes,resources.budget);
    std::uint64_t found=0;
    const auto emit=[&](double left,double right) {
        if(right-left>threshold && right>=range.start && left<=range.end) {consume({left,right});++found;check(cancel);}
    };
    const auto visit=[&](auto&& self,std::size_t level,std::uint64_t index)->void {
        check(cancel);const auto node=read_bucket(level,index);
        if(node.last_time<range.start || node.first_time>range.end || node.maximum_spacing<=threshold)return;
        if(!level) {
            const auto time=read_times(index);
            for(std::size_t i=1;i<time.size();++i)emit(time[i-1],time[i]);
        }else {
            self(self,level-1,index*2);
            if(index*2+1<levels_[level-1].count) {
                const auto left=read_bucket(level-1,index*2),right=read_bucket(level-1,index*2+1);
                emit(left.last_time,right.first_time);self(self,level-1,index*2+1);
            }
        }
    };
    visit(visit,levels_.size()-1,0);check(cancel);source_->check_current();return found;
}
} // namespace lvm
