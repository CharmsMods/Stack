#pragma once
#include "BracketingJobRunner.h"
#include "BracketingState.h"
#include "Async/TaskSystem.h"

namespace Stack::Project {
// Returns true when prepared inspection data replaced the accepted preview.
bool TickBracketingPreparation(BracketingState&, BracketingJobRequest, Async::ActivityMetadata activity = {});
}
