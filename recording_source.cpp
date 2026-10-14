#include "recording_source.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <numeric>

namespace lvm {
namespace {
void check_cancel(const std::atomic<bool>* cancel) {
    if (cancel && cancel->load(std::memory_order_relaxed))
        throw RecordingFailure(RecordingError::Cancelled, "Operation cancelled.");
}
RecordingStamp read_stamp(const std::filesystem::path& path, RecordingError failure) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec) throw RecordingFailure(failure, "Cannot access the recording source.");
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec) throw RecordingFailure(failure, "Cannot inspect the recording source.");
    return {bytes, modified};
}
bool same_stamp(const RecordingStamp& a, const RecordingStamp& b) {
    return a.bytes == b.bytes && a.modified == b.modified;
}
std::size_t bounded_rows(const RecordingReadRequest& request) {
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (!request.block_rows || !request.block_bytes || request.channels.size() >= maximum / 16)
        throw RecordingFailure(RecordingError::InvalidRequest, "Invalid recording block allowance.");
    const auto row_bytes = (request.channels.size() + 1) * 16;
    const auto rows = request.block_bytes / row_bytes;
    if (!rows)
        throw RecordingFailure(RecordingError::ResourceLimit, "A recording row exceeds the block allowance.");
    return std::min<std::size_t>({request.block_rows, rows, 65536});
}
} // namespace

std::shared_ptr<const RecordingSource> RecordingSource::from_file(
    const std::filesystem::path& path, TimeRange bounds,
    std::vector<std::size_t> physical_columns, std::shared_ptr<const ScanIndex> index,
    std::optional<RecordingStamp> expected,double filter_sample_step) {
    auto source = std::shared_ptr<RecordingSource>(new RecordingSource);
    if(!std::isfinite(filter_sample_step) || filter_sample_step<0)
        throw RecordingFailure(RecordingError::InvalidRequest,"Invalid source filter cadence.");
    source->filter_sample_step_=filter_sample_step;
    std::error_code ec;
    source->path_ = std::filesystem::absolute(path, ec);
    if (ec) throw RecordingFailure(RecordingError::ReadFailure, "Cannot resolve the recording path.");
    source->stamp_ = read_stamp(source->path_, expected || index
        ? RecordingError::SourceChanged : RecordingError::ReadFailure);
    if ((expected && !same_stamp(*expected, *source->stamp_)) ||
        (index && !same_stamp({index->file_size, index->modified}, *source->stamp_)))
        throw RecordingFailure(RecordingError::SourceChanged, "The source file changed. Open it again.");
    source->bounds_ = index ? TimeRange{index->range_start, index->range_end} : bounds;
    if (!source->bounds_.valid() ||
        std::find(physical_columns.begin(), physical_columns.end(), 0) != physical_columns.end())
        throw RecordingFailure(RecordingError::InvalidRequest, "Invalid recording metadata.");
    source->physical_columns_ = std::move(physical_columns);
    source->index_ = std::move(index);
    return source;
}

std::shared_ptr<const RecordingSource> RecordingSource::from_memory(Dataset&& data) {
    if (!data.ok || data.time.empty() || !std::is_sorted(data.time.begin(), data.time.end()) ||
        !std::all_of(data.time.begin(), data.time.end(), [](double t) { return std::isfinite(t); }) ||
        !std::all_of(data.channels.begin(), data.channels.end(), [&](const auto& c) { return c.size() == data.rows(); }) ||
        (!data.source_columns.empty() && data.source_columns.size() != data.channel_count()))
        throw RecordingFailure(RecordingError::InvalidRequest, "A memory recording must contain prepared aligned samples.");
    auto source = std::shared_ptr<RecordingSource>(new RecordingSource);
    source->bounds_ = {data.time.front(), data.time.back()};
    source->physical_columns_ = data.source_columns;
    if (source->physical_columns_.empty()) {
        source->physical_columns_.resize(data.channel_count());
        std::iota(source->physical_columns_.begin(), source->physical_columns_.end(), std::size_t{1});
    }
    source->memory_ = std::make_shared<const Dataset>(std::move(data));
    return source;
}

std::uint64_t RecordingSource::dataset_array_bytes(const Dataset& data) {
    auto bytes=saturated_add(data.time.capacity()*sizeof(double),data.raw_time.capacity()*sizeof(double));
    bytes=saturated_add(bytes,data.channels.capacity()*sizeof(std::vector<double>));
    for(const auto& values:data.channels)bytes=saturated_add(bytes,values.capacity()*sizeof(double));
    return bytes;
}
std::shared_ptr<const RecordingSource> RecordingSource::from_resident(Dataset& data,
    std::shared_ptr<const RecordingSource> file,std::shared_ptr<MemoryBudget> budget,double filter_step) {
    if(!file || !file->file_backed() || !budget || !data.ok || data.rows()<3 ||
       data.source_columns!=file->physical_columns() || data.time.front()!=file->bounds().start ||
       data.time.back()!=file->bounds().end || !std::isfinite(filter_step) || filter_step<=0 ||
       !std::is_sorted(data.time.begin(),data.time.end()) ||
       !std::all_of(data.time.begin(),data.time.end(),[](double t){return std::isfinite(t);}) ||
       !std::all_of(data.channels.begin(),data.channels.end(),[&](const auto& values){return values.size()==data.rows();}))
        throw RecordingFailure(RecordingError::InvalidRequest,"Invalid resident recording transfer.");
    auto source=std::const_pointer_cast<RecordingSource>(from_file(file->path(),file->bounds(),
        file->physical_columns(),file->scan_index(),file->stamp(),filter_step));
    auto memory=std::make_shared<Dataset>();
    source->resident_reservation_=budget->transfer_resident(dataset_array_bytes(data));
    if(!source->resident_reservation_)
        throw RecordingFailure(RecordingError::ResourceLimit,"Resident recording accounting is unavailable.");
    // No throwing operations after the transfer: the document can replace its
    // now-empty Dataset with its preallocated metadata/seed immediately.
    *memory=std::move(data);source->memory_=std::move(memory);return source;
}

void RecordingSource::check_current() const {
    if(raw_) {raw_->check_current();return;}
    if (stamp_ && !same_stamp(*stamp_, read_stamp(path_, RecordingError::SourceChanged)))
        throw RecordingFailure(RecordingError::SourceChanged, "The source file changed. Open it again.");
}

std::size_t RecordingSource::replay(const RecordingReadRequest& request,
    const RecordingBlockConsumer& consume, const std::atomic<bool>* cancel) const {
    check_cancel(cancel);
    if (!consume) throw RecordingFailure(RecordingError::InvalidRequest, "A recording reader needs a block consumer.");
    TimeRange range;
    if (!request.range.resolve(bounds_, range) ||
        std::any_of(request.channels.begin(), request.channels.end(), [&](auto c) { return c >= channel_count(); }))
        throw RecordingFailure(RecordingError::InvalidRequest, "Analysis range or channels are outside the recording.");
    const auto rows = bounded_rows(request);
    check_current();
    std::size_t count = 0;
    const auto visitor = [&](const auto& time, const auto& values) {
        check_cancel(cancel);
        check_current();
        if (time.size() > std::numeric_limits<std::size_t>::max() - count)
            throw RecordingFailure(RecordingError::ResourceLimit, "Recording sample count overflow.");
        consume(time, values); // Borrowed until return; consumers must not publish partial results.
        count += time.size();
        check_cancel(cancel);
        check_current();
    };
    try {
        if(replay_) {
            auto bounded=request;bounded.block_rows=rows;
            replay_(bounded,visitor,cancel);
        } else if (memory_) {
            const auto first = std::lower_bound(memory_->time.begin(), memory_->time.end(), range.start);
            const auto last = std::upper_bound(first, memory_->time.end(), range.end);
            std::vector<double> time;
            std::vector<std::vector<double>> values(request.channels.size());
            for (auto at = first; at != last;) {
                check_cancel(cancel);
                const auto length = std::min<std::size_t>(rows, last - at);
                const auto offset = at - memory_->time.begin();
                time.assign(at, at + length);
                for (std::size_t c = 0; c < values.size(); ++c) {
                    const auto& input = memory_->channels[request.channels[c]];
                    values[c].assign(input.begin() + offset, input.begin() + offset + length);
                }
                visitor(time, values);
                at += length;
            }
        } else {
            LoadOptions options;
            options.use_time_window = true;
            options.time_start = range.start;
            options.time_end = range.end;
            options.scan_index = index_;
            options.block_rows = rows;
            options.cancel_flag = cancel;
            options.consume_block = visitor;
            for (auto c : request.channels) options.stream_columns.push_back(physical_columns_[c]);
            const auto parsed = read_lvm_file(path_, options);
            check_cancel(cancel);
            check_current();
            if (!parsed.ok)
                throw RecordingFailure(RecordingError::ReadFailure, parsed.error);
        }
    } catch (const std::bad_alloc&) {
        throw RecordingFailure(RecordingError::ResourceLimit, "Not enough memory for a recording block.");
    }
    check_cancel(cancel);
    check_current();
    if (!count)
        throw RecordingFailure(RecordingError::ReadFailure, "No data found in the selected time range.");
    return count;
}

std::shared_ptr<const RecordingSource> RecordingSource::processed(std::shared_ptr<const RecordingSource> raw,
                                                               std::string identity,Replay replay) {
    if(!raw || !replay || identity.empty())throw RecordingFailure(RecordingError::InvalidRequest,"Invalid processing view.");
    raw->check_current();auto source=std::shared_ptr<RecordingSource>(new RecordingSource);
    source->bounds_=raw->bounds_;source->path_=raw->path_;source->stamp_=raw->stamp_;
    source->physical_columns_=raw->physical_columns_;source->raw_=std::move(raw);
    source->processing_identity_="\nprocessed-v1:"+std::move(identity);source->replay_=std::move(replay);
    return source;
}

} // namespace lvm
