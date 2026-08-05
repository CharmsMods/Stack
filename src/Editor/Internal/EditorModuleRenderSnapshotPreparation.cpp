#include "Editor/EditorModule.h"

#include "Raw/RawPreciseIntegration.h"

#include <new>
#include <stdexcept>

bool EditorModule::TryBuildRenderSnapshot(
    std::uint64_t generation,
    EditorRenderWorker::Snapshot& snapshot) noexcept {
    try {
        snapshot = BuildRenderSnapshot(generation);
        return true;
    } catch (const std::bad_alloc&) {
        // Handled below.
    } catch (const std::length_error&) {
        // Handled below.
    } catch (...) {
        // Malformed persisted payloads must not escape the UI render loop.
    }

    ResetIncompleteCompositeOutputRequestsForRetry();
    ResetIncompletePreviewRequestsForRetry();
    Stack::PreciseIntegration::IntegrationState& precise =
        m_RawWorkspaceAutoBaseUi.preciseStartingPoint;
    if (precise.active &&
        Stack::PreciseIntegration::IsRunning(precise.state) &&
        precise.identity.generation == generation) {
        try {
            Stack::PreciseIntegration::Cancel(
                precise,
                "The render snapshot could not be prepared safely.");
        } catch (...) {
            precise.active = false;
            precise.state =
                Stack::PreciseIntegration::LifecycleState::Canceled;
        }
    }
    return false;
}
