#pragma once
#include "recording_source.hpp"
namespace gui {
// Captures formulas/filter as values. No GUI access from replay/worker threads.
std::shared_ptr<const lvm::RecordingSource> processed_recording_source(bool apply_processing=true);
}
