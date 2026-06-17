#pragma once

#include "UpdateManager.h"
#include "settings/AppearanceTheme.h"

class EditorModule;

namespace AppSettingsPopup {

enum class Category {
    Appearance = 0,
    Graph = 1,
    CanvasComposition = 2,
    Updates = 3
};

struct State {
    Category activeCategory = Category::Appearance;
    bool showInstallConfirmPopup = false;
    std::string lastActionError;
};

void RenderContents(
    StackAppearance::AppearanceManager* appearance,
    EditorModule* editor,
    AppUpdate::UpdateManager* updateManager,
    State& state);

} // namespace AppSettingsPopup
