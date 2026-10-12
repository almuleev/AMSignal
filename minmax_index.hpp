#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>
#include <vector>

// Exact min/max queries with small cached blocks. No samples are skipped.
class MinMaxIndex {
public:
    using Range = std::pair<double, double>;
    static constexpr std::size_t block_size = 64;
    std::size_t memory_bytes() const {
        std::size_t bytes=levels_.capacity()*sizeof(std::vector<Range>);
        for (const auto& level:levels_) bytes+=level.capacity()*sizeof(Range);
        return bytes;
    }
    template<class Sample> void build(std::size_t count, Sample sample) {
        count_ = count;
        levels_.clear();
        levels_.emplace_back((count + block_size - 1) / block_size, empty());
        for (std::size_t i = 0; i < count; ++i) add(levels_[0][i / block_size], sample(i));
        while (levels_.back().size() > 1) {
            const auto& previous = levels_.back();
            std::vector<Range> next((previous.size() + 1) / 2, empty());
            for (std::size_t i = 0; i < previous.size(); ++i) merge(next[i / 2], previous[i]);
            levels_.push_back(std::move(next));
        }
    }
    template<class Sample> Range query(std::size_t lo, std::size_t hi, Sample sample) const {
        Range result = empty();
        hi = std::min(hi, count_); lo = std::min(lo, hi);
        while (lo < hi && lo % block_size) add(result, sample(lo++));
        const std::size_t tail = hi - hi % block_size;
        while (lo + block_size <= hi) {
            std::size_t block = lo / block_size, level = 0, width = block_size;
            while (level + 1 < levels_.size() && block % 2 == 0 && width <= (tail - lo) / 2) {
                ++level; block /= 2; width *= 2;
            }
            merge(result, levels_[level][block]); lo += width;
        }
        while (lo < hi) add(result, sample(lo++));
        return result;
    }
private:
    static Range empty() { return {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()}; }
    static void add(Range& range, double value) {
        if (std::isfinite(value)) { range.first = std::min(range.first, value); range.second = std::max(range.second, value); }
    }
    static void merge(Range& range, const Range& other) {
        range.first = std::min(range.first, other.first); range.second = std::max(range.second, other.second);
    }
    std::size_t count_ = 0;
    std::vector<std::vector<Range>> levels_;
};
