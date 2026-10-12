#pragma once

#include <cmath>

namespace lvm {

struct TimeRange {
    double start = 0.0;
    double end = 0.0;
    bool valid() const {
        return std::isfinite(start) && std::isfinite(end) && end >= start;
    }
};

// Independent of the displayed viewport and its resident sample arrays.
struct AnalysisRange {
    enum class Kind { EntireRecording, Interval };
    Kind kind = Kind::EntireRecording;
    TimeRange interval;

    static AnalysisRange entire() { return {}; }
    static AnalysisRange selected(double start, double end) {
        return {Kind::Interval, {start, end}};
    }
    // Reject out-of-source ranges rather than silently changing an analysis.
    bool resolve(const TimeRange& recording, TimeRange& result) const {
        if (!recording.valid()) return false;
        if (kind == Kind::EntireRecording) { result = recording; return true; }
        if (!interval.valid() || interval.start < recording.start || interval.end > recording.end)
            return false;
        result = interval;
        return true;
    }
};

} // namespace lvm
