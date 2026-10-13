#include "disk_spectrum.hpp"
#include "recording_overview.hpp"
#include "frf_stream.hpp"
#include "sampling.hpp"
#include "fft.hpp"
#include <fstream>
#include <complex>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <limits>

namespace lvm {
namespace {
using Complex=std::complex<double>;
constexpr std::size_t block=32768;
constexpr double pi=3.14159265358979323846;
void check(const std::atomic<bool>* cancel) {
    if(cancel && cancel->load())throw RecordingFailure(RecordingError::Cancelled,"Operation cancelled.");
}
void failure() {throw RecordingFailure(RecordingError::ResourceLimit,"Cannot read/write FFT temporary disk. Check free space and permissions.");}
struct Workspace {
    std::filesystem::path root;
    std::vector<std::filesystem::path> files;
    Workspace(const std::filesystem::path& parent,std::uint64_t required,std::uint64_t limit) {
        if(required>limit)throw RecordingFailure(RecordingError::ResourceLimit,"FFT exceeds the temporary disk allowance.");
        const auto directory=parent.empty() ? std::filesystem::temp_directory_path() : parent;
        std::error_code ec;const auto space=std::filesystem::space(directory,ec);
        if(ec || space.available<required+64ULL*1024*1024)throw RecordingFailure(RecordingError::ResourceLimit,"Insufficient free space for exact disk FFT.");
        static std::atomic<std::uint64_t> serial{0};
        for(unsigned i=0;i<128;++i) {
            root=directory/("AMSignal-fft-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++serial));
            if(std::filesystem::create_directory(root,ec))return;
            if(ec && ec!=std::errc::file_exists)failure();
            ec.clear();
        }
        failure();
    }
    std::filesystem::path file() {auto p=root/(std::to_string(files.size())+".bin");files.push_back(p);return p;}
    void keep(const std::vector<std::filesystem::path>& retained) {
        for(const auto& p:files)if(std::find(retained.begin(),retained.end(),p)==retained.end()) {std::error_code ec;std::filesystem::remove(p,ec);}
    }
    ~Workspace() {
        // Only individually created files, never recursive deletion or foreign files.
        for(const auto& p:files){std::error_code ec;std::filesystem::remove(p,ec);}
        std::error_code ec;std::filesystem::remove(root,ec);
    }
};
template<class T> class Array {
    std::fstream stream;
public:
    std::filesystem::path path;
    std::size_t size;
    Array(Workspace& workspace,std::size_t count):path(workspace.file()),size(count) {
        stream.open(path,std::ios::binary|std::ios::in|std::ios::out|std::ios::trunc);
        if(!stream)failure();
        if(count){stream.seekp(static_cast<std::streamoff>(count*sizeof(T)-1));stream.put(0);if(!stream)failure();}
    }
    void read(std::size_t first,T* values,std::size_t count) {
        if(first>size || count>size-first)throw std::out_of_range("Disk FFT array bounds.");
        stream.seekg(static_cast<std::streamoff>(first*sizeof(T)));stream.read(reinterpret_cast<char*>(values),count*sizeof(T));if(!stream)failure();
    }
    void write(std::size_t first,const T* values,std::size_t count) {
        if(first>size || count>size-first)throw std::out_of_range("Disk FFT array bounds.");
        stream.seekp(static_cast<std::streamoff>(first*sizeof(T)));stream.write(reinterpret_cast<const char*>(values),count*sizeof(T));if(!stream)failure();
    }
    void close() {if(stream.is_open()){stream.close();if(!stream)failure();}}
};
template<class T> class Cursor {
    Array<T>& array;std::vector<T> buffer;std::size_t first=std::numeric_limits<std::size_t>::max();
public:
    explicit Cursor(Array<T>& a):array(a),buffer(block){}
    T at(std::size_t i) {if(first==std::numeric_limits<std::size_t>::max() || i<first || i>=first+buffer.size()) {
        first=(i/block)*block;buffer.resize(std::min(block,array.size-first));array.read(first,buffer.data(),buffer.size());
    }return buffer[i-first];}
};
std::size_t reverse_bits(std::size_t x,unsigned bits) {std::size_t r=0;for(unsigned i=0;i<bits;++i){r=(r<<1)|(x&1);x>>=1;}return r;}
void radix(Array<Complex>& a,Workspace& workspace,bool inverse,const std::atomic<bool>* cancel) {
    const auto n=a.size;unsigned bits=0;for(auto k=n;k>1;k>>=1)++bits;
    // Tiled external bit reversal: bounded transpose, contiguous row I/O.
    const unsigned low_bits=bits/2,high_bits=bits-low_bits;
    const std::size_t cols=std::size_t(1)<<low_bits,rows=std::size_t(1)<<high_bits,tile=128;
    {
        Array<Complex> reordered(workspace,n);std::vector<Complex> values(tile*tile),line(tile);
        for(std::size_t h=0;h<rows;h+=tile)for(std::size_t l=0;l<cols;l+=tile) {
            check(cancel);const auto nr=std::min(tile,rows-h),nc=std::min(tile,cols-l);
            for(std::size_t u=0;u<nr;++u)a.read(reverse_bits(h+u,high_bits)*cols+l,values.data()+u*tile,nc);
            for(std::size_t v=0;v<nc;++v) {
                for(std::size_t u=0;u<nr;++u)line[u]=values[u*tile+v];
                reordered.write(reverse_bits(l+v,low_bits)*rows+h,line.data(),nr);
            }
        }
        std::vector<Complex> copy(block);
        for(std::size_t at=0;at<n;at+=block){check(cancel);const auto count=std::min(block,n-at);reordered.read(at,copy.data(),count);a.write(at,copy.data(),count);}
        reordered.close();std::error_code ec;std::filesystem::remove(reordered.path,ec);
    }
    std::vector<Complex> left(block),right(block);
    const auto local=std::min(block,n);
    // Short stages stay in one tile. Operation and w recurrence match fft_radix2.
    for(std::size_t at=0;at<n;at+=local) {
        check(cancel);a.read(at,left.data(),local);
        for(std::size_t len=2;len<=local;len*=2) {
            const double angle=2*pi/static_cast<double>(len)*(inverse ? 1 : -1);
            const Complex wlen(std::cos(angle),std::sin(angle));
            for(std::size_t i=0;i<local;i+=len) {Complex w(1,0);
                for(std::size_t k=0;k<len/2;++k){const auto u=left[i+k],v=left[i+k+len/2]*w;left[i+k]=u+v;left[i+k+len/2]=u-v;w*=wlen;}
            }
        }
        a.write(at,left.data(),local);
    }
    for(std::size_t len=local*2;len<=n;len*=2) {
        const auto half=len/2;const double angle=2*pi/static_cast<double>(len)*(inverse ? 1 : -1);
        const Complex wlen(std::cos(angle),std::sin(angle));
        for(std::size_t i=0;i<n;i+=len) {Complex w(1,0);
            for(std::size_t at=0;at<half;at+=block){check(cancel);const auto count=std::min(block,half-at);
                a.read(i+at,left.data(),count);a.read(i+half+at,right.data(),count);
                for(std::size_t k=0;k<count;++k){const auto u=left[k],v=right[k]*w;left[k]=u+v;right[k]=u-v;w*=wlen;}
                a.write(i+at,left.data(),count);a.write(i+half+at,right.data(),count);
            }
        }
    }
    if(inverse)for(std::size_t at=0;at<n;at+=block){check(cancel);const auto count=std::min(block,n-at);a.read(at,left.data(),count);
        for(std::size_t k=0;k<count;++k)left[k]/=static_cast<double>(n);
        a.write(at,left.data(),count);}
}
Complex chirp(std::size_t k,std::size_t n) {
    const auto square=(static_cast<unsigned long long>(k)*k)%(2ULL*n);
    const double angle=-pi*static_cast<double>(square)/static_cast<double>(n);return {std::cos(angle),std::sin(angle)};
}
void transform(Array<Complex>& signal,Workspace& workspace,const std::atomic<bool>* cancel) {
    const auto n=signal.size;
    if(!(n&(n-1))){radix(signal,workspace,false,cancel);return;}
    std::size_t m=1;while(m<2*n-1)m*=2;
    Array<Complex> a(workspace,m),kernel(workspace,m);std::vector<Complex> values(block),other(block);
    for(std::size_t at=0;at<m;at+=block){check(cancel);const auto count=std::min(block,m-at);
        std::fill(values.begin(),values.begin()+count,Complex{});
        std::fill(other.begin(),other.begin()+count,Complex{});
        if(at<n){const auto input=std::min(count,n-at);signal.read(at,values.data(),input);for(std::size_t k=0;k<input;++k)values[k]*=chirp(at+k,n);}
        for(std::size_t k=0;k<count;++k){const auto i=at+k;if(i<n)other[k]=std::conj(chirp(i,n));else if(i>m-n)other[k]=std::conj(chirp(m-i,n));}
        a.write(at,values.data(),count);kernel.write(at,other.data(),count);
    }
    radix(kernel,workspace,false,cancel);radix(a,workspace,false,cancel);
    for(std::size_t at=0;at<m;at+=block){check(cancel);const auto count=std::min(block,m-at);a.read(at,values.data(),count);kernel.read(at,other.data(),count);
        for(std::size_t k=0;k<count;++k)values[k]*=other[k];
        a.write(at,values.data(),count);}
    radix(a,workspace,true,cancel);
    for(std::size_t at=0;at<n;at+=block){check(cancel);const auto count=std::min(block,n-at);a.read(at,values.data(),count);
        for(std::size_t k=0;k<count;++k)values[k]*=chirp(at+k,n);
        signal.write(at,values.data(),count);}
    a.close();kernel.close();std::error_code ec;std::filesystem::remove(a.path,ec);std::filesystem::remove(kernel.path,ec);
}
}
struct DiskSpectrum::Storage {std::shared_ptr<Workspace> workspace;std::vector<std::filesystem::path> channels;};
void DiskSpectrum::read(std::size_t channel,std::size_t first,std::size_t count,std::vector<double>& values) const {
    if(!storage || channel>=storage->channels.size() || first>bins || count>bins-first)throw std::out_of_range("Spectrum disk range.");
    values.resize(count);std::ifstream in(storage->channels[channel],std::ios::binary);in.seekg(static_cast<std::streamoff>(first*8));
    in.read(reinterpret_cast<char*>(values.data()),count*8);if(!in)failure();
}
std::vector<std::pair<std::size_t,double>> DiskSpectrum::display_points(std::size_t channel,std::size_t first,std::size_t count,std::size_t pixels) const {
    pixels=std::min<std::size_t>(8192,pixels);
    if(!pixels || !count)return {};
    if(first>bins || count>bins-first)throw std::out_of_range("Spectrum display range.");
    std::vector<Bucket> packed(pixels);
    const auto add=[&](std::size_t bin,double value) {
        const auto pixel=std::min(pixels-1,static_cast<std::size_t>(static_cast<long double>(bin-first)*pixels/count));
        auto& b=packed[pixel];
        if(!b.count){b.first=bin;b.first_value=b.minimum=b.maximum=value;b.minimum_bin=b.maximum_bin=bin;}
        if(value<b.minimum){b.minimum=value;b.minimum_bin=bin;}
        if(value>b.maximum){b.maximum=value;b.maximum_bin=bin;}
        b.count=bin-b.first+1;b.last_value=value;
    };
    const auto& summaries=overview.at(channel);
    // At overview scale cached extrema retain exact bin positions; zoom reads originals.
    if(first==0 && count==bins && !summaries.empty() && count/pixels>=summaries.front().count) {
        for(const auto& b:summaries)if(b.first>=first && b.first+b.count<=first+count) {
            std::vector<std::pair<std::size_t,double>> p={{b.first,b.first_value},{b.minimum_bin,b.minimum},{b.maximum_bin,b.maximum},{b.first+b.count-1,b.last_value}};
            std::sort(p.begin(),p.end());for(const auto& point:p)add(point.first,point.second);
        }
        // Edge buckets always use original amplitudes.
        std::vector<double> values;
        for(const auto& b:summaries)if(b.first<first+count && b.first+b.count>first && !(b.first>=first && b.first+b.count<=first+count)) {
            const auto lo=std::max(first,b.first),hi=std::min(first+count,b.first+b.count);read(channel,lo,hi-lo,values);
            for(std::size_t k=0;k<values.size();++k)add(lo+k,values[k]);
        }
    } else {
        std::vector<double> values;
        for(std::size_t at=first;at<first+count;at+=block){read(channel,at,std::min(block,first+count-at),values);
            for(std::size_t k=0;k<values.size();++k)add(at+k,values[k]);}
    }
    std::vector<std::pair<std::size_t,double>> points;points.reserve(pixels*4);
    for(const auto& b:packed)if(b.count){std::vector<std::pair<std::size_t,double>> p={{b.first,b.first_value},{b.minimum_bin,b.minimum},{b.maximum_bin,b.maximum},{b.first+b.count-1,b.last_value}};
        std::sort(p.begin(),p.end());p.erase(std::unique(p.begin(),p.end()),p.end());points.insert(points.end(),p.begin(),p.end());}
    return points;
}
std::size_t Spectrum::bin_count() const {return disk ? disk->bins : freqs.size();}
double Spectrum::frequency(std::size_t bin) const {return disk ? static_cast<double>(bin)/(static_cast<double>(n)*sample_dt) : freqs.at(bin);}
std::pair<std::size_t,std::size_t> Spectrum::bin_range(double start,double end) const {
    if(!disk)return {std::lower_bound(freqs.begin(),freqs.end(),start)-freqs.begin(),std::upper_bound(freqs.begin(),freqs.end(),end)-freqs.begin()};
    const auto bound=[&](double value,bool upper){std::size_t lo=0,hi=bin_count();while(lo<hi){const auto mid=lo+(hi-lo)/2;
        if(frequency(mid)<value || (upper && frequency(mid)==value))lo=mid+1;else hi=mid;}return lo;};
    return {bound(start,false),bound(end,true)};
}
void Spectrum::read_amplitudes(std::size_t channel,std::size_t first,std::size_t count,std::vector<double>& values) const {
    if(disk){disk->read(channel,first,count,values);return;}
    const auto& input=amp.at(channel);if(first>input.size() || count>input.size()-first)throw std::out_of_range("Spectrum range.");
    values.assign(input.begin()+first,input.begin()+first+count);
}
Spectrum compute_disk_spectrum(std::shared_ptr<const RecordingSource> source,RecordingReadRequest read,
    std::vector<std::string> names,const AnalysisResources& resources,const std::atomic<bool>* cancel,const DiskSpectrumOptions& options) {
    Spectrum spec;check(cancel);if(!source || names.size()!=read.channels.size()) {spec.error="Inconsistent dataset dimensions.";return spec;}
    // Explicit fixed resident allowance, including cadence sorting (8 MiB), I/O,
    // transpose/FFT tiles and interpolation cursors. Outputs remain on disk.
    constexpr std::uint64_t working=24ULL*1024*1024;
    if(working+(resources.cached_plans_accounted ? 0 : fft_cached_plan_bytes())>resources.memory_bytes ||
       (resources.budget && !(spec.memory_reservation=resources.budget->reserve(working)))) {
        spec.error="FFT resources are insufficient for the bounded disk path.";return spec;
    }
    read.block_rows=4096;read.block_bytes=1024*1024;
    auto timeline=read;timeline.channels.clear();lvm::FrfSource time_source;
    const auto cached=options.timeline;
    if(cached && (cached->source()->path()!=source->path() || cached->source()->bounds().start!=source->bounds().start ||
       cached->source()->bounds().end!=source->bounds().end || !source->stamp() ||
       cached->source()->stamp()->bytes!=source->stamp()->bytes || cached->source()->stamp()->modified!=source->stamp()->modified))
        throw RecordingFailure(RecordingError::InvalidRequest,"FFT timeline does not match its source.");
    time_source.replay=[&](const auto& visitor,const auto* flag){if(cached)cached->replay_timeline(read.range,[&](const auto& t){visitor(t,{});},flag);
        else source->replay(timeline,visitor,flag);};
    const auto rows=cached ? cached->sample_range(read.range) : std::pair<std::uint64_t,std::uint64_t>{0,0};
    const auto n=cached ? rows.second-rows.first : source->replay(timeline,[](const auto&,const auto&){},cancel);
    if(n<4){spec.error="Not enough samples for FFT (need at least 4).";return spec;}
    if(n>static_cast<std::size_t>(std::numeric_limits<int>::max())){spec.error="FFT exceeds the supported sample count.";return spec;}
    std::size_t m=1;while(m<2*n-1)m*=2;
    const long double required=(n&(n-1) ? 48.0L*m+40.0L*n : 56.0L*n)+8.0L*(n/2+1)*names.size();
    if(required>options.disk_bytes)throw RecordingFailure(RecordingError::ResourceLimit,"FFT exceeds the temporary disk allowance.");
    auto parent=options.temporary_directory;
    if(parent.empty() && source->file_backed()) {
        std::error_code ec;const auto available=std::filesystem::space(std::filesystem::temp_directory_path(),ec);
        if(ec || available.available<required+64ULL*1024*1024)parent=source->path().parent_path();
    }
    auto workspace=std::make_shared<Workspace>(parent,static_cast<std::uint64_t>(required),options.disk_bytes);
    const auto meta=inspect_frf_timeline(time_source,cancel,static_cast<std::size_t>(n));
    if(meta.error!=FrfError::None){spec.error=meta.error==FrfError::TooShort ? "Not enough samples for FFT (need at least 4)." : "FFT requires a finite, strictly increasing time axis.";return spec;}
    if(meta.source_count!=n)throw RecordingFailure(RecordingError::SourceChanged,"Recording changed during FFT preparation.");
    spec.n=static_cast<int>(n);spec.source_start=meta.source_start;spec.source_end=meta.source_end;
    auto storage=std::make_shared<DiskSpectrum::Storage>();storage->workspace=workspace;
    auto disk=std::make_shared<DiskSpectrum>();disk->storage=storage;disk->bins=n/2+1;
    const std::uint64_t summaries_bytes=names.size()*2048ULL*sizeof(DiskSpectrum::Bucket)+4096;
    if(summaries_bytes>working/2)throw RecordingFailure(RecordingError::ResourceLimit,"Too many channels for the disk spectrum overview allowance.");
    {
        Array<long double> offsets(*workspace,n);std::size_t row=0;double previous=0;long double offset=0;
        time_source.replay([&](const auto& time,const auto&) {std::vector<long double> values(time.size());
            for(std::size_t i=0;i<time.size();++i){if(row+i){double step=time[i]-previous;if(step>meta.sample_dt*sampling_gap_factor){step=meta.sample_dt;spec.gaps_ignored=true;}offset+=step;}
                values[i]=offset;previous=time[i];}offsets.write(row,values.data(),values.size());row+=values.size();},cancel);
        if(row!=n)throw RecordingFailure(RecordingError::SourceChanged,"Recording changed during FFT preparation.");
        const long double grid_step=offset/(n-1);spec.sample_dt=static_cast<double>(grid_step);spec.nyquist=.5/spec.sample_dt;
        if(!std::isfinite(spec.sample_dt) || spec.sample_dt<=0 || !std::isfinite(spec.nyquist)) {spec.error="Invalid sample spacing.";return spec;}
        const long double tolerance=std::max(2*std::numeric_limits<long double>::epsilon()*offset,static_cast<long double>(spec.sample_dt)*1e-6L);
        Cursor<long double> axis(offsets);
        for(std::size_t i=1;i<n-1;++i){if((i&65535)==0)check(cancel);if(std::fabs(axis.at(i)-i*grid_step)>tolerance){spec.resampled=true;break;}}
        for(std::size_t c=0;c<read.channels.size();++c) {
            check(cancel);Array<double> original(*workspace,n);auto channel=read;channel.channels={read.channels[c]};row=0;long double sum=0;std::size_t finite=0;bool infinite=false;
            source->replay(channel,[&](const auto& time,const auto& values){if(time.size()>n-row)throw RecordingFailure(RecordingError::SourceChanged,"Recording changed during FFT preparation.");
                for(auto v:values[0]){if(std::isinf(v))infinite=true;else if(std::isfinite(v)){sum+=v;++finite;}}
                original.write(row,values[0].data(),values[0].size());row+=values[0].size();},cancel);
            if(row!=n)throw RecordingFailure(RecordingError::SourceChanged,"Recording changed during FFT preparation.");
            if(infinite){spec.error="Channel contains infinite values.";return spec;}if(finite<4)continue;
            const double fill=sum/static_cast<double>(finite);Cursor<double> input(original);Cursor<long double> positions(offsets);
            const auto value=[&](std::size_t i){const auto v=input.at(i);return std::isnan(v) ? fill : v;};
            Array<Complex> signal(*workspace,n);std::vector<Complex> values(block);long double total=0;std::size_t right=1;
            for(std::size_t at=0;at<n;at+=block){check(cancel);const auto count=std::min(block,n-at);
                for(std::size_t k=0;k<count;++k){const auto i=at+k;double clean=value(i);
                    if(spec.resampled && i>0 && i<n-1){const long double target=i*grid_step;while(right<n-1 && positions.at(right)<target){++right;if((right&65535)==0)check(cancel);}
                        const auto left=positions.at(right-1),interval=positions.at(right)-left;const auto weight=std::clamp((target-left)/interval,0.0L,1.0L);
                        clean=static_cast<double>((1-weight)*value(right-1)+weight*value(right));}
                    values[k]={clean,0};total+=clean;}signal.write(at,values.data(),count);}
            const double mean=total/static_cast<double>(n);
            for(std::size_t at=0;at<n;at+=block){check(cancel);const auto count=std::min(block,n-at);signal.read(at,values.data(),count);
                for(std::size_t k=0;k<count;++k)values[k]-=mean;
                signal.write(at,values.data(),count);}
            transform(signal,*workspace,cancel);Array<double> amplitudes(*workspace,disk->bins);std::vector<double> output(block);double maximum=0;
            const auto overview_step=(disk->bins+2047)/2048;
            std::vector<DiskSpectrum::Bucket> summaries;summaries.reserve(2048);
            const double interior=2.0/static_cast<double>(n),edge=1.0/static_cast<double>(n);
            for(std::size_t at=0;at<disk->bins;at+=block){check(cancel);const auto count=std::min(block,disk->bins-at);signal.read(at,values.data(),count);
                for(std::size_t k=0;k<count;++k){const auto i=at+k;output[k]=((i==0 || (!(n%2) && i==n/2)) ? edge : interior)*std::abs(values[k]);
                    if(!std::isfinite(output[k])){spec.error="FFT overflow; reduce signal magnitude.";return spec;}maximum=std::max(maximum,output[k]);
                    if(i%overview_step==0){DiskSpectrum::Bucket b;b.first=b.minimum_bin=b.maximum_bin=i;b.first_value=b.minimum=b.maximum=output[k];summaries.push_back(b);}
                    auto& b=summaries.back();++b.count;b.last_value=output[k];
                    if(output[k]<b.minimum){b.minimum=output[k];b.minimum_bin=i;}
                    if(output[k]>b.maximum){b.maximum=output[k];b.maximum_bin=i;}}
                amplitudes.write(at,output.data(),count);}
            amplitudes.close();storage->channels.push_back(amplitudes.path);disk->maxima.push_back(maximum);disk->overview.push_back(std::move(summaries));
            spec.names.push_back(names[c]);spec.source_channels.push_back(c);
            signal.close();original.close();std::error_code ec;std::filesystem::remove(signal.path,ec);std::filesystem::remove(original.path,ec);
        }
        offsets.close();
    }
    check(cancel);source->check_current();workspace->keep(storage->channels);
    disk->disk_bytes=storage->channels.size()*disk->bins*8;spec.disk=std::move(disk);spec.ok=!spec.names.empty();spec.recording_source=source;
    if(!spec.ok)spec.error="No channel had enough finite samples for FFT.";
    // Work arrays are gone. Keep only the admitted bounded display summaries.
    if(spec.memory_reservation){spec.memory_reservation->shrink_to(summaries_bytes);std::const_pointer_cast<DiskSpectrum>(spec.disk)->memory_reservation=std::move(spec.memory_reservation);}
    return spec;
}

Spectrum compute_recording_spectrum(std::shared_ptr<const RecordingSource> source,RecordingReadRequest read,
    std::vector<std::string> names,const AnalysisResources& resources,const std::atomic<bool>* cancel,
    std::shared_ptr<const RecordingOverview> timeline) {
    check(cancel);auto time=read;time.channels.clear();
    source->check_current();
    if(timeline && (timeline->source()->path()!=source->path() || timeline->source()->bounds().start!=source->bounds().start ||
       timeline->source()->bounds().end!=source->bounds().end || !source->stamp() ||
       timeline->source()->stamp()->bytes!=source->stamp()->bytes || timeline->source()->stamp()->modified!=source->stamp()->modified))
        throw RecordingFailure(RecordingError::InvalidRequest,"FFT timeline does not match its source.");
    DiskSpectrumOptions disk_options;disk_options.timeline=timeline;
    const auto rows=timeline ? timeline->sample_range(read.range) : std::pair<std::uint64_t,std::uint64_t>{0,0};
    const auto n=timeline ? rows.second-rows.first : source->replay(time,[](const auto&,const auto&){},cancel);
    if(n>static_cast<std::size_t>(std::numeric_limits<int>::max())){Spectrum result;result.error="FFT exceeds the supported sample count.";return result;}
    long double scratch=16.0L*n;
    if(n && (n&(n-1))){std::size_t m=1;while(m<2*n-1)m*=2;scratch=48.0L*n+32.0L*m;}
    const long double input_bytes=n*(read.channels.size()+1.0L)*8;
    const long double required=input_bytes+2*1024.0L*1024+n*(8.0L+sizeof(long double))+
        (n/2+1)*(read.channels.size()+1.0L)*8+scratch;
    const auto numerical_cap=resources.memory_bytes;
    const auto available=resources.budget ? std::min<std::uint64_t>(numerical_cap,resources.budget->available()) : numerical_cap;
    if(n<=static_cast<std::size_t>(std::numeric_limits<int>::max()) && required<=available) {
        try {
        auto reservation=resources.budget ? resources.budget->reserve(static_cast<std::uint64_t>(input_bytes+1024*1024)) : nullptr;
        if(resources.budget && !reservation)return compute_disk_spectrum(source,read,names,resources,cancel,disk_options);
        Dataset data;data.ok=true;data.names=names;data.time.reserve(n);data.channels.resize(read.channels.size());
        for(auto& column:data.channels)column.reserve(n);
        source->replay(read,[&](const auto& timeline,const auto& values){if(timeline.size()>n-data.rows())throw RecordingFailure(RecordingError::SourceChanged,"Recording changed during FFT preparation.");
            data.time.insert(data.time.end(),timeline.begin(),timeline.end());
            for(std::size_t c=0;c<values.size();++c)data.channels[c].insert(data.channels[c].end(),values[c].begin(),values[c].end());},cancel);
        if(data.rows()!=n)throw RecordingFailure(RecordingError::SourceChanged,"Recording changed during FFT preparation.");
        auto result=compute_spectrum(data,0,cancel,resources);source->check_current();
        if(!result.ok && result.error.rfind("FFT resources are insufficient",0)==0) {
            data={};reservation.reset();result={};
            return compute_disk_spectrum(source,read,names,resources,cancel,disk_options);
        }
        result.recording_source=source;return result;
        } catch(const std::bad_alloc&) {
            // Stack-owned Dataset/reservation are already gone before retry.
            return compute_disk_spectrum(source,read,names,resources,cancel,disk_options);
        }
    }
    return compute_disk_spectrum(std::move(source),std::move(read),std::move(names),resources,cancel,disk_options);
}
}
