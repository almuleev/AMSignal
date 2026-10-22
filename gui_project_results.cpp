#include "gui_project_results.hpp"
#include "gui_analysis_source.hpp"
#include "gui_documents.hpp"
#include "gui_frf.hpp"
#include "gui_processing.hpp"
#include "gui_resources.hpp"
#include "gui_spectrum.hpp"
#include "gui_state.hpp"
#include "gui_text.hpp"
#include <cstring>
#include <stdexcept>

namespace gui {
namespace {
// Comments remain compatible with the existing LVM readers and scan indexes.
// Keep both decoded arrays and the encoded metadata small even for disk FFT.
constexpr std::size_t max_cache_bytes=4*1024*1024;
using Bytes=std::vector<unsigned char>;
struct InvalidCache : std::runtime_error {InvalidCache():std::runtime_error("Invalid project analysis cache") {}};
void expect(bool value) {if(!value)throw InvalidCache();}
struct Writer {
    Bytes bytes;
    void integer(std::uint64_t value) {
        expect(bytes.size()+8<=max_cache_bytes);
        for(int k=0;k<8;++k) {bytes.push_back(static_cast<unsigned char>(value));value>>=8;}
    }
    void real(double value) {std::uint64_t bits;std::memcpy(&bits,&value,8);integer(bits);}
    void text(const std::string& value) {
        integer(value.size());expect(value.size()<=max_cache_bytes-bytes.size());
        bytes.insert(bytes.end(),value.begin(),value.end());
    }
    void doubles(const std::vector<double>& values) {
        integer(values.size());expect(values.size()<=(max_cache_bytes-bytes.size())/8);
        for(double value:values)real(value);
    }
    void mask(const std::vector<unsigned char>& values) {
        integer(values.size());expect(values.size()<=max_cache_bytes-bytes.size());
        bytes.insert(bytes.end(),values.begin(),values.end());
    }
};
struct Reader {
    const Bytes& bytes;std::size_t at=0;
    std::uint64_t integer() {
        expect(bytes.size()-at>=8);std::uint64_t value=0;
        for(int k=0;k<8;++k)value|=std::uint64_t(bytes[at++])<<(8*k);
        return value;
    }
    double real() {const auto bits=integer();double value;std::memcpy(&value,&bits,8);return value;}
    bool flag() {const auto value=integer();expect(value<=1);return value!=0;}
    std::string text() {
        const auto count=integer();expect(count<=bytes.size()-at);
        std::string value(bytes.begin()+at,bytes.begin()+at+count);at+=count;return value;
    }
    std::vector<double> doubles() {
        const auto count=integer();expect(count<=(bytes.size()-at)/8);
        std::vector<double> values(static_cast<std::size_t>(count));for(auto& value:values)value=real();return values;
    }
    std::vector<unsigned char> mask() {
        const auto count=integer();expect(count<=bytes.size()-at);
        std::vector<unsigned char> values(bytes.begin()+at,bytes.begin()+at+count);at+=count;
        for(auto value:values)expect(value<=1);
        return values;
    }
};
std::uint64_t checksum(const Bytes& bytes) {
    std::uint64_t hash=14695981039346656037ULL;
    for(auto value:bytes) {hash^=value;hash*=1099511628211ULL;}return hash;
}
// Match every numerical setting. Display names and viewports do not change H/FFT.
std::string numerical_key() {
    std::ostringstream out;
    const auto bounds=analysis_recording_bounds();
    out<<to_utf8(APP_VERSION_W)<<'\n'<<g.ds.channel_count()<<':'<<g.ds.frequency_axis<<':'
       <<(g.recording_rows ? g.recording_rows : g.ds.rows())<<':'<<numfmt(bounds.start)<<':'<<numfmt(bounds.end)<<'\n'
       <<g.fft_window_active<<':'<<numfmt(g.fft_window_start)<<':'<<numfmt(g.fft_window_end)<<'\n';
    const auto formula=[&](const std::wstring& value) {const auto text=to_utf8(value);out<<text.size()<<':'<<text<<'\n';};
    formula(g.global_formula);for(const auto& value:g.channel_formulas)formula(value);
    out<<g.noise_threshold_enabled;
    if(g.noise_threshold_enabled)out<<':'<<g.noise_threshold_mode<<':'<<g.noise_threshold_topology<<':'
        <<numfmt(g.noise_threshold_min)<<':'<<numfmt(g.noise_threshold_max);
    out<<'\n'<<g.frf.apply_processing<<':'<<static_cast<int>(g.frf.options.estimator)<<':'<<static_cast<int>(g.frf.options.window)<<':'
       <<g.frf.options.segment_length<<':'<<g.frf.options.remove_mean<<':'<<numfmt(g.frf.options.reference_threshold)<<'\n';
    for(int c:g.frf.inputs)out<<c<<',';
    out<<'\n';for(int c:g.frf.outputs)out<<c<<',';
    return out.str();
}
bool same_options(const lvm::FrfOptions& a,const lvm::FrfOptions& b) {
    return a.estimator==b.estimator && a.window==b.window && a.segment_length==b.segment_length &&
        a.remove_mean==b.remove_mean && a.reference_threshold==b.reference_threshold;
}
bool source_window(double start,double end,bool selected) {
    double a=0,b=0;bool current=false;
    return current_fft_source_window(a,b,current) && a==start && b==end && current==selected;
}
void write_spectrum(Writer& out) {
    const auto& s=g.spec;
    out.real(g.spec_source_start);out.real(g.spec_source_end);out.integer(g.spec_source_from_selection);
    out.real(s.source_start);out.real(s.source_end);out.real(s.sample_dt);out.real(s.nyquist);out.integer(s.n);
    out.integer(s.resampled);out.integer(s.gaps_ignored);
    out.integer(s.bin_count());out.integer(s.names.size());
    // Preflight before reading disk amplitudes into RAM.
    expect(s.bin_count()<=max_cache_bytes/8/(s.names.size()+1));
    for(std::size_t k=0;k<s.bin_count();++k)out.real(s.frequency(k));
    expect(s.source_channels.size()==s.names.size());
    for(std::size_t c=0;c<s.names.size();++c) {
        out.integer(s.source_channels[c]);std::vector<double> values;
        s.read_amplitudes(c,0,s.bin_count(),values);out.doubles(values);
    }
    out.integer(g.spec_visible_state.size());for(char value:g.spec_visible_state)out.integer(value!=0);
}
void read_spectrum(Reader& in,lvm::Spectrum& s,double& start,double& end,bool& selected,std::vector<char>& visible) {
    start=in.real();end=in.real();selected=in.flag();expect(source_window(start,end,selected));
    s.source_start=in.real();s.source_end=in.real();s.sample_dt=in.real();s.nyquist=in.real();
    const auto count=in.integer();expect(count>=2 && count<=std::numeric_limits<int>::max());s.n=static_cast<int>(count);
    s.resampled=in.flag();s.gaps_ignored=in.flag();
    const auto bins=in.integer(),channels=in.integer();
    expect(bins==count/2+1 && bins<=(in.bytes.size()-in.at)/8 && channels>0 && channels<=g.ds.channel_count());
    expect(std::isfinite(s.sample_dt) && s.sample_dt>0 && std::isfinite(s.nyquist) && s.nyquist>0);
    s.freqs.resize(static_cast<std::size_t>(bins));
    for(std::size_t k=0;k<bins;++k) {s.freqs[k]=in.real();expect(std::isfinite(s.freqs[k]) && (k ? s.freqs[k]>s.freqs[k-1] : s.freqs[k]==0));}
    for(std::size_t c=0;c<channels;++c) {
        const auto channel=in.integer();expect(channel<g.ds.channel_count());
        expect(std::find(s.source_channels.begin(),s.source_channels.end(),channel)==s.source_channels.end());
        s.source_channels.push_back(static_cast<std::size_t>(channel));s.names.push_back(to_utf8(channel_display_label(channel)));
        auto values=in.doubles();expect(values.size()==bins);
        for(double value:values)expect(std::isfinite(value) && value>=0);
        s.amp.push_back(std::move(values));
    }
    expect(in.integer()==g.visible.size());visible.resize(g.visible.size());for(auto& value:visible)value=in.flag();
    s.recording_source=g.recording_source;s.ok=true;
}
void write_frf(Writer& out) {
    out.real(g.frf.source_start);out.real(g.frf.source_end);out.integer(g.frf.from_selection);
    out.text(to_utf8(g.frf.processing_description));out.integer(g.frf.result.responses.size());
    for(const auto& r:g.frf.result.responses) {
        out.integer(r.ok);out.integer(static_cast<unsigned>(r.error));
        if(!r.ok)continue;
        out.real(r.sample_dt);out.real(r.source_start);out.real(r.source_end);
        out.integer(r.sample_count);out.integer(r.segment_length);out.integer(r.averages);out.integer(r.overlap_samples);out.integer(r.gaps_ignored);
        out.doubles(r.frequencies);out.integer(r.transfer.size());
        for(const auto& value:r.transfer) {out.real(value.real());out.real(value.imag());}
        out.mask(r.valid);out.doubles(r.reference_amplitude);out.mask(r.reference_amplitude_valid);
        out.doubles(r.coherence);out.mask(r.coherence_valid);
    }
}
void read_frf(Reader& in,lvm::FrfBatchResult& batch,double& start,double& end,bool& selected,std::wstring& description) {
    start=in.real();end=in.real();selected=in.flag();expect(source_window(start,end,selected));
    description=to_w(in.text());const auto count=in.integer();expect(count>0 && count==g.frf.outputs.size());
    batch.options=g.frf.options;batch.responses.resize(static_cast<std::size_t>(count));
    for(auto& r:batch.responses) {
        r.options=g.frf.options;r.ok=in.flag();const auto error=in.integer();
        expect(error<=static_cast<unsigned>(lvm::FrfError::SourceChanged));r.error=static_cast<lvm::FrfError>(error);
        if(!r.ok) {expect(r.error!=lvm::FrfError::None);continue;}
        expect(r.error==lvm::FrfError::None);
        r.sample_dt=in.real();r.source_start=in.real();r.source_end=in.real();
        r.sample_count=in.integer();r.segment_length=in.integer();r.averages=in.integer();r.overlap_samples=in.integer();r.gaps_ignored=in.flag();
        expect(std::isfinite(r.sample_dt) && r.sample_dt>0 && std::isfinite(r.source_start) && std::isfinite(r.source_end) &&
            r.source_end>r.source_start && r.segment_length>=4 && r.sample_count>=r.segment_length && r.averages>0 && r.overlap_samples<r.segment_length);
        r.frequencies=in.doubles();const auto bins=r.frequencies.size();expect(bins==r.segment_length/2+1 && bins>=3);
        expect(in.integer()==bins && bins<=(in.bytes.size()-in.at)/16);
        r.transfer.resize(bins);for(auto& value:r.transfer) {const double real=in.real(),imag=in.real();value={real,imag};}
        r.valid=in.mask();r.reference_amplitude=in.doubles();r.reference_amplitude_valid=in.mask();r.coherence=in.doubles();r.coherence_valid=in.mask();
        expect(r.valid.size()==bins && r.reference_amplitude.size()==bins && r.reference_amplitude_valid.size()==bins &&
            r.coherence.size()==bins && r.coherence_valid.size()==bins);
        for(std::size_t k=0;k<bins;++k) {
            expect(std::isfinite(r.frequencies[k]) && (k ? r.frequencies[k]>r.frequencies[k-1] : r.frequencies[k]==0));
            if(r.valid[k])expect(std::isfinite(r.transfer[k].real()) && std::isfinite(r.transfer[k].imag()));
            if(r.reference_amplitude_valid[k])expect(std::isfinite(r.reference_amplitude[k]) && r.reference_amplitude[k]>=0);
            if(r.coherence_valid[k])expect(std::isfinite(r.coherence[k]) && r.coherence[k]>=0 && r.coherence[k]<=1+1e-12);
        }
        batch.ok=true;
    }
    expect(batch.ok);
}
}

void write_project_results(std::ostream& out,const char* nl) {
    if(g.ds.frequency_axis)return; // Imported amplitudes are already the project data.
    const bool fft=g.spec_valid && g.spec.ok && !g.spec_pending && spectrum_matches_current_source();
    const bool frf=g.frf.result.ok && !g.frf.pending && same_options(g.frf.result.options,g.frf.options) &&
        g.frf.result.responses.size()==g.frf.outputs.size() && source_window(g.frf.source_start,g.frf.source_end,g.frf.from_selection);
    if(!fft && !frf)return;
    // Each result is optional independently; an oversized FFT must not prevent
    // saving a compact FRF. No computation is launched during project saving.
    try {
        Writer prefix;prefix.text("AMSignal analysis cache 1");prefix.text(numerical_key());
        Writer spectrum,frf_result;
        if(fft)try {write_spectrum(spectrum);}catch(const std::exception&) {spectrum.bytes.clear();}
        if(frf)try {write_frf(frf_result);}catch(const std::exception&) {frf_result.bytes.clear();}
        expect(prefix.bytes.size()<=max_cache_bytes-16);
        const auto fits=[&](std::size_t count) {return count<=max_cache_bytes-prefix.bytes.size()-16;};
        if(!fits(spectrum.bytes.size()))spectrum.bytes.clear();
        if(!fits(spectrum.bytes.size()+frf_result.bytes.size()))spectrum.bytes.clear();
        if(!fits(frf_result.bytes.size()))frf_result.bytes.clear();
        if(spectrum.bytes.empty() && frf_result.bytes.empty())return;
        prefix.integer(!spectrum.bytes.empty());prefix.bytes.insert(prefix.bytes.end(),spectrum.bytes.begin(),spectrum.bytes.end());
        prefix.integer(!frf_result.bytes.empty());prefix.bytes.insert(prefix.bytes.end(),frf_result.bytes.begin(),frf_result.bytes.end());
        out<<"# [analysis_cache]"<<nl<<"# checksum="<<checksum(prefix.bytes)<<nl;
        const char* hex="0123456789ABCDEF";
        for(std::size_t at=0;at<prefix.bytes.size();at+=1024) {
            out<<"# bytes=";for(std::size_t k=at;k<std::min(at+1024,prefix.bytes.size());++k)out<<hex[prefix.bytes[k]>>4]<<hex[prefix.bytes[k]&15];out<<nl;
        }
        out<<"# [analysis_cache_end]"<<nl;
    }catch(const std::exception&) {} // Cache admission never prevents saving data.
}

void restore_project_results(const std::vector<std::string>& comments) {
    if(g.ds.frequency_axis)return;
    try {
        Bytes bytes;std::string expected;bool inside=false,complete=false;
        const auto digit=[](char c)->int {return c>='0' && c<='9' ? c-'0' : c>='A' && c<='F' ? c-'A'+10 : -1;};
        for(const auto& line:comments) {
            if(line=="[analysis_cache]") {expect(!inside && bytes.empty());inside=true;continue;}
            if(line=="[analysis_cache_end]" && inside) {complete=true;inside=false;continue;}
            if(!inside)continue;
            if(line.rfind("checksum=",0)==0) {expect(expected.empty());expected=line.substr(9);continue;}
            expect(line.rfind("bytes=",0)==0 && (line.size()-6)%2==0);
            expect((line.size()-6)/2<=max_cache_bytes-bytes.size());
            for(std::size_t k=6;k<line.size();k+=2) {const auto a=digit(line[k]),b=digit(line[k+1]);expect(a>=0 && b>=0);bytes.push_back(static_cast<unsigned char>(16*a+b));}
        }
        expect(complete && !inside && expected==std::to_string(checksum(bytes)));
        const auto resources=current_analysis_resources();
        auto admission=resources.budget ? resources.budget->reserve(bytes.size()*3+1024*1024) : nullptr;
        if(resources.budget && !admission)return;
        Reader in{bytes};expect(in.text()=="AMSignal analysis cache 1" && in.text()==numerical_key());
        lvm::Spectrum spec;lvm::FrfBatchResult batch;
        double fft_start=0,fft_end=0,frf_start=0,frf_end=0;bool fft_selected=false,frf_selected=false;
        std::vector<char> visible;std::wstring description;
        const bool fft=in.flag();if(fft)read_spectrum(in,spec,fft_start,fft_end,fft_selected,visible);
        const bool frf=in.flag();if(frf)read_frf(in,batch,frf_start,frf_end,frf_selected,description);
        expect(in.at==bytes.size());
        if(fft) {
            g.spec_source_start=fft_start;g.spec_source_end=fft_end;g.spec_source_from_selection=fft_selected;
            g.spec_source_valid=true;g.spec_attempted=true;g.spec_visible_state=std::move(visible);
            apply_spectrum_result(std::move(spec));
        }
        if(frf) {
            g.frf.source_start=frf_start;g.frf.source_end=frf_end;g.frf.from_selection=frf_selected;
            g.frf.attempted=true;g.frf.processing_description=std::move(description);
            refresh_frf_channel_names();apply_frf_result(std::move(batch));
        }
        admission.reset();trim_document_analysis_caches();
    }catch(const std::exception&) {} // Unknown, stale or malformed caches recalculate.
}
} // namespace gui
