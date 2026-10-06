#include "App/Validation/Suites/HeaderActionRegistryValidation.h"
#include "App/HeaderActionRegistry.h"

#include <iostream>
#include <stdexcept>

namespace Stack::Validation {
namespace {
void RequireHeaderAction(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void ConsumeHeaderAction(HeaderActionRegistry& registry) {
    auto action = registry.Consume();
    RequireHeaderAction(static_cast<bool>(action), "Queued header action was lost.");
    action();
    RequireHeaderAction(!registry.Consume(), "Header action was consumed twice.");
}
}

bool ValidateHeaderActionRegistry() {
    try {
        HeaderActionRegistry registry;
        const HeaderActionRegistry::Bounds clip{100.0f, 0.0f, 300.0f, 40.0f};
        int selectedWorkspace = 0;
        int workspaceId = 17;
        int selectCalls = 0;
        RequireHeaderAction(registry.Add({80.0f, 0.0f, 160.0f, 40.0f}, clip,
            [workspaceId, &selectedWorkspace, &selectCalls] {
                selectedWorkspace = workspaceId;
                ++selectCalls;
            }), "Visible tab failed registration.");
        workspaceId = 42;
        RequireHeaderAction(!registry.QueueAt(99.0f, 20.0f),
            "A tab accepted a click outside the visible strip.");
        RequireHeaderAction(!registry.QueueAt(120.0f, 40.0f),
            "A tab accepted a click on its excluded bottom edge.");
        RequireHeaderAction(!registry.QueueAt(160.0f, 20.0f),
            "A tab accepted a click on its excluded right edge.");
        RequireHeaderAction(registry.QueueAt(100.0f, 0.0f),
            "A tab rejected its included top-left edge.");
        RequireHeaderAction(selectedWorkspace == 0, "Header action executed during hit testing.");

        registry.BeginFrame();
        registry.Add({100.0f, 0.0f, 160.0f, 40.0f}, clip,
            [workspaceId, &selectedWorkspace] { selectedWorkspace = workspaceId; });
        RequireHeaderAction(!registry.QueueAt(500.0f, 20.0f),
            "An unregistered point accepted a click.");
        ConsumeHeaderAction(registry);
        RequireHeaderAction(selectedWorkspace == 17 && selectCalls == 1,
            "Rebuilt tabs changed the workspace targeted by a queued action.");

        registry.BeginFrame();
        int createCalls = 0;
        RequireHeaderAction(!registry.Add({260.0f, 0.0f, 310.0f, 40.0f}, clip,
            [&createCalls] { ++createCalls; }, false),
            "A disabled create control registered an action.");
        RequireHeaderAction(!registry.QueueAt(280.0f, 20.0f) && !registry.Consume(),
            "A disabled create control accepted a click.");
        RequireHeaderAction(!registry.Add({300.0f, 0.0f, 340.0f, 40.0f}, clip,
            [&createCalls] { ++createCalls; }),
            "A fully clipped control registered an action.");
        RequireHeaderAction(registry.Add({260.0f, 0.0f, 310.0f, 40.0f}, clip,
            [&createCalls] { ++createCalls; }), "Create control failed registration.");
        RequireHeaderAction(!registry.QueueAt(300.0f, 20.0f),
            "A control accepted a click beyond the strip's right edge.");
        RequireHeaderAction(registry.QueueAt(280.0f, 20.0f),
            "Create control rejected a visible click.");
        registry.BeginFrame();
        ConsumeHeaderAction(registry);
        RequireHeaderAction(createCalls == 1, "Create action did not execute exactly once.");

        int closedWorkspace = 0;
        registry.Add({100.0f, 0.0f, 180.0f, 40.0f}, clip,
            [&selectedWorkspace] { selectedWorkspace = 99; });
        registry.Add({150.0f, 0.0f, 180.0f, 40.0f}, clip,
            [&closedWorkspace] { closedWorkspace = 17; });
        RequireHeaderAction(registry.QueueAt(165.0f, 20.0f),
            "Close control rejected a click.");
        ConsumeHeaderAction(registry);
        RequireHeaderAction(closedWorkspace == 17 && selectedWorkspace == 17,
            "Tab selection intercepted the overlapping close control.");

        std::cout << "PASS header actions: clipped bounds, disabled controls, stable workspace IDs, "
                     "deferred clicks across frames, and close hit priority.\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "FAIL header actions: " << error.what() << '\n';
        return false;
    }
}

} // namespace Stack::Validation
