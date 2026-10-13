#pragma once
#include "frf_analysis.hpp"
#include <functional>
#include <memory>
#include <stdexcept>

namespace lvm {
class FrfSourceFailure:public std::runtime_error {
public:
    FrfError error;
    explicit FrfSourceFailure(FrfError e):std::runtime_error(frf_error_text(e)),error(e) {}
};
using FrfBlockConsumer = std::function<void(const std::vector<double>&,
                                           const std::vector<std::vector<double>>&)>;
struct FrfSource {
    // Selected references first, responses next. Replay owns its file/config.
    std::size_t references=0, responses=0;
    std::function<void(const FrfBlockConsumer&,const std::atomic<bool>*)> replay;
    std::size_t expected_samples=0; // Exact selected N, when known; never a cap.
};
struct PreparedFrf {
    std::shared_ptr<MemoryBudget::Reservation> memory_reservation;
    FrfSamples metadata; // No sample vectors in streamed inputs.
    std::vector<FrfError> response_errors;
    std::shared_ptr<const FrfBatchInput> memory;
    struct Storage;
    std::shared_ptr<Storage> storage;
    std::size_t resident_bytes=0, disk_bytes=0;
    // Reads averaged reference and all responses, retaining acquisition order.
    void read(std::size_t first,std::size_t count,std::vector<std::vector<double>>& channels,
              const std::atomic<bool>* cancel=nullptr) const;
};
std::shared_ptr<const PreparedFrf> prepare_frf_batch(FrfBatchInput input,const std::atomic<bool>* cancel=nullptr,
    std::shared_ptr<MemoryBudget::Reservation> reservation={});
std::shared_ptr<const PreparedFrf> prepare_frf_stream(const FrfSource& source,const std::atomic<bool>* cancel=nullptr,
    std::shared_ptr<MemoryBudget::Reservation> reservation={},const AnalysisResources& resources={});
FrfSamples inspect_frf_timeline(const FrfSource& source,const std::atomic<bool>* cancel=nullptr,
                              std::size_t expected_samples=1048577);
FrfBatchResult compute_prepared_frf(const PreparedFrf& input,const FrfOptions& options={},
                                   const std::atomic<bool>* cancel=nullptr,const AnalysisResources& resources={});
}
