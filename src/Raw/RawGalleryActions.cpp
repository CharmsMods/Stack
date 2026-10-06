#include "Raw/RawGalleryActions.h"

#include <algorithm>

namespace Stack::RawGalleryActions {

Availability GetAvailability(const Context& context, Action action) {
    const auto disabled = [](const char* reason) { return Availability{false, reason}; };
    if (context.previewOnly) return disabled("Gallery actions are unavailable during the workspace preview.");
    if (context.items.empty()) return disabled("Select an image or project first.");
    const bool projectsOnly = std::all_of(context.items.begin(), context.items.end(),
        [](const Item& item) { return item.kind == ItemKind::Project; });
    const auto* focused = context.focusedIndex < context.items.size()
        ? &context.items[context.focusedIndex] : nullptr;
    switch (action) {
    case Action::Open:
        if (!context.openAvailable) return disabled("Opening Gallery items is unavailable in this view.");
        if (context.foregroundBusy) return disabled("Finish the current project operation before opening another item.");
        break;
    case Action::CreateCaptureSet:
        if (!context.openAvailable) return disabled("Creating a capture set is unavailable in this view.");
        if (context.bracketSources.size() < 2u) return disabled("Select at least two original captures for a capture set.");
        if (context.foregroundBusy) return disabled("Finish the current project operation before creating a capture set.");
        break;
    case Action::AddToQueue:
        if (!context.queueAvailable) return disabled("Queue is unavailable in this view.");
        break;
    case Action::CopyEdits:
        if (!focused || !focused->canCopyEdits) return disabled("The focused item has no RAW edit to copy.");
        break;
    case Action::PasteEdits:
        if (!context.hasEditClipboard) return disabled("Copy RAW edits first.");
        break;
    case Action::ShowInExplorer:
        if (context.items.size() != 1u) return disabled("Select one image or project to reveal in Explorer.");
        break;
    case Action::CopyPath:
        break;
    case Action::NewSavedVersion:
        if (!projectsOnly || context.items.size() != 1u || !context.items.front().canVersion)
            return disabled("Select one saved managed project to create an independent version.");
        if (context.items.front().protectedProject)
            return disabled("Close this project in all tabs first. Versions copy the saved project.");
        if (context.foregroundBusy) return disabled("Finish the current project operation before creating a version.");
        break;
    case Action::TrashProjects:
    case Action::RevertProjects:
        if (!projectsOnly) return disabled("Only saved projects can be moved to Stack Trash. Original RAW files stay read-only.");
        if (std::any_of(context.items.begin(), context.items.end(),
                [](const Item& item) { return item.protectedProject; }))
            return disabled("Close the selected projects in all tabs before moving them to Stack Trash.");
        break;
    }
    return {true, {}};
}

const char* Label(Action action) {
    switch (action) {
    case Action::Open: return "Open";
    case Action::CreateCaptureSet: return "Create Capture Set";
    case Action::AddToQueue: return "Add to Queue";
    case Action::CopyEdits: return "Copy edits";
    case Action::PasteEdits: return "Paste edits...";
    case Action::ShowInExplorer: return "Show in Explorer";
    case Action::CopyPath: return "Copy path";
    case Action::NewSavedVersion: return "New saved version";
    case Action::TrashProjects: return "Move project to Stack Trash";
    case Action::RevertProjects: return "Revert saved project...";
    }
    return "Gallery action";
}

} // namespace Stack::RawGalleryActions
