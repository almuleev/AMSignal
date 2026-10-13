#include "scan_index_cache.hpp"
#include <cmath>
#include <cstring>
#include <fstream>

namespace lvm {
namespace {
constexpr std::size_t header_bytes=65536;
constexpr std::uint64_t magic=0x3158444943534D41ULL; // AMSCIDX1; parser semantics version 1.
[[noreturn]] void fail(RecordingError error,const char* text){throw RecordingFailure(error,text);}
void check(const std::atomic<bool>* cancel){if(cancel && cancel->load(std::memory_order_relaxed))fail(RecordingError::Cancelled,"Operation cancelled.");}
RecordingStamp stamp(const std::filesystem::path& path,RecordingError failure=RecordingError::SourceChanged) {
    std::error_code ec;RecordingStamp s;s.bytes=std::filesystem::file_size(path,ec);
    if(ec)fail(failure,"Cannot access recording source.");
    s.modified=std::filesystem::last_write_time(path,ec);if(ec)fail(failure,"Cannot access recording source.");return s;
}
void current(const std::filesystem::path& path,RecordingStamp expected,const std::atomic<bool>* cancel) {
    check(cancel);const auto now=stamp(path);if(now.bytes!=expected.bytes || now.modified!=expected.modified)
        fail(RecordingError::SourceChanged,"The source changed while indexing. Reopen the recording.");
}
std::uint64_t hash(const unsigned char* p,std::size_t n,std::uint64_t value=14695981039346656037ULL) {
    for(std::size_t i=0;i<n;++i){value^=p[i];value*=1099511628211ULL;}return value;
}
void put(unsigned char* p,std::uint64_t value){for(int i=0;i<8;++i)p[i]=static_cast<unsigned char>(value>>(8*i));}
std::uint64_t get(const unsigned char* p){std::uint64_t value=0;for(int i=0;i<8;++i)value|=std::uint64_t(p[i])<<(8*i);return value;}
std::uint64_t bits(double value){std::uint64_t v;std::memcpy(&v,&value,8);return v;}
double number(std::uint64_t value){double v;std::memcpy(&v,&value,8);return v;}
struct Payload {
    std::istream* in=nullptr;std::ostream* out=nullptr;
    std::uint64_t checksum=14695981039346656037ULL,remaining=0;
    void bytes(char* data,std::size_t n) {
        if(n>remaining)fail(RecordingError::ReadFailure,"Invalid scan cache length.");
        if(in){in->read(data,static_cast<std::streamsize>(n));if(!*in)fail(RecordingError::ReadFailure,"Truncated scan cache.");}
        else {out->write(data,static_cast<std::streamsize>(n));if(!*out)fail(RecordingError::ResourceLimit,"Cannot write scan cache.");}
        checksum=hash(reinterpret_cast<const unsigned char*>(data),n,checksum);remaining-=n;
    }
    std::uint64_t word(std::uint64_t value=0) {
        unsigned char buffer[8];put(buffer,value);bytes(reinterpret_cast<char*>(buffer),8);return get(buffer);
    }
};
std::size_t index_limit(const ScanIndexCacheOptions& options) {
    if(options.memory_bytes<=2*header_bytes || options.max_disk_bytes<header_bytes+8)
        fail(RecordingError::ResourceLimit,"Scan cache allowance is too small.");
    const auto limit=(options.memory_bytes-2*header_bytes)/2;
    // Zero means unbounded to the legacy scanner. Never pass it through when
    // a cache request explicitly asked for a tiny bounded workspace.
    if(limit<sizeof(ScanIndex)+sizeof(ScanIndexCheckpoint))
        fail(RecordingError::ResourceLimit,"Scan index allowance cannot hold one checkpoint.");
    return limit;
}
void validate(const ScanIndex& index) {
    if(!TimeRange{index.range_start,index.range_end}.valid() || index.checkpoints.empty())fail(RecordingError::ReadFailure,"Invalid scan cache bounds.");
    double previous=-std::numeric_limits<double>::infinity();std::uint64_t offset=0;
    for(std::size_t i=0;i<index.checkpoints.size();++i) {
        const auto& c=index.checkpoints[i];
        const auto t=std::abs(c.adjusted_time)<1e-12 ? 0 : c.adjusted_time;
        if(!c.have_time_state || !std::isfinite(c.adjusted_time) || c.adjusted_time<=previous ||
           t<index.range_start || t>index.range_end || c.offset>index.file_size ||
           (c.offset && c.offset<=offset) || (!c.offset && (i+1!=index.checkpoints.size() || t!=index.range_end)) ||
           !std::isfinite(c.prev_raw_time) || !std::isfinite(c.prev_adjusted_time) || !std::isfinite(c.time_offset) ||
           !std::isfinite(c.fallback_step) || c.fallback_step<=0 || !std::isfinite(c.section_anchor_seconds) ||
           !std::isfinite(c.active_section_offset_seconds) || !std::isfinite(c.active_section_x0))
            fail(RecordingError::ReadFailure,"Invalid scan checkpoint state.");
        previous=c.adjusted_time;offset=c.offset;
    }
}
std::shared_ptr<ScanIndex> read_index(const std::filesystem::path& cache,const std::filesystem::path& source,
    RecordingStamp expected,std::size_t memory_limit,std::uint64_t disk_limit,const std::atomic<bool>* cancel) {
    const auto size=std::filesystem::file_size(cache);
    if(size<header_bytes+8 || size>disk_limit)fail(RecordingError::ReadFailure,"Invalid scan cache size.");
    std::ifstream in(cache,std::ios::binary);std::vector<unsigned char> header(header_bytes);in.read(reinterpret_cast<char*>(header.data()),header.size());
    if(!in || get(header.data())!=magic || get(header.data()+8)!=1 ||
       get(header.data()+header_bytes-8)!=hash(header.data(),header_bytes-8))fail(RecordingError::ReadFailure,"Invalid scan cache header.");
    const auto path=source.u8string();const auto path_size=get(header.data()+80),checkpoints=get(header.data()+88),
        labels=get(header.data()+96),comments=get(header.data()+104),payload=get(header.data()+112);
    if(get(header.data()+16)!=expected.bytes || get(header.data()+24)!=static_cast<std::uint64_t>(expected.modified.time_since_epoch().count()) ||
       get(header.data()+32)!=std::filesystem::file_time_type::duration::period::num ||
       get(header.data()+40)!=std::filesystem::file_time_type::duration::period::den ||
       path_size!=path.size() || path_size>32768 || std::memcmp(header.data()+120,path.data(),path.size()) ||
       payload!=size-header_bytes-8 || checkpoints>memory_limit/sizeof(ScanIndexCheckpoint) ||
       labels>memory_limit/sizeof(std::string) || comments>memory_limit/sizeof(std::string) ||
       checkpoints*80>payload || labels+comments>(payload-checkpoints*80)/8)
        fail(RecordingError::ReadFailure,"Scan cache does not match source or allowances.");
    auto index=std::make_shared<ScanIndex>();index->file_size=expected.bytes;index->modified=expected.modified;
    index->range_start=number(get(header.data()+48));index->range_end=number(get(header.data()+56));
    // Reserved fields must stay zero until a new schema/version is introduced.
    if(get(header.data()+64) || get(header.data()+72))fail(RecordingError::ReadFailure,"Unsupported scan cache fields.");
    const auto base=checkpoints*sizeof(ScanIndexCheckpoint)+(labels+comments)*sizeof(std::string)+sizeof(ScanIndex);
    if(base>memory_limit)fail(RecordingError::ReadFailure,"Scan cache index is too large.");
    index->checkpoints.reserve(static_cast<std::size_t>(checkpoints));
    index->column_labels.reserve(static_cast<std::size_t>(labels));index->export_comments.reserve(static_cast<std::size_t>(comments));
    Payload data{&in,nullptr,14695981039346656037ULL,payload};
    for(std::uint64_t i=0;i<checkpoints;++i) {
        if((i&255)==0)current(source,expected,cancel);
        ScanIndexCheckpoint c;c.offset=data.word();c.adjusted_time=number(data.word());c.prev_raw_time=number(data.word());
        c.prev_adjusted_time=number(data.word());c.time_offset=number(data.word());c.fallback_step=number(data.word());
        const auto flags=data.word();if(flags>7)fail(RecordingError::ReadFailure,"Invalid checkpoint flags.");
        c.have_time_state=flags&1;c.have_section_anchor=flags&2;c.active_section_valid=flags&4;
        c.section_anchor_seconds=number(data.word());c.active_section_offset_seconds=number(data.word());c.active_section_x0=number(data.word());
        index->checkpoints.push_back(c);
    }
    for(auto group:{std::pair<std::vector<std::string>*,std::uint64_t>{&index->column_labels,labels},{&index->export_comments,comments}}) {
        for(std::uint64_t i=0;i<group.second;++i) {
            if((i&255)==0)current(source,expected,cancel);
            const auto length=data.word();const auto used=index->memory_bytes();
            if(used>memory_limit || length>memory_limit-used || length>data.remaining)
                fail(RecordingError::ReadFailure,"Invalid scan cache string allowance.");
            std::string text(static_cast<std::size_t>(length),'\0');data.bytes(text.data(),text.size());group.first->push_back(std::move(text));
            if(index->memory_bytes()>memory_limit)fail(RecordingError::ReadFailure,"Scan cache strings exceed the allowance.");
        }
    }
    unsigned char footer[8];in.read(reinterpret_cast<char*>(footer),8);
    if(data.remaining || !in || get(footer)!=data.checksum)fail(RecordingError::ReadFailure,"Scan cache checksum mismatch.");
    validate(*index);current(source,expected,cancel);return index;
}
void write_index(const std::filesystem::path& cache,const std::filesystem::path& source,const ScanIndex& index,
    std::uint64_t disk_limit,const std::atomic<bool>* cancel) {
    validate(index);const auto path=source.u8string();if(path.size()>32768)fail(RecordingError::ResourceLimit,"Source path too long for scan cache.");
    std::uint64_t payload=index.checkpoints.size()*80;
    for(const auto* group:{&index.column_labels,&index.export_comments})for(const auto& text:*group)payload=saturated_add(payload,8+text.size());
    if(payload>disk_limit || header_bytes+8>disk_limit-payload)fail(RecordingError::ResourceLimit,"Scan cache exceeds its disk allowance.");
    std::vector<unsigned char> header(header_bytes);put(header.data(),magic);put(header.data()+8,1);
    put(header.data()+16,index.file_size);put(header.data()+24,static_cast<std::uint64_t>(index.modified.time_since_epoch().count()));
    put(header.data()+32,std::filesystem::file_time_type::duration::period::num);put(header.data()+40,std::filesystem::file_time_type::duration::period::den);
    put(header.data()+48,bits(index.range_start));put(header.data()+56,bits(index.range_end));put(header.data()+80,path.size());
    put(header.data()+88,index.checkpoints.size());put(header.data()+96,index.column_labels.size());put(header.data()+104,index.export_comments.size());
    put(header.data()+112,payload);std::memcpy(header.data()+120,path.data(),path.size());put(header.data()+header_bytes-8,hash(header.data(),header_bytes-8));
    std::ofstream out(cache,std::ios::binary);out.write(reinterpret_cast<const char*>(header.data()),header.size());
    if(!out)fail(RecordingError::ResourceLimit,"Cannot create scan cache.");
    Payload data{nullptr,&out,14695981039346656037ULL,payload};
    for(const auto& c:index.checkpoints) {
        check(cancel);data.word(c.offset);data.word(bits(c.adjusted_time));data.word(bits(c.prev_raw_time));data.word(bits(c.prev_adjusted_time));
        data.word(bits(c.time_offset));data.word(bits(c.fallback_step));data.word(c.have_time_state|(c.have_section_anchor<<1)|(c.active_section_valid<<2));
        data.word(bits(c.section_anchor_seconds));data.word(bits(c.active_section_offset_seconds));data.word(bits(c.active_section_x0));
    }
    for(const auto* group:{&index.column_labels,&index.export_comments})for(const auto& text:*group) {
        check(cancel);data.word(text.size());data.bytes(const_cast<char*>(text.data()),text.size());
    }
    unsigned char footer[8];put(footer,data.checksum);out.write(reinterpret_cast<const char*>(footer),8);out.close();
    if(!out || data.remaining)fail(RecordingError::ResourceLimit,"Cannot finish scan cache.");
    current(source,{index.file_size,index.modified},cancel);
}
}
CachedScanIndex scan_index_cached(const std::filesystem::path& original,std::shared_ptr<DiskCache> cache,
    const ScanIndexCacheOptions& options,const std::atomic<bool>* cancel) {
    try {
        const auto limit=index_limit(options);check(cancel);
        auto reservation=options.budget ? options.budget->reserve(options.memory_bytes) : nullptr;
        if(options.budget && !reservation)fail(RecordingError::ResourceLimit,"Insufficient memory for scan index.");
        const auto path=std::filesystem::absolute(original).lexically_normal();const auto expected=stamp(path,RecordingError::ReadFailure);
        const auto key=recording_cache_key(path,expected,"scan-amscidx1-parser1");CachedScanIndex result;
        if(cache)try {
            auto lease=cache->find(key,CacheKind::ScanIndex);
            if(lease)try{result.index=read_index(lease->path(),path,expected,limit,options.max_disk_bytes,cancel);result.from_cache=true;}
            catch(const RecordingFailure& e){if(e.error==RecordingError::Cancelled || e.error==RecordingError::SourceChanged)throw;lease->discard();result.cache_error=e.error;}
        }catch(const RecordingFailure& e){if(e.error==RecordingError::Cancelled || e.error==RecordingError::SourceChanged)throw;result.cache_error=e.error;}
         catch(const std::filesystem::filesystem_error&){result.cache_error=RecordingError::ResourceLimit;}
        if(!result.index) {
            result.index=std::make_shared<ScanIndex>();double first=0,last=0;std::string error;
            if(!scan_time_bounds(path,first,last,error,cancel,result.index.get(),limit)) {
                check(cancel);if(error.find("memory allowance")!=std::string::npos)fail(RecordingError::ResourceLimit,error.c_str());
                fail(RecordingError::ReadFailure,error.c_str());
            }
            current(path,expected,cancel);
            if(result.index->file_size!=expected.bytes || result.index->modified!=expected.modified)
                fail(RecordingError::SourceChanged,"Source changed before scanning.");
            if(cache)try {
                auto lease=cache->begin_write(key,CacheKind::ScanIndex,options.max_disk_bytes);
                write_index(lease->path(),path,*result.index,options.max_disk_bytes,cancel);
                current(path,expected,cancel);lease->commit();current(path,expected,cancel);
            }catch(const RecordingFailure& e){if(e.error==RecordingError::Cancelled || e.error==RecordingError::SourceChanged)throw;result.cache_error=e.error;}
             catch(const std::filesystem::filesystem_error&){result.cache_error=RecordingError::ResourceLimit;}
        }
        current(path,expected,cancel);
        if(result.index->memory_bytes()>limit || (reservation && !reservation->shrink_to(result.index->memory_bytes())))
            fail(RecordingError::ResourceLimit,"Scan index exceeded its admission.");
        result.index->memory_reservation=std::move(reservation);return result;
    }catch(const std::bad_alloc&){fail(RecordingError::ResourceLimit,"Insufficient memory for scan index.");}
}
std::shared_ptr<const RecordingOverview> managed_recording_overview(std::shared_ptr<const RecordingSource> source,
    std::shared_ptr<DiskCache> cache,const OverviewOptions& options,const std::atomic<bool>* cancel) {
    try {
    if(!source || !cache || !source->stamp())fail(RecordingError::InvalidRequest,"Missing managed overview source or cache.");
    check(cancel);source->check_current();std::string variant="overview-amsoidx2-b"+std::to_string(options.base_rows)+
        "-range"+std::to_string(bits(source->bounds().start))+":"+std::to_string(bits(source->bounds().end));
    for(auto column:source->physical_columns())variant+=":"+std::to_string(column);
    variant+=source->processing_identity();
    const auto key=recording_cache_key(source->path(),*source->stamp(),variant);
    struct Pinned {std::shared_ptr<DiskCache::Lease> lease;std::shared_ptr<const RecordingOverview> index;};
    auto result=std::make_shared<Pinned>();result->lease=cache->find(key,CacheKind::Overview);
    if(result->lease)try {
        result->index=RecordingOverview::open(source,result->lease->path());
        if(result->index->disk_bytes()>options.max_disk_bytes)fail(RecordingError::ResourceLimit,"Managed overview exceeds this request's disk allowance.");
    }catch(const RecordingFailure& e) {
        if(e.error!=RecordingError::ReadFailure)throw;
        result->lease->discard();result->lease.reset();result->index.reset();
    }
    if(!result->index) {
        result->lease=cache->begin_write(key,CacheKind::Overview,options.max_disk_bytes);
        auto built=RecordingOverview::build(source,result->lease->path(),options,cancel);built.reset();
        // Close the temporary index handle before renaming on Windows.
        check(cancel);source->check_current();result->lease->commit();
        result->index=RecordingOverview::open(source,result->lease->path());
    }
    check(cancel);source->check_current();return std::shared_ptr<const RecordingOverview>(result,result->index.get());
    }catch(const std::bad_alloc&){fail(RecordingError::ResourceLimit,"Insufficient memory for managed overview.");}
     catch(const std::filesystem::filesystem_error&){fail(RecordingError::ResourceLimit,"Cannot access managed overview disk.");}
}
} // namespace lvm
