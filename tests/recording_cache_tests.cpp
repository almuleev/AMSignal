#include "scan_index_cache.hpp"
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <future>
#include <thread>

namespace {
using Check=void (*)(bool,const std::string&);
struct Files {
    std::filesystem::path root=std::filesystem::path("tests")/("_tmp_cache_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files(){std::filesystem::create_directories(root);}
    ~Files(){std::error_code ec;std::filesystem::remove_all(root,ec);}
};
template<class Action> bool rejects(lvm::RecordingError error,Action action) {
    try{action();}catch(const lvm::RecordingFailure& e){return e.error==error;}return false;
}
void write(const std::filesystem::path& path,std::size_t n) {std::ofstream out(path,std::ios::binary);out<<std::string(n,'x');}
std::shared_ptr<lvm::DiskCache::Lease> item(lvm::DiskCache& cache,const std::string& key,std::size_t quota,std::size_t size) {
    auto lease=cache.begin_write(key,lvm::CacheKind::ScanIndex,quota);write(lease->path(),size);lease->commit();return lease;
}
bool same(double a,double b){return std::memcmp(&a,&b,sizeof(double))==0;}
void replace_word(const std::filesystem::path& path,std::size_t offset,std::uint64_t value,bool header) {
    std::ifstream in(path,std::ios::binary);std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),{});in.close();
    const auto put=[&](std::size_t at,std::uint64_t n){for(int i=0;i<8;++i)data.at(at+i)=static_cast<unsigned char>(n>>(8*i));};put(offset,value);
    const auto begin=header ? std::size_t(0) : std::size_t(65536),end=header ? std::size_t(65528) : data.size()-8;
    std::uint64_t checksum=14695981039346656037ULL;for(auto i=begin;i<end;++i){checksum^=data[i];checksum*=1099511628211ULL;}put(end,checksum);
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data.data()),data.size());
}
bool equal(const lvm::ScanIndex& a,const lvm::ScanIndex& b) {
    if(!same(a.range_start,b.range_start) || !same(a.range_end,b.range_end) || a.file_size!=b.file_size || a.modified!=b.modified ||
       a.column_labels!=b.column_labels || a.export_comments!=b.export_comments || a.checkpoints.size()!=b.checkpoints.size())return false;
    for(std::size_t i=0;i<a.checkpoints.size();++i) {
        const auto& x=a.checkpoints[i];const auto& y=b.checkpoints[i];
        if(x.offset!=y.offset || !same(x.adjusted_time,y.adjusted_time) || !same(x.prev_raw_time,y.prev_raw_time) ||
           !same(x.prev_adjusted_time,y.prev_adjusted_time) || !same(x.time_offset,y.time_offset) || !same(x.fallback_step,y.fallback_step) ||
           x.have_time_state!=y.have_time_state || x.have_section_anchor!=y.have_section_anchor || x.active_section_valid!=y.active_section_valid ||
           !same(x.section_anchor_seconds,y.section_anchor_seconds) || !same(x.active_section_offset_seconds,y.active_section_offset_seconds) ||
           !same(x.active_section_x0,y.active_section_x0))return false;
    }return true;
}
void disk(Check check) {
    Files files;const auto root=files.root/"owned";std::filesystem::create_directory(root);
    const auto foreign=root/"user.bin",abandoned=root/".ams-aaaaaaaaaaaaaaaa-1111111111111111.part",scratch=root/"ams-overview-123-456";
    write(foreign,20);write(abandoned,20);std::filesystem::create_directory(scratch);write(scratch/"partial",20);
    auto cache=std::make_shared<lvm::DiskCache>(root,100,4);
    check(std::filesystem::exists(foreign) && !std::filesystem::exists(abandoned) && !std::filesystem::exists(scratch),"startup removes only recognized abandoned cache outputs and scratch directories");
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{lvm::DiskCache another(root,100);}),"private cache directory has one process owner");
    check(rejects(lvm::RecordingError::InvalidRequest,[&]{cache->begin_write("../recording",lvm::CacheKind::ScanIndex,1);}),"cache rejects path-like keys");
    auto pending=cache->begin_write("aaaaaaaaaaaaaaaa",lvm::CacheKind::ScanIndex,80);const auto partial=pending->path();write(partial,10);
    check(cache->used_bytes()==80 && !cache->find("aaaaaaaaaaaaaaaa",lvm::CacheKind::ScanIndex),"temporary output reserves maximum quota and is unpublished");
    pending.reset();check(cache->used_bytes()==0 && !std::filesystem::exists(partial),"cancelled writer releases quota and removes partial output");
    auto a=item(*cache,"aaaaaaaaaaaaaaaa",80,30);const auto ap=a->path();
    check(cache->used_bytes()==30 && ap.extension()==".scan","commit converts temporary quota into actual payload bytes");
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{cache->set_limit(29);}) && std::filesystem::exists(ap),"cannot lower disk budget below a live pin");
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{cache->begin_write("bbbbbbbbbbbbbbbb",lvm::CacheKind::Overview,80);}),"pinned reader is protected from eviction to admit a writer");
    auto b=item(*cache,"bbbbbbbbbbbbbbbb",50,30);const auto bp=b->path();a.reset();b.reset();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));a=cache->find("aaaaaaaaaaaaaaaa",lvm::CacheKind::ScanIndex);a.reset();
    auto c=item(*cache,"cccccccccccccccc",60,40);
    check(std::filesystem::exists(ap) && !std::filesystem::exists(bp) && cache->used_bytes()==70,"least recently used unpinned artifact is evicted across shared quota");
    c.reset();cache->set_limit(35);check(cache->used_bytes()<=35 && std::filesystem::exists(foreign),"lowering disk budget evicts cached artifacts but preserves foreign files");
    cache->set_limit(200);auto old=item(*cache,"dddddddddddddddd",50,20);const auto oldpath=old->path();
    auto newer=item(*cache,"dddddddddddddddd",50,21);const auto newpath=newer->path();
    check(oldpath!=newpath && std::filesystem::exists(oldpath) && cache->find("dddddddddddddddd",lvm::CacheKind::ScanIndex)->path()==newpath,"new cache versions never overwrite the file held by an old reader");
    old->discard();check(!cache->find("dddddddddddddddd",lvm::CacheKind::Overview),"cache kinds cannot alias one another");
    old.reset();check(!std::filesystem::exists(oldpath),"invalid pinned version is removed only after its last reader exits");
    cache.reset();check(rejects(lvm::RecordingError::ResourceLimit,[&]{lvm::DiskCache another(root,200);}),"live lease keeps directory ownership after manager destruction");
    newer.reset();cache=std::make_shared<lvm::DiskCache>(root,200,4);
    auto adopted=cache->find("dddddddddddddddd",lvm::CacheKind::ScanIndex);check(adopted && adopted->path()==newpath,"committed payload is adopted on process restart");
    adopted.reset();auto oversize=cache->begin_write("eeeeeeeeeeeeeeee",lvm::CacheKind::ScanIndex,10);write(oversize->path(),11);
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{oversize->commit();}) && !cache->find("eeeeeeeeeeeeeeee",lvm::CacheKind::ScanIndex),"oversized output cannot be published");oversize.reset();
    write(newpath,22);check(!cache->find("dddddddddddddddd",lvm::CacheKind::ScanIndex),"unexpected payload length invalidates an adopted entry");
    cache.reset();cache=std::make_shared<lvm::DiskCache>(root,100,2);
    for(int i=0;i<5;++i)item(*cache,std::string(16,char('a'+i)),10,1).reset();
    check(cache->entry_count()==2 && cache->used_bytes()==2,"entry count bounds metadata independently of payload size");
    cache.reset();cache=std::make_shared<lvm::DiskCache>(files.root/"concurrent",1024*1024,64);
    std::vector<std::future<std::shared_ptr<lvm::DiskCache::Lease>>> tasks;
    for(int i=0;i<16;++i)tasks.push_back(std::async(std::launch::async,[cache,i]{return item(*cache,std::string(16,"0123456789abcdef"[i]),4096,100);}));
    std::vector<std::shared_ptr<lvm::DiskCache::Lease>> pins;for(auto& task:tasks)pins.push_back(task.get());
    check(cache->used_bytes()==1600 && cache->entry_count()==16,"concurrent writers reserve and commit once under one common disk budget");
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{cache->set_limit(1599);}),"concurrent live readers remain pinned during budget changes");
    pins.clear();cache->set_limit(100);check(cache->used_bytes()==100 && cache->entry_count()==1,"concurrent output becomes evictable when all leases exit");
}
void fixture(const std::filesystem::path& path) {
    std::ofstream out(path);out<<"LabVIEW Measurement\nDate\t2009/05/15\nTime\t00:00:00,000\n***End_of_Header***\nTime\tA\tTime\tB\n";
    out<<std::setprecision(17);
    for(int section=0;section<3;++section) {
        if(section)out<<"***End_of_Header***\nDate\t2009/05/15\nTime\t00:00:"<<(section==1 ? "10" : "20")<<",000\nX0\t0\n***End_of_Header***\nTime\tA\tTime\tB\n";
        for(int i=0;i<4500;++i){const double t=i*.001;out<<t<<'\t'<<i%17<<'\t'<<t<<'\t'<<(i==102 ? "NaN" : std::to_string(section*4500+i))<<'\n';}
    }
    out<<"# exported note\n";
}
void scan(Check check) {
    Files files;const auto path=files.root/"source.lvm";fixture(path);
    auto pool=std::make_shared<lvm::DiskCache>(files.root/"cache",64ULL*1024*1024);
    auto budget=std::make_shared<lvm::MemoryBudget>();budget->configure(64ULL*1024*1024,0,64ULL*1024*1024);
    lvm::ScanIndexCacheOptions options;options.budget=budget;
    lvm::ScanIndex original;double first=0,last=0;std::string error;
    check(lvm::scan_time_bounds(path,first,last,error,nullptr,&original),"control scan builds checkpoint fixture with multiple sections");
    auto fresh=lvm::scan_index_cached(path,pool,options);auto hit=lvm::scan_index_cached(path,pool,options);
    check(!fresh.from_cache && !fresh.cache_error && hit.from_cache && !hit.cache_error,"first index is persisted and second opening hits cache");
    check(equal(original,*fresh.index) && equal(original,*hit.index),"all checkpoint values, flags, bounds, labels and comments survive bitwise round trip");
    check(budget->reserved()==fresh.index->memory_bytes()+hit.index->memory_bytes(),"loaded and fresh indices own separate exact resident reservations");
    const auto source=[&](std::shared_ptr<const lvm::ScanIndex> index){return lvm::RecordingSource::from_file(path,{first,last},{1,3},std::move(index));};
    auto old=source(fresh.index),cached=source(hit.index);bool exact=true;
    lvm::LoadOptions control_options;control_options.use_time_window=true;control_options.time_start=first;control_options.time_end=last;
    auto control=lvm::read_lvm_file(path,control_options);
    check(control.ok && control.rows()==13500,"full original control locates samples across acquisition sections");
    for(std::size_t position:{std::size_t(0),std::size_t(4095),std::size_t(4503),std::size_t(8995),std::size_t(9100),std::size_t(13499)}) {
        const auto t=control.time.at(position);
        lvm::RecordingReadRequest req;req.range=lvm::AnalysisRange::selected(t,std::min(last,t+.004));req.channels={1,0};req.block_rows=2;
        std::vector<double> x1,x2;std::vector<std::vector<double>> y1(2),y2(2);
        const auto collect=[](auto& x,auto& y){return [&](const auto& t,const auto& data){x.insert(x.end(),t.begin(),t.end());for(std::size_t c=0;c<y.size();++c)y[c].insert(y[c].end(),data[c].begin(),data[c].end());};};
        old->replay(req,collect(x1,y1));cached->replay(req,collect(x2,y2));
        exact&=x1.size()==x2.size() && y1[0].size()==y2[0].size();
        for(std::size_t i=0;i<x1.size() && exact;++i)exact&=same(x1[i],x2[i]) && same(y1[0][i],y2[0][i]) && same(y1[1][i],y2[1][i]);
    }
    check(exact,"replayed original samples match across section, checkpoint and near-end boundaries");
    fresh.index.reset();hit.index.reset();check(budget->reserved()>0,"source sharing retains index admission after scan result destruction");
    old.reset();cached.reset();check(budget->reserved()==0,"last source releases admitted index arrays");
    const auto stamp=lvm::RecordingStamp{std::filesystem::file_size(path),std::filesystem::last_write_time(path)};
    const auto key=lvm::recording_cache_key(path,stamp,"scan-amscidx1-parser1");auto lease=pool->find(key,lvm::CacheKind::ScanIndex);
    const auto corrupt=[&](std::size_t offset){std::fstream out(lease->path(),std::ios::in|std::ios::out|std::ios::binary);out.seekg(offset);char b;out.read(&b,1);b^=0x55;out.seekp(offset);out.write(&b,1);};
    corrupt(65536+15);lease.reset();auto repaired=lvm::scan_index_cached(path,pool,options);
    check(!repaired.from_cache && repaired.cache_error==lvm::RecordingError::ReadFailure && equal(original,*repaired.index),"damaged payload is rejected and rescanned without changing index semantics");
    lease=pool->find(key,lvm::CacheKind::ScanIndex);corrupt(8);lease.reset();repaired=lvm::scan_index_cached(path,pool,options);
    check(!repaired.from_cache && repaired.cache_error && equal(original,*repaired.index),"damaged header cannot publish a partial cached index");
    lease=pool->find(key,lvm::CacheKind::ScanIndex);std::filesystem::resize_file(lease->path(),100);lease.reset();
    auto truncated=lvm::scan_index_cached(path,pool,options);check(!truncated.from_cache && equal(original,*truncated.index),"truncated payload safely rebuilds a valid index");
    for(const auto& word:std::vector<std::pair<std::size_t,std::uint64_t>>{{8,2},{88,std::numeric_limits<std::uint64_t>::max()},{64,1},{65536+8,0x7ff8000000000000ULL}}) {
        lease=pool->find(key,lvm::CacheKind::ScanIndex);replace_word(lease->path(),word.first,word.second,word.first<65536);lease.reset();
        auto rejected=lvm::scan_index_cached(path,pool,options);
        check(!rejected.from_cache && rejected.cache_error==lvm::RecordingError::ReadFailure && equal(original,*rejected.index),
            "valid checksum does not permit an unsupported schema, excessive count, reserved field or nonfinite parser state");
    }
    std::atomic<bool> cancel{true};check(rejects(lvm::RecordingError::Cancelled,[&]{lvm::scan_index_cached(path,pool,options,&cancel);}),"cancelled index request does not read or publish cache output");
    auto tiny=options;tiny.memory_bytes=131073;check(rejects(lvm::RecordingError::ResourceLimit,[&]{lvm::scan_index_cached(path,pool,tiny);}),"fresh fallback cannot bypass the admitted index memory limit");
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{lvm::scan_index_cached(files.root/"missing.txt",pool,tiny);}),"an allowance smaller than one checkpoint is refused before any source I/O");
    check(rejects(lvm::RecordingError::ReadFailure,[&]{lvm::scan_index_cached(files.root/"missing.txt",pool,options);}),"initial missing source is a read failure rather than a changed-source fallback");
    budget->configure(1,0,1);check(rejects(lvm::RecordingError::ResourceLimit,[&]{lvm::scan_index_cached(path,pool,options);}),"cached index opening requires memory admission before allocation");budget->configure(64ULL*1024*1024,0,64ULL*1024*1024);
    auto no_disk=std::make_shared<lvm::DiskCache>(files.root/"tiny",65536);
    auto uncached=lvm::scan_index_cached(path,no_disk,options);check(!uncached.from_cache && uncached.cache_error==lvm::RecordingError::ResourceLimit && equal(original,*uncached.index),"optional disk denial preserves successful bounded scanning");
    const auto copied=files.root/"copy.lvm";std::filesystem::copy_file(path,copied);std::filesystem::last_write_time(copied,stamp.modified);
    auto copy=lvm::scan_index_cached(copied,pool,options);check(!copy.from_cache && lvm::recording_cache_key(copied,stamp,"scan-amscidx1-parser1")!=key,"identical size and modification time of another path cannot alias a cache");
    {std::ofstream out(path,std::ios::app);out<<"# changed\n";}auto changed=lvm::scan_index_cached(path,pool,options);
    check(!changed.from_cache && changed.index->file_size!=original.file_size,"changed file invalidates persisted index identity");
    const auto single=files.root/"no-newline.txt";{std::ofstream out(single);out<<"Time\tA\n0\t3";}
    auto one=lvm::scan_index_cached(single,pool,options);auto one_hit=lvm::scan_index_cached(single,pool,options);
    check(one_hit.from_cache && equal(*one.index,*one_hit.index),"EOF checkpoint with offset zero and one sample round trips correctly");
    const auto csv=files.root/"quoted.csv";{std::ofstream out(csv);out<<"Time,\"A, volts\",\"line one\nline two\"\n0,3,4\n1,5,6\n";}
    auto quoted=lvm::scan_index_cached(csv,pool,options);auto quoted_hit=lvm::scan_index_cached(csv,pool,options);
    check(quoted_hit.from_cache && equal(*quoted.index,*quoted_hit.index),"quoted and multiline column names round trip without reparsing headers");
}
void overview(Check check) {
    Files files;const auto path=files.root/"signal.txt";{std::ofstream out(path);out<<"Time\tA\tB\n";for(int i=0;i<10000;++i)out<<i<<'\t'<<i%17<<'\t'<<i%23<<'\n';}
    auto pool=std::make_shared<lvm::DiskCache>(files.root/"cache",4ULL*1024*1024);
    auto scanned=lvm::scan_index_cached(path,nullptr);auto source=lvm::RecordingSource::from_file(path,{0,9999},{1,2},scanned.index);
    lvm::OverviewOptions options;options.max_disk_bytes=1024*1024;options.base_rows=64;int progress=0;options.progress=[&](auto,auto){++progress;};
    auto built=lvm::managed_recording_overview(source,pool,options);const auto size=pool->used_bytes();progress=0;
    auto new_source=lvm::RecordingSource::from_file(path,{0,9999},{1,2},scanned.index);auto reused=lvm::managed_recording_overview(new_source,pool,options);
    check(built->sample_count()==10000 && reused->sample_count()==10000 && progress==0 && pool->used_bytes()==size && reused->source()==new_source,"managed overview reopens validated hierarchy and binds the current source descriptor");
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{pool->set_limit(size-1);}),"active overview handle pins its disk payload");
    pool->set_limit(size+1);check(rejects(lvm::RecordingError::ResourceLimit,[&]{pool->begin_write("ffffffffffffffff",lvm::CacheKind::ScanIndex,2);}),"scan quota cannot evict an active overview in the common disk budget");
    pool->set_limit(4ULL*1024*1024);auto changed_options=options;changed_options.base_rows=128;auto variant=lvm::managed_recording_overview(source,pool,changed_options);
    check(pool->entry_count()==2 && variant->sample_count()==10000,"overview bucket layout is part of the cache identity");
    auto mapped=lvm::RecordingSource::from_file(path,{0,9999},{2,1},scanned.index);auto mapping=lvm::managed_recording_overview(mapped,pool,options);
    check(pool->entry_count()==3 && mapping->source()->physical_columns()==std::vector<std::size_t>({2,1}),"physical channel mapping cannot reuse an incompatible overview");
    lvm::OverviewRequest request;request.range=lvm::AnalysisRange::selected(9990,9999);request.channels={0,1};request.pixel_columns=100;
    auto view=reused->read_view(request);check(view.sample_count==10 && view.buckets[0].channels[0].first.value==9990%17,"original close-up works while cache lease owns the hierarchy");
    std::atomic<bool> cancel{true};check(rejects(lvm::RecordingError::Cancelled,[&]{lvm::managed_recording_overview(source,pool,options,&cancel);}),"cancelled managed overview publishes no handle");
    const auto pinned_count=pool->entry_count();auto too_small=options;too_small.max_disk_bytes=1;
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{lvm::managed_recording_overview(source,pool,too_small);}) && pool->entry_count()==pinned_count,"reused hierarchy respects per-request disk allowance");
    built.reset();reused.reset();variant.reset();mapping.reset();pool->set_limit(1);
    check(pool->used_bytes()==0 && pool->entry_count()==0,"all overview artifacts become evictable after their last shared handle is released");
    pool->set_limit(4ULL*1024*1024);built=lvm::managed_recording_overview(source,pool,options);std::filesystem::path bad_path;
    for(const auto& file:std::filesystem::directory_iterator(pool->directory()))if(file.path().extension()==".overview")bad_path=file.path();
    {std::fstream out(bad_path,std::ios::in|std::ios::out|std::ios::binary);out.seekp(8);out.put('x');}
    reused=lvm::managed_recording_overview(source,pool,options);
    check(pool->entry_count()==2 && reused->sample_count()==10000 && built->read_view(request).sample_count==10,
        "damaged overview is rebuilt at a new path while the existing reader remains pinned");
    built.reset();check(!std::filesystem::exists(bad_path) && pool->entry_count()==1,"damaged old overview is deleted after its active reader exits");
    reused.reset();pool->set_limit(1);
    pool->set_limit(4ULL*1024*1024);cancel=false;auto interrupted=options;interrupted.progress=[&](auto,auto){cancel=true;};
    check(rejects(lvm::RecordingError::Cancelled,[&]{lvm::managed_recording_overview(source,pool,interrupted,&cancel);}) && pool->used_bytes()==0 && pool->entry_count()==0,
        "cancellation during managed construction removes scratch and reserved partial output");
    bool clean=true;for(const auto& file:std::filesystem::directory_iterator(pool->directory()))clean&=file.path().filename()==".ams-owner";
    check(clean,"cancelled overview leaves no disk scratch artifacts");cancel=false;
    auto changing=options;changing.progress=[&](auto,auto){std::ofstream out(path,std::ios::app);out<<"# source changed\n";};
    check(rejects(lvm::RecordingError::SourceChanged,[&]{lvm::managed_recording_overview(source,pool,changing);}) && pool->used_bytes()==0,
        "source change during construction cannot commit an overview of another recording revision");
}
}
void test_recording_cache(Check check){disk(check);scan(check);overview(check);}
