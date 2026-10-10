#include "filter_engine.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
constexpr double kButterworthQ = 0.70710678118654752440;
constexpr double kBesselQ = 0.57735026918962576451;
struct BiquadCoefficients {
    double b0 = 1.0;
    double b1 = 0.0;
    double b2 = 0.0;
    double a1 = 0.0;
    double a2 = 0.0;
};

struct BiquadState {
    double z1 = 0.0;
    double z2 = 0.0;
};

double clamp_filter_cutoff(double hz, double nyquist) {
    return std::isfinite(hz) ? std::clamp(hz, 0.0, nyquist) : 0.0;
}

BiquadCoefficients design_biquad(FilterMode mode, int topology, double cutoff_hz, double sample_rate) {
    BiquadCoefficients coeffs;
    if (!(sample_rate > 0.0) || !(cutoff_hz > 0.0) || !std::isfinite(cutoff_hz)) return coeffs;
    if (mode != FilterModeLowPass && mode != FilterModeHighPass) return coeffs;
    cutoff_hz = std::clamp(cutoff_hz, 0.0, sample_rate * 0.499);
    if (!(cutoff_hz > 0.0)) return coeffs;
    const double q = (topology == FilterTopologyBessel) ? kBesselQ :
                     (topology == FilterTopologyChebyshev) ? 0.92 :
                     kButterworthQ;
    const double omega = 2.0 * 3.14159265358979323846 * cutoff_hz / sample_rate;
    const double sn = std::sin(omega);
    const double cs = std::cos(omega);
    const double alpha = sn / (2.0 * q);
    double b0 = 1.0, b1 = 0.0, b2 = 0.0;
    double a0 = 1.0, a1 = 0.0, a2 = 0.0;
    if (mode == FilterModeLowPass) {
        b0 = (1.0 - cs) * 0.5;
        b1 = 1.0 - cs;
        b2 = (1.0 - cs) * 0.5;
    } else {
        b0 = (1.0 + cs) * 0.5;
        b1 = -(1.0 + cs);
        b2 = (1.0 + cs) * 0.5;
    }
    a0 = 1.0 + alpha;
    a1 = -2.0 * cs;
    a2 = 1.0 - alpha;
    if (!(std::abs(a0) > 0.0)) return coeffs;
    coeffs.b0 = b0 / a0;
    coeffs.b1 = b1 / a0;
    coeffs.b2 = b2 / a0;
    coeffs.a1 = a1 / a0;
    coeffs.a2 = a2 / a0;
    return coeffs;
}

double process_biquad_sample(const BiquadCoefficients& coeffs, BiquadState& state, double input) {
    const double output = coeffs.b0 * input + state.z1;
    state.z1 = coeffs.b1 * input - coeffs.a1 * output + state.z2;
    state.z2 = coeffs.b2 * input - coeffs.a2 * output;
    return output;
}
} // namespace

struct FilterStream::State {
    BiquadState stage1{}, stage2{}, stage3{}, stage4{};
    bool have_segment=false;
    double prev_time=0;
};
FilterStream::FilterStream(FilterSettings settings):state_(std::make_unique<State>()),settings_(settings) {}
FilterStream::~FilterStream()=default;
FilterStream::FilterStream(FilterStream&&) noexcept=default;
FilterStream& FilterStream::operator=(FilterStream&&) noexcept=default;

std::vector<double> FilterStream::process(const std::vector<double>& time, const std::vector<double>& src,
                                         const std::atomic<bool>* cancel) {
    const auto& settings=settings_;
    if (src.size() != time.size()) throw std::invalid_argument("Filter requires aligned samples and time.");
    const double step = settings.sample_step;
    if (!(step > 0.0) || !std::isfinite(step)) return src;
    const double nyquist = 0.5 / step;
    std::vector<double> dst(src.size());
    const double sample_rate = 1.0 / step;
    const double gap_threshold = step * 64.0;
    const int mode = (settings.mode >= FilterModeLowPass && settings.mode <= FilterModeBandStop)
        ? settings.mode
        : FilterModeLowPass;
    const int topology = (settings.topology == FilterTopologyBessel)
        ? FilterTopologyBessel
        : (settings.topology == FilterTopologyChebyshev)
            ? FilterTopologyChebyshev
            : (settings.topology == FilterTopologyLinkwitzRiley)
                ? FilterTopologyLinkwitzRiley
                : FilterTopologyButterworth;
    const double low_cutoff = clamp_filter_cutoff(settings.low_cutoff, nyquist);
    const double high_cutoff = clamp_filter_cutoff(settings.high_cutoff, nyquist);
    const BiquadCoefficients lowpass = design_biquad(FilterModeLowPass, topology, high_cutoff, sample_rate);
    const BiquadCoefficients highpass = design_biquad(FilterModeHighPass, topology, low_cutoff, sample_rate);
    const bool linkwitz_riley = topology == FilterTopologyLinkwitzRiley;
    const BiquadCoefficients lowpass_lr = design_biquad(FilterModeLowPass, FilterTopologyButterworth, high_cutoff, sample_rate);
    const BiquadCoefficients highpass_lr = design_biquad(FilterModeHighPass, FilterTopologyButterworth, low_cutoff, sample_rate);

    auto& stage1=state_->stage1;
    auto& stage2=state_->stage2;
    auto& stage3=state_->stage3;
    auto& stage4=state_->stage4;
    auto& have_segment=state_->have_segment;
    auto& prev_time=state_->prev_time;
    for (std::size_t i = 0; i < src.size(); ++i) {
        if (cancel && (i & 0xFFF) == 0 && cancel->load(std::memory_order_relaxed)) throw std::runtime_error("Operation cancelled.");
        const double input = src[i];
        const double tt = time[i];
        const bool finite = std::isfinite(input) && std::isfinite(tt);
        if (!finite) {
            dst[i] = input;
            stage1 = {};
            stage2 = {};
            stage3 = {};
            stage4 = {};
            have_segment = false;
            prev_time = tt;
            continue;
        }
        if (have_segment) {
            const double dt = tt - prev_time;
            if (!(dt > 0.0) || !std::isfinite(dt) || dt > gap_threshold) {
                stage1 = {};
                stage2 = {};
                stage3 = {};
                stage4 = {};
                have_segment = false;
            }
        }

        double output = input;
        if (linkwitz_riley) {
            switch (mode) {
                case FilterModeHighPass: {
                    const double hp1 = (low_cutoff > 0.0) ? process_biquad_sample(highpass_lr, stage1, input) : input;
                    output = (low_cutoff > 0.0) ? process_biquad_sample(highpass_lr, stage2, hp1) : hp1;
                    break;
                }
                case FilterModeBandPass: {
                    const double hp1 = (low_cutoff > 0.0) ? process_biquad_sample(highpass_lr, stage1, input) : input;
                    const double hp2 = (low_cutoff > 0.0) ? process_biquad_sample(highpass_lr, stage2, hp1) : hp1;
                    const double bp1 = (high_cutoff < nyquist) ? process_biquad_sample(lowpass_lr, stage3, hp2) : hp2;
                    output = (high_cutoff < nyquist) ? process_biquad_sample(lowpass_lr, stage4, bp1) : bp1;
                    break;
                }
                case FilterModeBandStop: {
                    const double hp1 = (low_cutoff > 0.0) ? process_biquad_sample(highpass_lr, stage1, input) : input;
                    const double hp2 = (low_cutoff > 0.0) ? process_biquad_sample(highpass_lr, stage2, hp1) : hp1;
                    const double bp1 = (high_cutoff < nyquist) ? process_biquad_sample(lowpass_lr, stage3, hp2) : hp2;
                    const double bp2 = (high_cutoff < nyquist) ? process_biquad_sample(lowpass_lr, stage4, bp1) : bp1;
                    output = input - bp2;
                    break;
                }
                case FilterModeLowPass:
                default: {
                    const double lp1 = (high_cutoff < nyquist) ? process_biquad_sample(lowpass_lr, stage1, input) : input;
                    output = (high_cutoff < nyquist) ? process_biquad_sample(lowpass_lr, stage2, lp1) : lp1;
                    break;
                }
            }
        } else {
            switch (mode) {
                case FilterModeHighPass:
                    output = (low_cutoff > 0.0) ? process_biquad_sample(highpass, stage1, input) : input;
                    break;
                case FilterModeBandPass: {
                    const double hp = (low_cutoff > 0.0) ? process_biquad_sample(highpass, stage1, input) : input;
                    output = (high_cutoff < nyquist) ? process_biquad_sample(lowpass, stage2, hp) : hp;
                    break;
                }
                case FilterModeBandStop: {
                    const double hp = (low_cutoff > 0.0) ? process_biquad_sample(highpass, stage1, input) : input;
                    const double bp = (high_cutoff < nyquist) ? process_biquad_sample(lowpass, stage2, hp) : hp;
                    output = input - bp;
                    break;
                }
                case FilterModeLowPass:
                default:
                    output = (high_cutoff < nyquist) ? process_biquad_sample(lowpass, stage1, input) : input;
                    break;
            }
        }
        dst[i] = output;
        have_segment = true;
        prev_time = tt;
    }
    return dst;
}

std::vector<double> filter_signal(const std::vector<double>& time,const std::vector<double>& src,
                                  const FilterSettings& settings,const std::atomic<bool>* cancel) {
    return FilterStream(settings).process(time,src,cancel);
}
