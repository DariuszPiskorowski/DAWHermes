#pragma once

#include <string>

#include "core/ProjectModel.h"
#include "core/SelectionState.h"
#include "hermes/HermesTypes.h"

namespace dawhermes::hermes {

HermesCommandAvailability getHermesCommandAvailability(
    HermesCommand command,
    const core::ProjectModel& project,
    const core::SelectionState& selection);

std::string describeAvailability(HermesCommand command, HermesCommandAvailability availability);

}  // namespace dawhermes::hermes
