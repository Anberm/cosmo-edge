#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "flow/common/AlgAlarmTypes.h"
#include "util/MsgBaseTypes.h"

namespace cosmo::alarm {

using AlarmBatchIndices = std::vector<size_t>;

// Build same-frame batches for ordinary target alarms. Non-batchable alarms are
// returned as single-element batches so their existing behavior is preserved.
std::vector<AlarmBatchIndices> BuildAlarmBatches(const std::deque<DataAlarmUnit>& alarms,
                                                 OnEventsPropertyType propertyType, unsigned int multiAlarms);

// Merge only the accepted members of one batch into a single outgoing alarm.
// The first accepted member remains the representative for legacy scalar
// fields, while target/box collections contain every accepted member.
DataAlarmUnit MergeAlarmBatch(const std::deque<DataAlarmUnit>& alarms,
                              const AlarmBatchIndices& acceptedIndices);

}  // namespace cosmo::alarm
