#include "frf_analysis.hpp"
#include "fft.hpp"
#include "sampling.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace lvm {
namespace {
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Operation cancelled.");
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
            sx+=s.reference[at+j]; sy+=s.response[at+j]; peak=std::max(peak,std::abs(s.reference[at+j]));
        }
        const long double mx=options.remove_mean ? sx/L : 0, my=options.remove_mean ? sy/L : 0;
        for (std::size_t j=0;j<L;++j) {
            if ((j & 0xffff)==0) check_cancel(cancel);
            x[j]=static_cast<double>((s.reference[at+j]-mx)*window[j]);
            y[j]=static_cast<double>((s.response[at+j]-my)*window[j]);
        }
        x=dft(x,cancel); y=dft(y,cancel);
        for (std::size_t k=0;k<bins;++k) {
            if ((k & 0xffff)==0) check_cancel(cancel);
            if (!std::isfinite(std::abs(x[k])) || !std::isfinite(std::abs(y[k]))) return fail(FrfError::Overflow);
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
        if (!std::isfinite(std::abs(value))) continue;
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
    FrfBatchResult batch; batch.options=options;
    check_cancel(cancel);
    if (input.references.empty() || input.responses.empty()) { batch.error=FrfError::InvalidChannels; return batch; }
    const std::size_t n=input.time.size();
    for (const auto& ref:input.references) {
        if (ref.size()!=n) { batch.error=FrfError::InvalidChannels; return batch; }
        for (std::size_t i=0;i<n;++i) {
            if ((i & 0xffff)==0) check_cancel(cancel);
            if (!std::isfinite(ref[i])) { batch.error=FrfError::MissingValues; return batch; }
        }
    }
    std::vector<double> average;
    if (input.references.size()==1) average=std::move(input.references.front());
    else {
        average.resize(n);
        for (std::size_t i=0;i<n;++i) {
            if ((i & 0xffff)==0) check_cancel(cancel);
            long double sum=0;
            for (const auto& ref:input.references) sum+=static_cast<long double>(ref[i])/input.references.size();
            average[i]=static_cast<double>(sum);
            if (!std::isfinite(average[i])) { batch.error=FrfError::Overflow; return batch; }
        }
    }
    input.references.clear();
    // Validate the common reference/timeline independently of response errors.
    FrfInput pair{std::move(input.time),std::move(average),{}};
    pair.response=pair.reference;
    auto samples=prepare_frf_samples(pair,cancel);
    pair={};
    if (samples.error!=FrfError::None) { batch.error=samples.error; return batch; }
    batch.responses.reserve(input.responses.size());
    for (auto& response:input.responses) {
        check_cancel(cancel);
        samples.response=std::move(response);
        // The single-pair estimator is unchanged. Identical sample counts and
        // options give all successful responses the same Fs, L, K and grid.
        auto r=compute_frf(samples,options,cancel);
        if (r.ok) batch.ok=true;
        batch.responses.push_back(std::move(r));
    }
    if (!batch.ok) batch.error=batch.responses.front().error;
    return batch;
}
} // namespace lvm
