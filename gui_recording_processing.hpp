#pragma once
#include "recording_source.hpp"
#include "resource_budget.hpp"
namespace gui {
// Move full resident inputs into an immutable worker owner without copying them.
bool share_resident_recording();
// Captures formulas/filter as values. No GUI access from replay/worker threads.
// Explicit cadence is used only when moving the resident Signal display to a worker.
std::shared_ptr<const lvm::RecordingSource> processed_recording_source(bool apply_processing=true,double display_filter_step=0,
    std::shared_ptr<lvm::MemoryBudget> checkpoint_budget={});
}
