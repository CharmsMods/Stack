#pragma once
#include "BracketingJobRunner.h"
#include "BracketingState.h"
#include "Async/TaskSystem.h"

namespace Stack::Project {
// Check before copying request inputs. Running workers need no per-frame request;
// completion after this check is collected on the next tick.
bool ShouldTickBracketingPreparation(const BracketingState&);
// Returns true when prepared inspection data replaced the accepted preview.
bool TickBracketingPreparation(BracketingState&, BracketingJobRequest, Async::ActivityMetadata activity = {});
}
