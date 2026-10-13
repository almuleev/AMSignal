#pragma once
#include "analysis.hpp"
#include "recording_source.hpp"
namespace lvm {
class RecordingOverview;
struct DiskSpectrumOptions {
    std::uint64_t disk_bytes=64ULL*1024*1024*1024;
    std::filesystem::path temporary_directory;
    std::shared_ptr<const RecordingOverview> timeline;
};
class DiskSpectrum {
public:
    struct Bucket {std::size_t first=0,count=0,minimum_bin=0,maximum_bin=0;double first_value=0,last_value=0,minimum=0,maximum=0;};
    std::shared_ptr<MemoryBudget::Reservation> memory_reservation;
    struct Storage;
    std::shared_ptr<Storage> storage;
    std::vector<double> maxima;
    std::vector<std::vector<Bucket>> overview;
    std::size_t bins=0;
    std::uint64_t disk_bytes=0;
    void read(std::size_t channel,std::size_t first,std::size_t count,std::vector<double>& values) const;
    std::vector<std::pair<std::size_t,double>> display_points(std::size_t channel,std::size_t first,std::size_t count,std::size_t pixels) const;
};
// Full-record DFT with bounded resident scratch, including Bluestein. Never Welch.
Spectrum compute_disk_spectrum(std::shared_ptr<const RecordingSource> source,RecordingReadRequest read,
    std::vector<std::string> names,const AnalysisResources& resources={},const std::atomic<bool>* cancel=nullptr,
    const DiskSpectrumOptions& options={});
Spectrum compute_recording_spectrum(std::shared_ptr<const RecordingSource> source,RecordingReadRequest read,
    std::vector<std::string> names,const AnalysisResources& resources={},const std::atomic<bool>* cancel=nullptr,
    std::shared_ptr<const RecordingOverview> timeline={});
}
