#pragma once

namespace EditorViewportPreview {

enum class Presentation {
    FilteredEdited,
    HoverOriginal,
    CleanProcessed,
    CleanProcessedCursorReadout,
    CleanProcessedInlineValues,
    StaticCompare,
};

struct StateInput {
    bool outputAvailable = false;
    bool sourceAvailable = false;
    bool imageHovered = false;
    bool staticCompareActive = false;
    bool commandKeysAllowed = false;
    bool wantTextInput = false;
    bool ctrlHeld = false;
    bool shiftHeld = false;
    bool valuesHeld = false;
    bool labelsFit = false;
    // Camera locking is intentionally not consulted by the presentation resolver.
    bool cameraLocked = false;
};

inline Presentation ResolvePresentation(const StateInput& input) {
    if (input.staticCompareActive && input.outputAvailable && input.sourceAvailable) {
        return Presentation::StaticCompare;
    }
    if (!input.outputAvailable || !input.imageHovered) {
        return Presentation::FilteredEdited;
    }

    const bool inspectionKeysAllowed =
        input.commandKeysAllowed &&
        !input.wantTextInput &&
        !input.ctrlHeld;
    if (inspectionKeysAllowed && input.shiftHeld) {
        if (!input.valuesHeld) {
            return Presentation::CleanProcessed;
        }
        return input.labelsFit
            ? Presentation::CleanProcessedInlineValues
            : Presentation::CleanProcessedCursorReadout;
    }

    return input.sourceAvailable
        ? Presentation::HoverOriginal
        : Presentation::FilteredEdited;
}

} // namespace EditorViewportPreview
