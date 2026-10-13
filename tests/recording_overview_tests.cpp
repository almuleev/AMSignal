#include "recording_overview.hpp"
#include "overview_worker.hpp"
#include "disk_spectrum.hpp"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <future>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>

namespace {
struct Files {
    std::filesystem::path root;
    Files() {
        root=std::filesystem::path("tests")/("_tmp_overview_"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directory(root);
    }
    ~Files() {std::error_code ec;std::filesystem::remove_all(root,ec);}
};
void worker_checks(void (*check)(bool,const std::string&)) {
    Files files;const auto path=files.root/"worker.txt",cache=files.root/"worker.bin";
    {std::ofstream out(path);out<<"Time\tA\tB\n";
     for(int i=0;i<200000;++i)out<<i<<'\t'<<i%17<<'\t'<<i%23<<'\n';}
    auto source=lvm::RecordingSource::from_file(path,{0,199999},{1,2});
    auto budget=std::make_shared<lvm::MemoryBudget>();budget->configure(64ULL*1024*1024,0,64ULL*1024*1024);
    lvm::OverviewWorker worker;lvm::OverviewWorker::Request request;
    request.source=source;request.cache=cache;request.generation=1;
    request.build.budget=budget;request.build.base_rows=64;request.view.budget=budget;request.view.channels={1,0};
    const auto wait=[&] {
        std::optional<lvm::OverviewWorker::Result> result;const auto start=std::chrono::steady_clock::now();
        while(!(result=worker.take_result()) && std::chrono::steady_clock::now()-start<std::chrono::seconds(15))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return result;
    };
    // Invalid requests must not first scan/build the entire file.
    request.view.pixel_columns=0;worker.submit(request);auto result=wait();
    check(result && result->error==lvm::RecordingError::InvalidRequest && !std::filesystem::exists(cache),
          "overview worker validates viewport before building a cache");
    request.view.pixel_columns=512;request.generation=2;worker.submit(request);
    const auto start=std::chrono::steady_clock::now();
    while(worker.progress().rows==0 && std::chrono::steady_clock::now()-start<std::chrono::seconds(5))
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    check(worker.progress().generation==2 && worker.progress().rows>0,"background overview reports progress without GUI callbacks");
    worker.cancel();request.generation=3;worker.submit(request);result=wait();
    check(result && result->generation==3 && !result->error && result->view && result->view->sample_count==200000,
          "replacement cancels an active build and publishes only the latest complete overview");
    check(result && result->channels==std::vector<std::size_t>({1,0}) && result->source==source,
          "background result retains source identity and logical channel order");
    check(worker.progress().phase==lvm::OverviewWorker::Phase::Idle && worker.progress().disk_bytes>0,
          "completed progress keeps counts and marks the worker idle");
    request.index=result ? result->index : nullptr;request.generation=4;
    request.view.range=lvm::AnalysisRange::selected(100,110);result.reset();worker.submit(request);result=wait();
    check(result && !result->error && result->view && result->view->sample_count==11 &&
          result->view->buckets.front().channels[0].first.value==100%23,
          "indexed background zoom loads exact original samples in requested channel order");
    check(budget->reserved()>0,"taken background view continues to own its memory admission");
    result.reset();check(budget->reserved()==0,"discarding a taken view releases its admission");
    request.generation=5;request.build.max_disk_bytes=1;worker.submit(request);result=wait();
    check(result && result->error==lvm::RecordingError::ResourceLimit && !result->view && !result->index,
          "reusing a background index respects the requested disk limit and publishes no partial output");
    request.build.max_disk_bytes=512ULL*1024*1024;
    lvm::OverviewWorker::SnapRequest nearest;nearest.index=request.index;nearest.source=source;nearest.generation=10;
    nearest.query.budget=budget;nearest.query.channels={{0,false,1,0}};
    nearest.query.target_time=1007;nearest.query.target_value=1007%17;
    const auto wait_snap=[&] {
        std::optional<lvm::OverviewWorker::SnapResult> point;const auto started=std::chrono::steady_clock::now();
        while(!(point=worker.take_snap_result()) && std::chrono::steady_clock::now()-started<std::chrono::seconds(15))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return point;
    };
    worker.submit_snap(nearest);request.generation=11;worker.submit(request);
    auto point=wait_snap();result=wait();
    check(point && !point->error && point->point.found && point->point.sample==1007 && point->point.value==1007%17 &&
          result && !result->error && result->generation==11,
          "one I/O thread completes independent original Snap and viewport slots without discarding either result");
    result.reset();nearest.generation=12;worker.submit_snap(nearest);worker.cancel_snap();
    nearest.generation=13;nearest.query.target_time=199999;nearest.query.target_value=199999%17;worker.submit_snap(nearest);point=wait_snap();
    check(point && point->generation==13 && !point->error && point->point.sample==199999 && budget->reserved()==0,
          "Snap replacement publishes only the latest original point and releases working memory");
    nearest.source=lvm::RecordingSource::from_file(path,{0,199999},{1,2});worker.submit_snap(nearest);point=wait_snap();
    check(point && point->error==lvm::RecordingError::InvalidRequest && !point->point.found,"Snap worker rejects an index of another source descriptor");
    nearest.source=source;
    request.source=lvm::RecordingSource::from_file(path,{0,199999},{1,2});worker.submit(request);result=wait();
    check(result && result->error==lvm::RecordingError::InvalidRequest,
          "background index cannot silently read a different source descriptor");
    request.source=source;{std::ofstream out(path,std::ios::app);out<<"# source changed\n";}
    worker.submit(request);result=wait();
    check(result && result->error==lvm::RecordingError::SourceChanged && !result->view,
          "changed source produces a typed background error");
    worker.submit_snap(nearest);point=wait_snap();
    check(point && point->error==lvm::RecordingError::SourceChanged && !point->point.found,"changed-source Snap has no partial original result");
    worker.cancel();check(!worker.take_result() && worker.progress().phase==lvm::OverviewWorker::Phase::Idle,
          "background cancellation clears retained output and progress");
    worker.shutdown();worker.shutdown();worker.submit(request);
    worker.submit_snap(nearest);
    check(!worker.take_result() && !worker.take_snap_result() && budget->reserved()==0,"terminal background shutdown is idempotent and ignores further requests");
}
template<class Action> bool rejects(lvm::RecordingError error,Action action) {
    try {action();}catch(const lvm::RecordingFailure& e) {return e.error==error;}
    return false;
}
bool same_point(const lvm::OverviewPoint& a,const lvm::OverviewPoint& b) {
    return a.sample==b.sample && a.time==b.time && a.value==b.value;
}
bool exact_snap(const lvm::RecordingSnapResult& actual,const lvm::RecordingSnapRequest& q,
                const std::vector<double>& time,const std::vector<std::vector<double>>& values) {
    lvm::RecordingSnapResult expected;long double best=std::numeric_limits<long double>::infinity();
    lvm::TimeRange range;q.range.resolve({time.front(),time.back()},range);
    // Same legacy tie order: requested channel first, then original row.
    for(const auto& c:q.channels)for(std::size_t i=0;i<time.size();++i) {
        if(time[i]<range.start || time[i]>range.end || !std::isfinite(values[c.channel][i]))continue;
        const auto y=c.affine ? values[c.channel][i]*c.multiplier+c.offset : values[c.channel][i];
        if(!std::isfinite(y))continue;
        const long double dx=(static_cast<long double>(time[i])-q.target_time)*q.time_scale;
        const long double dy=(static_cast<long double>(y)-q.target_value)*q.value_scale;
        const auto d=dx*dx+dy*dy;
        if(!expected.found || d<best) {
            best=d;expected.found=true;expected.sample=i;expected.time=time[i];expected.value=y;expected.channel=c.channel;
        }
    }
    return actual.found==expected.found && (!actual.found || (actual.sample==expected.sample &&
        actual.time==expected.time && actual.channel==expected.channel && actual.value==expected.value &&
        (actual.value!=0 || std::signbit(actual.value)==std::signbit(expected.value))));
}
bool exact_view(const lvm::OverviewView& view,const std::vector<double>& time,
                const std::vector<std::vector<double>>& values,const std::vector<std::size_t>& channels) {
    std::uint64_t count=0,previous=0;bool first=true;
    for(const auto& b:view.buckets) {
        if(b.channels.size()!=channels.size() || !b.sample_count || b.first_sample+b.sample_count>time.size() ||
           b.first_time!=time[b.first_sample] || b.last_time!=time[b.first_sample+b.sample_count-1] ||
           b.first_time<view.range.start || b.last_time>view.range.end || (!first && b.first_sample!=previous))return false;
        for(std::size_t c=0;c<channels.size();++c) {
            const auto& trace=b.channels[c];lvm::OverviewTrace expected;
            for(auto row=b.first_sample;row<b.first_sample+b.sample_count;++row) {
                const double value=values[channels[c]][row];if(!std::isfinite(value))continue;
                const lvm::OverviewPoint point{row,time[row],value};
                if(!expected.finite_count)expected.first=expected.minimum=expected.maximum=point;
                if(value<expected.minimum.value)expected.minimum=point;
                if(value>expected.maximum.value)expected.maximum=point;
                expected.last=point;++expected.finite_count;
            }
            if(trace.finite_count!=expected.finite_count)return false;
            if(trace.finite_count && (!same_point(trace.first,expected.first) || !same_point(trace.last,expected.last) ||
                !same_point(trace.minimum,expected.minimum) || !same_point(trace.maximum,expected.maximum)))return false;
            const auto ordered=trace.ordered_points();
            for(std::size_t i=1;i<ordered.second;++i)if(ordered.first[i-1].sample>=ordered.first[i].sample)return false;
            if(trace.finite_count==0 && ordered.second!=0)return false;
        }
        count+=b.sample_count;previous=b.first_sample+b.sample_count;first=false;
    }
    const auto lo=std::lower_bound(time.begin(),time.end(),view.range.start),hi=std::upper_bound(time.begin(),time.end(),view.range.end);
    return count==view.sample_count && count==static_cast<std::uint64_t>(hi-lo);
}
}

void test_recording_overview(void (*check)(bool,const std::string&)) {
    worker_checks(check);
    std::printf("test_recording_overview\n");Files files;
    const auto path=files.root/std::filesystem::u8path(u8"запись.txt"),cache=files.root/"overview.bin";
    std::ofstream out(path);out<<"Time\tA\tTime\tB\n"<<std::setprecision(17);
    constexpr std::size_t n=20169;double offset=0;
    for(std::size_t i=0;i<n;++i) {
        if(i==1024 || i==2049 || i==16384)offset+=20;
        const double t=i+offset,a=i==17776 ? -1000 : i==17777 ? 1000 : double(int(i%103)-51);
        out<<t<<'\t';if(i==10 || i==1024)out<<"NaN";else out<<a;
        out<<'\t'<<t<<'\t';if(i==2050)out<<"Inf";else out<<int(i%37)-18;
        out<<'\n';
    }
    out.close();
    auto scan=std::make_shared<lvm::ScanIndex>();double start=0,end=0;std::string error;
    check(lvm::scan_time_bounds(path,start,end,error,nullptr,scan.get()),"overview fixture scans");
    auto source=lvm::RecordingSource::from_file(path,{start,end},{1,3},scan);
    std::vector<double> time;std::vector<std::vector<double>> values(2);
    lvm::RecordingReadRequest read;read.channels={0,1};
    source->replay(read,[&](const auto& t,const auto& v) {
        time.insert(time.end(),t.begin(),t.end());
        for(int c=0;c<2;++c)values[c].insert(values[c].end(),v[c].begin(),v[c].end());
    });
    check(time.size()==n && std::isnan(values[0][10]) && !std::isfinite(values[1][2050]),"overview control uses original logical channels and missing values");
    auto budget=std::make_shared<lvm::MemoryBudget>();budget->configure(32ULL*1024*1024,0,32ULL*1024*1024);
    lvm::OverviewOptions options;options.memory_bytes=2ULL*1024*1024;options.budget=budget;
    std::shared_ptr<const lvm::RecordingOverview> index;
    for(auto base:{std::size_t(3),std::size_t(64),std::size_t(1024)}) {
        index.reset();options.base_rows=base;index=lvm::RecordingOverview::build(source,cache,options);
        check(index->sample_count()==n && index->level_count()>1 && index->disk_bytes()<=options.max_disk_bytes && budget->reserved()==0,
              "disk hierarchy is complete, bounded and releases build admission");
        bool mapping=true;
        for(auto i:{std::size_t(0),std::size_t(1023),std::size_t(1024),std::size_t(2049),n-1}) {
            const auto display=index->stitched_time(time[i]);
            mapping=mapping && display==static_cast<double>(i) && index->stitched_time(display,true)==time[i] &&
                index->stitched_sample_time(i)==display;
        }
        check(mapping,"cached stitching matches the original cadence and is reversible across leaf boundaries");
        const auto selected=lvm::AnalysisRange::selected(time[1023]+.25,time[2050]);
        const auto rows=index->sample_range(selected);
        std::vector<double> timeline;index->replay_timeline(selected,[&](const auto& ts){timeline.insert(timeline.end(),ts.begin(),ts.end());});
        check(rows==std::make_pair(std::uint64_t(1024),std::uint64_t(2051)) &&
              timeline==std::vector<double>(time.begin()+1024,time.begin()+2051),
              "binary timeline keeps exact timestamps and inclusive selection edges across disk pages");
        const auto empty=index->sample_range(lvm::AnalysisRange::selected(time[1023]+.25,time[1024]-.25));
        check(empty.first==empty.second,"an interval wholly inside an outage contains no invented samples");
        lvm::RecordingSnapRequest stitched;stitched.stitched=true;stitched.channels={{1,false,1,0}};
        stitched.target_time=1024;stitched.target_value=values[1][1024];stitched.time_scale=1000000;
        const auto exact_stitched=index->nearest_sample(stitched);
        check(exact_stitched.found && exact_stitched.sample==1024 && exact_stitched.time==time[1024] &&
              exact_stitched.value==values[1][1024],"stitched Snap measures screen distance and returns the original raw sample");
        if(base==1024) {
            lvm::Dataset data;data.ok=true;data.time=time;data.names={"A"};data.channels={values[0]};
            const auto expected=lvm::compute_spectrum(data,0);
            lvm::RecordingReadRequest fft_read;fft_read.channels={0};
            lvm::DiskSpectrumOptions disk;disk.timeline=index;
            lvm::AnalysisResources resources;resources.budget=budget;
            auto actual=lvm::compute_disk_spectrum(source,fft_read,data.names,resources,nullptr,disk);
            bool same=actual.ok==expected.ok && actual.n==expected.n && actual.sample_dt==expected.sample_dt &&
                actual.source_start==expected.source_start && actual.source_end==expected.source_end &&
                actual.resampled==expected.resampled && actual.gaps_ignored==expected.gaps_ignored;
            double difference=0;std::vector<double> amplitudes;actual.read_amplitudes(0,0,actual.bin_count(),amplitudes);
            for(std::size_t i=0;i<amplitudes.size();++i){same=same && actual.frequency(i)==expected.freqs[i];
                difference=std::max(difference,std::fabs(amplitudes[i]-expected.amp[0][i]));}
            check(same && difference<=1e-12,"cached-timeline disk FFT preserves N, full metadata, every frequency and amplitude with NaN/outages");
            std::printf("cached timeline FFT N=%zu: max abs error %.17g\n",time.size(),difference);
            actual={};
            fft_read.range=lvm::AnalysisRange::selected(time[1023]+.25,time[2050]);
            data.time.assign(time.begin()+1024,time.begin()+2051);
            data.channels[0].assign(values[0].begin()+1024,values[0].begin()+2051);
            const auto selected_expected=lvm::compute_spectrum(data,0);
            actual=lvm::compute_recording_spectrum(source,fft_read,data.names,resources,nullptr,index);
            check(actual.ok && actual.n==selected_expected.n && actual.freqs==selected_expected.freqs && actual.amp==selected_expected.amp,
                  "cached sample count also preserves the selected-interval RAM FFT exactly");
        }
        for(int transform=0;transform<3;++transform)for(auto target:{std::size_t(0),std::size_t(10),std::size_t(1024),
                std::size_t(2050),std::size_t(17776),n-1}) {
            lvm::RecordingSnapRequest q;q.channels={{1,false,1,0},{0,transform!=0,transform==1 ? -2.0 : 1e308,3}};
            q.budget=budget;q.target_time=time[target]+.25;q.target_value=transform==1 ? 3 : -5;
            q.time_scale=.25;q.value_scale=7;
            check(exact_snap(index->nearest_sample(q),q,time,values) && budget->reserved()==0,
                  "hierarchy Snap equals a full original screen-distance search with NaN/Inf, affine/overflow and channel ties");
            q.range=lvm::AnalysisRange::selected(std::max(start,time[target]-30),std::min(end,time[target]+30));
            check(exact_snap(index->nearest_sample(q),q,time,values),"Snap clips inclusive original rows to the requested interval, not leaf boundaries");
        }
        lvm::RecordingSnapRequest nearest;nearest.channels={{0,false,1,0}};nearest.target_time=time[17776];
        nearest.target_value=-1000;nearest.time_scale=1;nearest.value_scale=1;nearest.budget=budget;
        const auto exact=index->nearest_sample(nearest);
        check(exact.found && exact.sample==17776 && exact.value==-1000 && exact.rows_read<=2*base && budget->reserved()==0,
              "hierarchy rejection bounds original I/O for an isolated nearest peak");
        for(auto pixels:{std::size_t(1),std::size_t(16),std::size_t(257),std::size_t(4096)}) {
            for(auto range:{lvm::AnalysisRange::entire(),lvm::AnalysisRange::selected(time[37]+.5,time[n-64]-.5),
                            lvm::AnalysisRange::selected(time[2050],time[2050]),lvm::AnalysisRange::selected(5.5,5.6)}) {
                lvm::OverviewRequest request;request.range=range;request.pixel_columns=pixels;
                request.channels={1,0,1};request.memory_bytes=16ULL*1024*1024;request.budget=budget;
                auto view=index->read_view(request);
                check(exact_view(view,time,values,request.channels),"viewport extrema, original positions, finite counts and inclusive edges are exact");
                check(view.memory_reservation && budget->reserved()==view.resident_bytes(),"viewport keeps only actual admitted result storage");
                view={};request.display_density=true;
                auto display=index->read_view(request);
                check(exact_view(display,time,values,request.channels) && display.buckets.size()<=2*display.pixel_columns+2,
                      "display packing preserves original extrema/counts/edges with bounded density");
                bool spacing=true;
                for(const auto& bucket:display.buckets) {
                    double maximum=0;
                    for(auto row=bucket.first_sample+1;row<bucket.first_sample+bucket.sample_count;++row)
                        maximum=std::max(maximum,time[row]-time[row-1]);
                    spacing=spacing && maximum==bucket.maximum_spacing;
                }
                check(spacing && budget->reserved()==display.resident_bytes(),"packing retains exact gap bounds and only its own memory admission");
            }
        }
        std::vector<lvm::OverviewGap> gaps;
        const auto count=index->visit_gaps({start,end},4,[&](auto gap){gaps.push_back(gap);},nullptr,options);
        std::vector<lvm::OverviewGap> expected;
        for(std::size_t i=1;i<time.size();++i)if(time[i]-time[i-1]>4)expected.push_back({time[i-1],time[i]});
        bool same=count==expected.size() && gaps.size()==expected.size();
        for(std::size_t i=0;i<gaps.size() && same;++i)same=gaps[i].left==expected[i].left && gaps[i].right==expected[i].right;
        check(same && budget->reserved()==0,"every gap is streamed in order across leaf, tree and reader boundaries");
        gaps.clear();
        const auto middle=(time[1023]+time[1024])/2;
        index->visit_gaps({middle,middle},4,[&](auto gap){gaps.push_back(gap);});
        check(gaps.size()==1 && gaps[0].left==time[1023] && gaps[0].right==time[1024],"viewport wholly inside a gap retains its original boundaries");
    }
    auto stamp=std::filesystem::last_write_time(cache);
    lvm::RecordingSnapRequest snap;snap.channels={{0,false,1,0}};snap.target_time=time[10];snap.target_value=0;snap.budget=budget;
    snap.target_time=time[137];snap.target_value=values[0][137];
    const auto interior=index->nearest_sample(snap);
    check(interior.found && interior.sample==137 && interior.value==values[0][137],
          "Snap can select an interior original sample that is not a stored leaf extremum or endpoint");
    for(std::size_t i=0;i<n;++i)if(values[0][i]==values[1][i]) {
        snap.range=lvm::AnalysisRange::selected(time[i],time[i]);snap.target_time=time[i];snap.target_value=values[0][i];
        snap.channels={{1,false,1,0},{0,false,1,0}};
        check(index->nearest_sample(snap).channel==1,"equal screen distances retain the first requested channel even with a reverse tree traversal");
        snap.channels={{0,false,1,0},{1,false,1,0}};
        check(index->nearest_sample(snap).channel==0,"changing channel order changes only deterministic Snap tie priority");break;
    }
    snap.channels={{0,false,1,0}};snap.target_time=time[10];snap.target_value=0;
    snap.range=lvm::AnalysisRange::selected(time[10],time[10]);
    check(!index->nearest_sample(snap).found,"Snap on an entirely missing interval returns no finite point");
    snap.range=lvm::AnalysisRange::selected(5.5,5.6);
    check(!index->nearest_sample(snap).found,"Snap inside an empty interval cannot escape to a neighboring leaf or across a gap");
    snap.range=lvm::AnalysisRange::entire();snap.memory_bytes=1;
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{index->nearest_sample(snap);}) && budget->reserved()==0,
          "original Snap refuses insufficient workspace without a partial point");
    snap.memory_bytes=2ULL*1024*1024;snap.channels.assign(50000,{0,false,1,0});
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{index->nearest_sample(snap);}),"Snap aliases are bounded before allocating reader buffers");
    snap.channels={{0,false,1,0}};snap.time_scale=0;
    check(rejects(lvm::RecordingError::InvalidRequest,[&]{index->nearest_sample(snap);}),"invalid screen metric rejected before original I/O");
    snap.time_scale=1;snap.target_value=std::numeric_limits<double>::infinity();
    check(rejects(lvm::RecordingError::InvalidRequest,[&]{index->nearest_sample(snap);}),"nonfinite Snap target rejected");
    snap.target_value=0;snap.channels={{2,false,1,0}};
    check(rejects(lvm::RecordingError::InvalidRequest,[&]{index->nearest_sample(snap);}),"invalid Snap logical channel rejected");
    snap.channels={{0,false,1,0}};std::atomic<bool> snap_cancel{true};
    check(rejects(lvm::RecordingError::Cancelled,[&]{index->nearest_sample(snap,&snap_cancel);}),"cancelled Snap never returns a partial point");
    budget->configure(1,0,1);
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{index->nearest_sample(snap);}),"original Snap obeys shared memory admission");
    budget->configure(32ULL*1024*1024,0,32ULL*1024*1024);
    auto reopened=lvm::RecordingOverview::open_or_build(source,cache,options);
    check(reopened->sample_count()==n && std::filesystem::last_write_time(cache)==stamp,"valid persistent index reopens without rewriting");
    lvm::OverviewRequest request;request.range=lvm::AnalysisRange::selected(time[100],time[160]);request.channels={0,1};request.budget=budget;
    auto zoom=index->read_view(request);bool raw=true;for(const auto& b:zoom.buckets)raw=raw && !b.aggregated();
    check(raw && exact_view(zoom,time,values,request.channels),"deep zoom reads exact original samples rather than the envelope");
    zoom={};
    {
        auto a=index->read_view(request),b=index->read_view(request);
        a=std::move(b);
        check(!b.memory_reservation && budget->reserved()==a.resident_bytes() && exact_view(a,time,values,request.channels),
              "moving over a live viewport transfers only its own admission and releases the previous storage");
        a={};check(budget->reserved()==0,"resetting a viewport releases data and its reservation exactly once");
    }
    request.memory_bytes=2ULL*1024*1024;
    std::vector<std::future<lvm::OverviewView>> views;
    for(int i=0;i<4;++i)views.push_back(std::async(std::launch::async,[&]{return index->read_view(request);}));
    bool concurrent=true;for(auto& future:views) {const auto v=future.get();concurrent=concurrent && exact_view(v,time,values,request.channels);}
    check(concurrent && budget->reserved()==0,"concurrent views share no file cursor or result buffers");
    request.channels.assign(50000,0);
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{index->read_view(request);}) && budget->reserved()==0,
          "an oversized repeated-channel list is refused before copying any traces");
    request.channels.clear();check(index->read_view(request).sample_count==61,"timeline-only view has no channel copies");
    request.channels.assign(7000,0);request.range=lvm::AnalysisRange::selected(time[100],time[100]);
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{index->read_view(request);}) && budget->reserved()==0,
          "aliased channels include candidate/group storage in addition to parser and result before admission");
    request.channels={2};check(rejects(lvm::RecordingError::InvalidRequest,[&]{index->read_view(request);}),"invalid logical channel rejected");
    request.channels={0};request.range=lvm::AnalysisRange::selected(-1,end);
    check(rejects(lvm::RecordingError::InvalidRequest,[&]{index->read_view(request);}),"out-of-recording viewport is not silently clipped");
    request.range=lvm::AnalysisRange::entire();request.pixel_columns=0;
    check(rejects(lvm::RecordingError::InvalidRequest,[&]{index->read_view(request);}),"zero display resolution rejected");
    request.pixel_columns=1000000;
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{index->read_view(request);}) && budget->reserved()==0,"over-detailed view fails without publishing partial data or leaking admission");
    request.display_density=true;
    {auto packed=index->read_view(request);
     check(packed.pixel_columns<request.pixel_columns && exact_view(packed,time,values,request.channels),
           "display-only density adapts to the allowance without changing N or extrema");
     auto moved=std::move(packed);lvm::OverviewView assigned;assigned=std::move(moved);
     check(assigned.pixel_columns>0 && moved.pixel_columns==0 && assigned.sample_count==n,
           "viewport move assignment transfers the effective display density");}
    request.display_density=false;
    request.memory_bytes=1;
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{index->read_view(request);}),"insufficient viewport workspace rejected");
    budget->configure(1,0,1);request.memory_bytes=2ULL*1024*1024;
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{index->read_view(request);}),"shared data ledger prevents unadmitted viewport allocation");
    budget->configure(32ULL*1024*1024,0,32ULL*1024*1024);
    auto denied=options;denied.max_disk_bytes=65536+3*(48+104*2);
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{lvm::RecordingOverview::build(source,cache,denied);}) &&
          std::filesystem::last_write_time(cache)==stamp && budget->reserved()==0,"disk refusal preserves an existing complete cache and releases admission");
    check(rejects(lvm::RecordingError::ResourceLimit,[&]{lvm::RecordingOverview::open_or_build(source,cache,denied);}),"cache hit respects a lowered disk allowance");
    auto swapped=lvm::RecordingSource::from_file(path,{start,end},{3,1},scan);
    check(rejects(lvm::RecordingError::ReadFailure,[&]{lvm::RecordingOverview::open(swapped,cache);}),"persistent cache validates physical channel order");
    const auto other_path=files.root/"other.txt";std::filesystem::copy_file(path,other_path);
    std::filesystem::last_write_time(other_path,source->stamp()->modified);
    auto other=lvm::RecordingSource::from_file(other_path,{start,end},{1,3});
    check(rejects(lvm::RecordingError::ReadFailure,[&]{lvm::RecordingOverview::open(other,cache);}),"same-size and same-mtime other source cannot reuse this cache");
    const auto bad_header=files.root/"bad-header.bin",bad_data=files.root/"bad-data.bin",truncated=files.root/"truncated.bin";
    for(const auto& dest:{bad_header,bad_data,truncated})std::filesystem::copy_file(cache,dest);
    {std::fstream f(bad_header,std::ios::binary|std::ios::in|std::ios::out);f.seekp(30);f.put('\xAB');}
    check(rejects(lvm::RecordingError::ReadFailure,[&]{lvm::RecordingOverview::open(source,bad_header);}),"header corruption detected");
    {std::fstream f(bad_data,std::ios::binary|std::ios::in|std::ios::out);f.seekp(65536+24);f.put('\xAB');}
    auto damaged=lvm::RecordingOverview::open(source,bad_data);
    request.range=lvm::AnalysisRange::selected(time[100],time[160]);request.pixel_columns=1000;
    check(rejects(lvm::RecordingError::ReadFailure,[&]{damaged->read_view(request);}) && budget->reserved()==0,"lazy data checksum prevents a corrupt viewport from being published");
    std::filesystem::resize_file(truncated,index->disk_bytes()-1);
    check(rejects(lvm::RecordingError::ReadFailure,[&]{lvm::RecordingOverview::open(source,truncated);}),"truncated cache rejected before traversal");
    std::atomic<bool> cancel{true};
    check(rejects(lvm::RecordingError::Cancelled,[&]{index->read_view(request,&cancel);}),"cancelled viewport produces no result");
    check(rejects(lvm::RecordingError::Cancelled,[&]{lvm::RecordingOverview::build(source,cache,options,&cancel);}) &&
          std::filesystem::last_write_time(cache)==stamp,"cancelled build leaves the existing cache intact");
    cancel=false;
    auto interrupt=options;bool saw_progress=false;
    interrupt.progress=[&](auto rows,auto bytes){saw_progress=rows>0 && bytes>65536;cancel=true;};
    check(rejects(lvm::RecordingError::Cancelled,[&]{lvm::RecordingOverview::build(source,cache,interrupt,&cancel);}) &&
          saw_progress && std::filesystem::last_write_time(cache)==stamp && budget->reserved()==0,
          "cancellation after writing real index data leaves no partial cache or reservation");
    cancel=false;
    check(rejects(lvm::RecordingError::Cancelled,[&]{index->visit_gaps({start,end},4,[&](auto){cancel=true;},&cancel,options);}) &&
          budget->reserved()==0,"gap traversal cancellation is checked after callbacks and releases admission");
    check(rejects(lvm::RecordingError::InvalidRequest,[&]{lvm::RecordingOverview::build(source,path,options);}),"index cannot overwrite its source recording");
    std::size_t scratch_count=0;for(const auto& item:std::filesystem::directory_iterator(files.root))if(item.is_directory())++scratch_count;
    check(scratch_count==0,"successful and failed builds remove their private scratch directories");
    const auto tiny=files.root/"single.txt";{std::ofstream f(tiny);f<<"Time\tA\n4\t9\n";}
    auto single=lvm::RecordingSource::from_file(tiny,{4,4},{1});
    auto one=lvm::RecordingOverview::build(single,files.root/"single.bin",options);
    request.range=lvm::AnalysisRange::entire();request.channels={0};request.pixel_columns=1;
    {
        const auto one_view=one->read_view(request);
        check(one->sample_count()==1 && one->level_count()==1 && one_view.buckets.size()==1 &&
              one_view.buckets[0].channels[0].maximum.value==9,"single-sample recording has a valid one-level overview");
    }
    check(rejects(lvm::RecordingError::SourceChanged,[&]{
        index->visit_gaps({start,end},4,[&](auto){std::ofstream f(path,std::ios::app);f<<"# changed\n";});
    }),"source modification during gap callback invalidates the read");
    check(rejects(lvm::RecordingError::SourceChanged,[&]{index->read_view(request);}),"stale descriptor rejects subsequent overview reads");
    snap.range=lvm::AnalysisRange::entire();
    check(rejects(lvm::RecordingError::SourceChanged,[&]{index->nearest_sample(snap);}),"stale descriptor rejects original-source Snap without a saved envelope fallback");
    auto current=lvm::RecordingSource::from_file(path,{start,end},{1,3});
    auto change=options;change.progress=[&](auto,auto){std::ofstream f(path,std::ios::app);f<<"# changed during build\n";};
    check(rejects(lvm::RecordingError::SourceChanged,[&]{lvm::RecordingOverview::build(current,cache,change);}) &&
          std::filesystem::last_write_time(cache)==stamp && budget->reserved()==0,
          "source change after writing index data preserves the previous cache");
}
