#include "frf_analysis.hpp"
#include "fft.hpp"
#include "sampling.hpp"
#include "frf_stream.hpp"
#include "analysis_executor.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lvm {
namespace {
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Operation cancelled.");
}
bool finite_complex(const std::complex<double>& z) {
    if (!std::isfinite(z.real()) || !std::isfinite(z.imag())) return false;
    constexpr double safe=std::numeric_limits<double>::max()/2;
    return (std::abs(z.real())<=safe && std::abs(z.imag())<=safe) || std::isfinite(std::abs(z));
}
void transform(std::vector<std::complex<double>>& a,const std::atomic<bool>* cancel) {
    if ((a.size() & (a.size()-1))==0) fft_radix2(a,false,cancel);
    else a=dft(a,cancel);
}
long double reference_threshold(const std::vector<long double>& xx, double peak,
                                std::size_t L, std::size_t averages, const FrfOptions& options) {
    const long double maximum=*std::max_element(xx.begin()+1,xx.end());
    const long double roundoff=static_cast<long double>(peak)*32*std::numeric_limits<double>::epsilon()*L;
    return std::max(roundoff*roundoff*averages,maximum*options.reference_threshold*options.reference_threshold);
}

// All accumulation remains long double and in segment order. The first
// completed response owns common display arrays; subsequent results copy them.
void finish_frf(FrfResult& r, const std::vector<long double>& xx,
                const std::vector<long double>& yy, const std::vector<std::complex<long double>>& xy,
                long double window_sum, long double threshold, const std::atomic<bool>* cancel,
                const FrfResult* common = nullptr) {
    const std::size_t L=r.segment_length, bins=L/2+1;
    if (common) {
        r.frequencies=common->frequencies;
        r.reference_amplitude=common->reference_amplitude;
        r.reference_amplitude_valid=common->reference_amplitude_valid;
    } else {
        r.frequencies.resize(bins);
        r.reference_amplitude.assign(bins,std::numeric_limits<double>::quiet_NaN());
        r.reference_amplitude_valid.assign(bins,0);
    }
    r.transfer.resize(bins); r.valid.assign(bins,0);
    r.coherence.assign(bins,std::numeric_limits<double>::quiet_NaN()); r.coherence_valid.assign(bins,0);
    bool any=false;
    for (std::size_t k=0;k<bins;++k) {
        if ((k & 0xffff)==0) check_cancel(cancel);
        if (!common) {
            r.frequencies[k]=(static_cast<double>(k)/L)/r.sample_dt;
            if (k && window_sum>0 && xx[k]>=0) {
                const long double amplitude=2*std::sqrt(xx[k]/r.averages)/window_sum;
                if (std::isfinite(amplitude)) {
                    r.reference_amplitude[k]=static_cast<double>(amplitude);
                    r.reference_amplitude_valid[k]=1;
                }
            }
        }
        if (!k || xx[k]<=threshold) continue;
        const auto h=xy[k]/xx[k];
        const std::complex<double> value{static_cast<double>(h.real()),static_cast<double>(h.imag())};
        if (!finite_complex(value)) continue;
        r.transfer[k]=value;
        if (r.options.estimator==FrfEstimator::H1 && r.averages>=2 && yy[k]>0) {
            const long double c=std::norm((xy[k]/std::sqrt(xx[k]))/std::sqrt(yy[k]));
            if (std::isfinite(c)) {
                r.coherence[k]=static_cast<double>(std::clamp(c,0.0L,1.0L)); r.coherence_valid[k]=1;
            }
        }
        r.valid[k]=1; any=true;
    }
    r.ok=any; if (!any) r.error=FrfError::WeakReference;
}

FrfBatchResult compute_shared_frf(const PreparedFrf& input,
                                 const FrfOptions& options, const std::atomic<bool>* cancel) {
    const auto& s=input.metadata;
    FrfBatchResult batch; batch.options=options;
    const std::size_t n=s.source_count, response_count=input.response_errors.size();
    if(s.error!=FrfError::None) {batch.error=s.error;return batch;}
    if(!response_count) {batch.error=FrfError::InvalidChannels;return batch;}
    FrfError error=FrfError::None;
    if ((options.estimator!=FrfEstimator::Direct && options.estimator!=FrfEstimator::H1) ||
        options.window!=FrfWindow::Hann || !std::isfinite(options.reference_threshold) ||
        options.reference_threshold<=0 || options.reference_threshold>=1 ||
        (options.segment_length && (options.segment_length<4 || options.segment_length%2))) error=FrfError::InvalidOptions;
    std::size_t L=options.estimator==FrfEstimator::Direct ? n : options.segment_length;
    if (!L) { const std::size_t target=std::max<std::size_t>(4,n/4); L=4; while (L<=target/2) L*=2; }
    if (error==FrfError::None && (L>n || L<4)) error=FrfError::TooShort;
    if (error==FrfError::None && L>static_cast<std::size_t>(std::numeric_limits<int>::max()/2)) error=FrfError::Overflow;
    batch.responses.resize(response_count);
    struct Accum { std::vector<long double> yy; std::vector<std::complex<long double>> xy; };
    std::vector<Accum> accum(response_count);
    const std::size_t bins=L/2+1, overlap=options.estimator==FrfEstimator::H1 ? L/2 : 0, hop=L-overlap;
    // All possible caller/helper plans, accumulations, results, window, raw tile
    // and transformed frames. Auto/Direct never silently change the FFT length.
    const long double io_block=input.memory ? 0 : 4096.0L*(response_count+1)*8;
    const long double fixed=fft_cached_plan_bytes() + io_block + L*8.0L + bins*(16.0L+response_count*100.0L);
    const long double frame_bytes=static_cast<long double>(L)*(response_count+1)*24.0L;
    std::size_t fft_m=1;
    if(error==FrfError::None && (L & (L-1))!=0)while(fft_m<2*L-1)fft_m*=2;
    const long double scratch=(L & (L-1)) ? (L+fft_m)*32.0L : 0;
    if(error==FrfError::None && fixed+frame_bytes+scratch>analysis_memory_budget)error=FrfError::ResourceLimit;
    std::size_t tile_capacity=1;
    if(error==FrfError::None)tile_capacity=std::max<std::size_t>(1,std::min<std::size_t>(8,
        static_cast<std::size_t>(std::min(16.0L*1024*1024,analysis_memory_budget-fixed-scratch)/frame_bytes)));
    if(error==FrfError::None && (n-L)/hop+1<7)tile_capacity=1;
    std::size_t active=0;
    for (std::size_t c=0;c<response_count;++c) {
        auto& r=batch.responses[c]; r.options=options;
        r.sample_dt=s.sample_dt; r.source_start=s.source_start; r.source_end=s.source_end;
        r.sample_count=s.source_count ? s.source_count : n; r.gaps_ignored=s.gaps_ignored;
        if (input.response_errors[c]==FrfError::InvalidChannels) { r.error=FrfError::InvalidChannels; continue; }
        if (error!=FrfError::None) { r.error=error; continue; }
        r.segment_length=L; r.overlap_samples=overlap;
        r.error=input.response_errors[c];
        if (r.error!=FrfError::None) continue;
        accum[c].yy.assign(bins,0); accum[c].xy.resize(bins); ++active;
    }
    if (!active) { batch.error=batch.responses.front().error; return batch; }
    std::vector<long double> xx(bins,0);
    std::vector<double> window(L);
    for (std::size_t j=0;j<L;++j) {
        if ((j & 0xffff)==0) check_cancel(cancel);
        window[j]=.5-.5*std::cos(2*std::acos(-1.0)*j/L);
    }
    long double window_sum=0; for (double value:window) window_sum+=value;
    double peak=0;
    struct Frame { std::vector<std::vector<std::complex<double>>> spectra; double peak=0; };
    std::vector<Frame> frames(tile_capacity);
    std::vector<std::vector<double>> raw;
    const auto segments=(n-L)/hop+1;
    std::size_t tile_base=0, tile_used=0;
    const auto hardware=std::thread::hardware_concurrency();
    std::size_t participants=segments>=7 && L>=256 ? std::min<std::size_t>(4,hardware ? hardware : 1) : 1;
    while(participants>1 && fixed+tile_capacity*frame_bytes+participants*scratch>analysis_memory_budget)--participants;
    for (std::size_t at=0; n-at>=L; at+=hop) {
        check_cancel(cancel);
        const std::size_t segment=at/hop;
        if(segment==tile_base) {
            tile_used=std::min(tile_capacity,segments-segment);
            input.read(at,L+(tile_used-1)*hop,raw,cancel);
            const auto job=[&](std::size_t index) {
                auto& frame=frames[index];frame.peak=0;frame.spectra.resize(response_count+1);
                for(std::size_t c=0;c<=response_count;++c) {
                    if(c && batch.responses[c-1].error!=FrfError::None)continue;
                    auto& a=frame.spectra[c];a.resize(L);long double sum=0;
                    for(std::size_t j=0;j<L;++j) {
                        if((j & 65535)==0)check_cancel(cancel);
                        const auto value=raw[c][index*hop+j];
                        if(options.remove_mean)sum+=value;
                        if(!c)frame.peak=std::max(frame.peak,std::abs(value));
                    }
                    const long double mean=options.remove_mean ? sum/L : 0;
                    for(std::size_t j=0;j<L;++j) {
                        if((j & 65535)==0)check_cancel(cancel);
                        a[j]=static_cast<double>((raw[c][index*hop+j]-mean)*window[j]);
                    }
                    transform(a,cancel);
                }
            };
            if(participants<=1 || !analysis_executor().run(tile_used,participants,job))
                for(std::size_t i=0;i<tile_used;++i)job(i);
        }
        auto& frame=frames[segment-tile_base];
        auto& x=frame.spectra[0];peak=std::max(peak,frame.peak);
        bool reference_ok=true;
        for (std::size_t k=0;k<bins;++k) {
            if ((k & 0xffff)==0) check_cancel(cancel);
            if (!finite_complex(x[k])) { reference_ok=false; break; }
            const std::complex<long double> a=x[k]; xx[k]+=std::norm(a);
        }
        if (!reference_ok) {
            for (auto& r:batch.responses) if (r.error==FrfError::None) r.error=FrfError::Overflow;
            break;
        }
        for (std::size_t c=0;c<response_count;++c) {
            auto& r=batch.responses[c]; if (r.error!=FrfError::None) continue;
            check_cancel(cancel);
            const auto& y=frame.spectra[c+1];
            for (std::size_t k=0;k<bins;++k) {
                if ((k & 0xffff)==0) check_cancel(cancel);
                if (!finite_complex(y[k])) { r.error=FrfError::Overflow; --active; break; }
                const std::complex<long double> a=x[k], b=y[k];
                accum[c].yy[k]+=std::norm(b); accum[c].xy[k]+=std::conj(a)*b;
            }
            if (r.error==FrfError::None) ++r.averages;
        }
        if (!active || options.estimator==FrfEstimator::Direct) break;
        if(segment+1==tile_base+tile_used)tile_base=segment+1;
    }
    const FrfResult* common=nullptr;
    long double threshold=0;
    for (std::size_t c=0;c<response_count;++c) {
        auto& r=batch.responses[c]; if (r.error!=FrfError::None) continue;
        // Every surviving response has the same K and reference statistics.
        if (!common) {
            threshold=reference_threshold(xx,peak,L,r.averages,options);
            finish_frf(r,xx,accum[c].yy,accum[c].xy,window_sum,threshold,cancel);
            common=&r;
        } else {
            finish_frf(r,xx,accum[c].yy,accum[c].xy,window_sum,threshold,cancel,common);
        }
        if (r.ok) batch.ok=true;
    }
    if (!batch.ok) batch.error=batch.responses.front().error;
    return batch;
}

}
const char* frf_error_text(FrfError e) {
    switch (e) {
        case FrfError::None: return "";
        case FrfError::InvalidChannels: return "Select two different, aligned input/output channels.";
        case FrfError::FrequencyData: return "FRF requires time-domain input and output signals.";
        case FrfError::TooShort: return "Not enough selected samples for L. Reduce segment length or select more data.";
        case FrfError::InvalidTime: return "FRF requires finite, strictly increasing timestamps.";
        case FrfError::MissingValues: return "The selected pair contains missing or infinite values.";
        case FrfError::InvalidOptions: return "Invalid options: H1 needs an even segment length >= 4, or Auto.";
        case FrfError::WeakReference: return "The reference has no usable AC excitation.";
        case FrfError::Overflow: return "FRF exceeded numeric or sample-count limits.";
        case FrfError::ResourceLimit: return "FRF resources are insufficient. Check scratch disk space or set a smaller explicit H1 segment length.";
        case FrfError::SourceChanged: return "The source file changed. Reopen the recording before calculating FRF.";
    }
    return "FRF calculation failed.";
}

FrfSamples prepare_frf_samples(const FrfInput& in, const std::atomic<bool>* cancel) {
    FrfSamples s;
    const auto fail=[&](FrfError e) { s.error=e; return s; };
    check_cancel(cancel);
    const std::size_t n=in.time.size();
    if (in.reference.size()!=n || in.response.size()!=n) return fail(FrfError::InvalidChannels);
    if (n<4) return fail(FrfError::TooShort);
    if (n>static_cast<std::size_t>(std::numeric_limits<int>::max()/2)) return fail(FrfError::Overflow);
    std::vector<double> steps; steps.reserve(n-1);
    for (std::size_t i=0;i<n;++i) {
        if ((i & 0xffff)==0) check_cancel(cancel);
        if (!std::isfinite(in.time[i])) return fail(FrfError::InvalidTime);
        if (!std::isfinite(in.reference[i]) || !std::isfinite(in.response[i])) return fail(FrfError::MissingValues);
        if (i) {
            const double d=in.time[i]-in.time[i-1];
            if (!std::isfinite(d) || d<=0) return fail(FrfError::InvalidTime);
            steps.push_back(d);
        }
    }
    const double typical=typical_sample_spacing(steps);
    if (!(typical>0) || !std::isfinite(.5/typical)) return fail(FrfError::InvalidTime);
    // Estimate Fs from the acquisition cadence, not the total elapsed time.
    // Gaps affect only provenance: both arrays keep every original sample.
    s.sample_dt=typical;
    s.source_start=in.time.front(); s.source_end=in.time.back(); s.source_count=n;
    for (std::size_t i=1;i<n;++i) {
        if ((i & 0xffff)==0) check_cancel(cancel);
        if (in.time[i]-in.time[i-1]>typical*sampling_gap_factor) s.gaps_ignored=true;
    }
    s.reference=in.reference;
    check_cancel(cancel);
    s.response=in.response;
    return s;
}

FrfResult compute_frf(const FrfSamples& s, const FrfOptions& options, const std::atomic<bool>* cancel) {
    FrfResult r; r.options=options;
    r.sample_dt=s.sample_dt; r.source_start=s.source_start; r.source_end=s.source_end;
    r.sample_count=s.source_count ? s.source_count : s.reference.size(); r.gaps_ignored=s.gaps_ignored;
    const auto fail=[&](FrfError e) { r.error=e; return r; };
    check_cancel(cancel);
    if (s.error!=FrfError::None) return fail(s.error);
    const std::size_t n=s.reference.size();
    if (s.response.size()!=n) return fail(FrfError::InvalidChannels);
    if (n<4) return fail(FrfError::TooShort);
    if (!(s.sample_dt>0) || !std::isfinite(s.sample_dt) || !std::isfinite(.5/s.sample_dt)) return fail(FrfError::InvalidTime);
    if ((options.estimator!=FrfEstimator::Direct && options.estimator!=FrfEstimator::H1) ||
        options.window!=FrfWindow::Hann || !std::isfinite(options.reference_threshold) ||
        options.reference_threshold<=0 || options.reference_threshold>=1 ||
        (options.segment_length && (options.segment_length<4 || options.segment_length%2))) return fail(FrfError::InvalidOptions);
    std::size_t L=options.estimator==FrfEstimator::Direct ? n : options.segment_length;
    if (!L) {
        const std::size_t target=std::max<std::size_t>(4,n/4);
        L=4; while (L<=target/2) L*=2;
    }
    if (L>n || L<4) return fail(FrfError::TooShort);
    if (L>static_cast<std::size_t>(std::numeric_limits<int>::max()/2)) return fail(FrfError::Overflow);
    r.segment_length=L; r.overlap_samples=options.estimator==FrfEstimator::H1 ? L/2 : 0;
    const std::size_t bins=L/2+1, hop=L-r.overlap_samples;
    std::vector<long double> xx(bins,0), yy(bins,0);
    std::vector<std::complex<long double>> xy(bins);
    std::vector<double> window(L);
    std::vector<std::complex<double>> x(L), y(L);
    double peak=0;
    for (std::size_t i=0;i<n;++i) {
        if ((i & 0xffff)==0) check_cancel(cancel);
        if (!std::isfinite(s.reference[i]) || !std::isfinite(s.response[i])) return fail(FrfError::MissingValues);
    }
    for (std::size_t j=0;j<L;++j) {
        if ((j & 0xffff)==0) check_cancel(cancel);
        window[j]=.5-.5*std::cos(2*std::acos(-1.0)*j/L);
    }
    long double window_sum=0;
    for (double value:window) window_sum+=value;
    for (std::size_t at=0; n-at>=L; at+=hop) {
        check_cancel(cancel);
        long double sx=0, sy=0;
        for (std::size_t j=0;j<L;++j) {
            if ((j & 0xffff)==0) check_cancel(cancel);
            if (options.remove_mean) { sx+=s.reference[at+j]; sy+=s.response[at+j]; }
            peak=std::max(peak,std::abs(s.reference[at+j]));
        }
        const long double mx=options.remove_mean ? sx/L : 0, my=options.remove_mean ? sy/L : 0;
        for (std::size_t j=0;j<L;++j) {
            if ((j & 0xffff)==0) check_cancel(cancel);
            x[j]=static_cast<double>((s.reference[at+j]-mx)*window[j]);
            y[j]=static_cast<double>((s.response[at+j]-my)*window[j]);
        }
        transform(x,cancel); transform(y,cancel);
        for (std::size_t k=0;k<bins;++k) {
            if ((k & 0xffff)==0) check_cancel(cancel);
            if (!finite_complex(x[k]) || !finite_complex(y[k])) return fail(FrfError::Overflow);
            const std::complex<long double> a=x[k], b=y[k];
            xx[k]+=std::norm(a); yy[k]+=std::norm(b); xy[k]+=std::conj(a)*b;
        }
        ++r.averages;
        if (options.estimator==FrfEstimator::Direct) break;
    }
    if (!r.averages) return fail(FrfError::TooShort);
    // Common window-energy/PSD scale and 1/K cancel in H1 and coherence.
    const long double maximum=*std::max_element(xx.begin()+1,xx.end());
    const long double roundoff=static_cast<long double>(peak)*32*std::numeric_limits<double>::epsilon()*L;
    const long double threshold=std::max(roundoff*roundoff*r.averages,maximum*options.reference_threshold*options.reference_threshold);
    r.frequencies.resize(bins); r.transfer.resize(bins); r.valid.assign(bins,0);
    r.reference_amplitude.assign(bins,std::numeric_limits<double>::quiet_NaN());
    r.reference_amplitude_valid.assign(bins,0);
    r.coherence.assign(bins,std::numeric_limits<double>::quiet_NaN()); r.coherence_valid.assign(bins,0);
    bool any=false;
    for (std::size_t k=0;k<bins;++k) {
        if ((k & 0xffff)==0) check_cancel(cancel);
        r.frequencies[k]=(static_cast<double>(k)/L)/s.sample_dt;
        if (k && window_sum>0 && xx[k]>=0) {
            const long double amplitude=2*std::sqrt(xx[k]/r.averages)/window_sum;
            if (std::isfinite(amplitude)) {
                r.reference_amplitude[k]=static_cast<double>(amplitude);
                r.reference_amplitude_valid[k]=1;
            }
        }
        if (!k || xx[k]<=threshold) continue;
        const auto h=xy[k]/xx[k];
        const std::complex<double> value{static_cast<double>(h.real()),static_cast<double>(h.imag())};
        if (!finite_complex(value)) continue;
        r.transfer[k]=value;
        if (options.estimator==FrfEstimator::H1 && r.averages>=2 && yy[k]>0) {
            // Dividing before squaring avoids overflow of the PSD product.
            const long double c=std::norm((xy[k]/std::sqrt(xx[k]))/std::sqrt(yy[k]));
            if (std::isfinite(c)) {
                r.coherence[k]=static_cast<double>(std::clamp(c,0.0L,1.0L)); r.coherence_valid[k]=1;
            }
        }
        r.valid[k]=1; any=true;
    }
    r.ok=any; if (!any) r.error=FrfError::WeakReference;
    return r;
}

FrfResult analyze_frf(const FrfInput& in, const FrfOptions& options, const std::atomic<bool>* cancel) {
    return compute_frf(prepare_frf_samples(in,cancel),options,cancel);
}
double frf_dynamic_coefficient(const FrfResult& r, std::size_t k) {
    if (k>=r.valid.size() || !r.valid[k] || k>=r.transfer.size()) return std::numeric_limits<double>::quiet_NaN();
    return std::abs(r.transfer[k]);
}

const FrfResult& FrfBatchResult::common() const {
    for (const auto& r:responses) if (r.ok) return r;
    static const FrfResult empty;
    return responses.empty() ? empty : responses.front();
}

FrfBatchResult analyze_frf_batch(FrfBatchInput input, const FrfOptions& options, const std::atomic<bool>* cancel) {
    return compute_prepared_frf(*prepare_frf_batch(std::move(input),cancel),options,cancel);
}
FrfBatchResult compute_prepared_frf(const PreparedFrf& input,const FrfOptions& options,const std::atomic<bool>* cancel) {
    check_cancel(cancel);
    return compute_shared_frf(input,options,cancel);
}
} // namespace lvm
