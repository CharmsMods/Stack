#pragma once
#include "Editor/EditorModuleTypes.h"
#include "Raw/RawZoneArea.h"

namespace Stack::Editor::RawLabInternal {
bool DrawRawLabAreaGraph(RawRecipe::RawZoneArea& area,
    EditorModuleTypes::RawZoneAreaGraphState& state,
    const RawRecipe::RawZoneAreaStatistics* statistics, bool maskGestureActive);
}
