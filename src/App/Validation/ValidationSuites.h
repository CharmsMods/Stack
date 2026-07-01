#pragma once

namespace Stack::Validation {

bool ValidateToneCurveAutoIntegration();
bool ValidateDevelopAutoSolveBehavior();
bool ValidateDevelopNodeSmoke();
bool ValidateDevelopRealRawSmoke(int rawArgCount, char** rawArgs);
bool ValidateRawWorkspaceLoadingSmoke(int rawArgCount, char** rawArgs);

} // namespace Stack::Validation
