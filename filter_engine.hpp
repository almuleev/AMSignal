#pragma once
#include <atomic>
#include <vector>
#include <memory>

enum FilterMode {
    FilterModeLowPass = 0,
    FilterModeHighPass = 1,
    FilterModeBandPass = 2,
    FilterModeBandStop = 3,
};

enum FilterTopology {
    FilterTopologyButterworth = 0,
    FilterTopologyBessel = 1,
    FilterTopologyChebyshev = 2,
    FilterTopologyLinkwitzRiley = 3,
};

struct FilterSettings {
    int mode = FilterModeBandPass;
    int topology = FilterTopologyButterworth;
    double low_cutoff = 0.0;
    double high_cutoff = 0.0;
    double sample_step = 0.0;
};

// Causal biquad state survives blocks; reset rules still use acquisition time.
class FilterStream {
public:
    explicit FilterStream(FilterSettings settings);
    ~FilterStream();
    FilterStream(FilterStream&&) noexcept;
    FilterStream& operator=(FilterStream&&) noexcept;
    std::vector<double> process(const std::vector<double>& time,const std::vector<double>& samples,
                                const std::atomic<bool>* cancel = nullptr);
private:
    struct State;
    std::unique_ptr<State> state_;
    FilterSettings settings_;
};

std::vector<double> filter_signal(const std::vector<double>& time, const std::vector<double>& samples,
                                  const FilterSettings& settings, const std::atomic<bool>* cancel = nullptr);
