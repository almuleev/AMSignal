#include "gui_filter_state_cache.hpp"
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>
namespace gui {
namespace {
constexpr std::uint64_t limit=2ULL*1024*1024,offset=14695981039346656037ULL;
void check(const std::shared_ptr<const lvm::RecordingSource>& source,const std::atomic<bool>* cancel) {
    if(cancel && cancel->load())throw lvm::RecordingFailure(lvm::RecordingError::Cancelled,"Operation cancelled.");
    source->check_current();
}
[[noreturn]] void invalid(){throw std::runtime_error("Invalid filter continuation cache.");}
std::uint64_t bits(double v){std::uint64_t word;std::memcpy(&word,&v,8);return word;}
double number(std::uint64_t v){double value;std::memcpy(&value,&v,8);return value;}
std::uint64_t hash(const char* bytes,std::size_t size,std::uint64_t value) {
    for(std::size_t i=0;i<size;++i){value^=static_cast<unsigned char>(bytes[i]);value*=1099511628211ULL;}return value;
}
struct Payload {
    std::istream* in=nullptr;std::ostream* out=nullptr;
    std::uint64_t checksum=offset,bytes=0,remaining=limit-24;
    void data(char* p,std::size_t n) {
        if(n>remaining)invalid();
        if(in){in->read(p,n);if(!*in)invalid();}
        else {out->write(p,n);if(!*out)invalid();}
        checksum=hash(p,n,checksum);bytes+=n;remaining-=n;
    }
    std::uint64_t word(std::uint64_t value=0) {
        char p[8];for(unsigned i=0;i<8;++i)p[i]=static_cast<char>(value>>(8*i));data(p,8);
        value=0;for(unsigned i=0;i<8;++i)value|=std::uint64_t(static_cast<unsigned char>(p[i]))<<(8*i);return value;
    }
};
std::string key(const lvm::RecordingSource& source,const std::string& identity) {
    return lvm::recording_cache_key(source.path(),*source.stamp(),identity);
}
void propagate(const lvm::RecordingFailure& error) {
    if(error.error==lvm::RecordingError::Cancelled || error.error==lvm::RecordingError::SourceChanged)throw;
}
}
bool load_filter_checkpoints(const std::shared_ptr<lvm::DiskCache>& cache,
    const std::shared_ptr<const lvm::RecordingSource>& source,const std::string& identity,
    FilterSettings& settings,std::size_t maximum,const std::atomic<bool>* cancel,
    std::vector<FilterCheckpoint>& states) {
    if(!cache || !source->stamp() || !maximum || identity.size()>65536)return false;
    check(source,cancel);std::shared_ptr<lvm::DiskCache::Lease> lease;
    try {
        lease=cache->find(key(*source,identity),lvm::CacheKind::FilterState);if(!lease)return false;
        const auto size=std::filesystem::file_size(lease->path());if(size<24 || size>limit)invalid();
        std::ifstream in(lease->path(),std::ios::binary);char magic[8];in.read(magic,8);
        if(!in || std::memcmp(magic,"FSTATE01",8))invalid();
        Payload header;header.in=&in;const auto expected=header.word(),length=header.word();
        if(length!=size-24)invalid();
        // Validate the entire bounded payload before allocating restored states.
        std::uint64_t checksum=offset,left=length;char block[4096];
        while(left){check(source,cancel);const auto n=std::min<std::uint64_t>(left,sizeof(block));
            in.read(block,n);if(!in)invalid();checksum=hash(block,n,checksum);left-=n;}
        if(checksum!=expected)invalid();
        in.clear();in.seekg(24);
        Payload payload;payload.in=&in;payload.remaining=length;
        const auto identity_size=payload.word();if(identity_size!=identity.size())invalid();
        std::string stored(identity_size,'\0');payload.data(stored.data(),stored.size());if(stored!=identity)invalid();
        const auto step=number(payload.word());
        if(!std::isfinite(step) || step<=0 || (settings.sample_step>0 && settings.sample_step!=step))invalid();
        auto restored_settings=settings;restored_settings.sample_step=step;
        const auto count=payload.word();if(!count || count>maximum)invalid();
        std::vector<FilterCheckpoint> restored;restored.reserve(maximum);
        double previous=-std::numeric_limits<double>::infinity();
        for(std::size_t i=0;i<count;++i) {
            check(source,cancel);FilterCheckpoint saved;saved.time=number(payload.word());
            const auto channels=payload.word();
            if(!std::isfinite(saved.time) || saved.time<previous || saved.time<source->bounds().start ||
               saved.time>source->bounds().end || !channels || channels>source->channel_count())invalid();
            previous=saved.time;saved.channels.reserve(channels);saved.filters.reserve(channels);
            for(std::size_t c=0;c<channels;++c) {
                const auto channel=payload.word();
                if(channel>=source->channel_count() || std::find(saved.channels.begin(),saved.channels.end(),channel)!=saved.channels.end())invalid();
                std::array<double,10> values;for(auto& value:values)value=number(payload.word());
                FilterStream filter(restored_settings);if(!filter.restore_continuation(values) || values[8]!=saved.time)invalid();
                saved.channels.push_back(channel);saved.filters.push_back(std::move(filter));
            }
            restored.push_back(std::move(saved));
        }
        if(payload.remaining || payload.checksum!=expected)invalid();
        check(source,cancel);
        states=std::move(restored);settings=restored_settings;return true;
    }catch(const lvm::RecordingFailure& error){propagate(error);}
     catch(const std::exception&){}
    if(lease)lease->discard();
    return false;
}
void save_filter_checkpoints(const std::shared_ptr<lvm::DiskCache>& cache,
    const std::shared_ptr<const lvm::RecordingSource>& source,const std::string& identity,
    double sample_step,const std::vector<FilterCheckpoint>& states,const std::atomic<bool>* cancel) {
    if(!cache || !source->stamp() || states.empty() || identity.size()>65536)return;
    check(source,cancel);std::shared_ptr<lvm::DiskCache::Lease> lease;
    try {
        lease=cache->begin_write(key(*source,identity),lvm::CacheKind::FilterState,limit);
        std::ofstream out(lease->path(),std::ios::binary);out.write("FSTATE01",8);
        Payload header;header.out=&out;header.word();header.word();
        Payload payload;payload.out=&out;payload.word(identity.size());
        out.write(identity.data(),identity.size());if(!out)invalid();
        payload.checksum=hash(identity.data(),identity.size(),payload.checksum);
        payload.bytes+=identity.size();payload.remaining-=identity.size();payload.word(bits(sample_step));payload.word(states.size());
        for(const auto& saved:states) {
            check(source,cancel);payload.word(bits(saved.time));payload.word(saved.channels.size());
            for(std::size_t i=0;i<saved.channels.size();++i) {
                payload.word(saved.channels[i]);for(auto value:saved.filters[i].continuation())payload.word(bits(value));
            }
        }
        out.seekp(8);header.word(payload.checksum);header.word(payload.bytes);out.flush();if(!out)invalid();
        out.close();if(!out)invalid();check(source,cancel);lease->commit();
    }catch(const lvm::RecordingFailure& error){propagate(error);}
     catch(const std::exception&){}
}
}
