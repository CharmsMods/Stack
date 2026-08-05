#pragma once

#include "UpdateManager.h"
#include "settings/AppearanceTheme.h"

class EditorModule;

namespace AppSettingsPopup {

enum class Category {
    Appearance = 0,
    Background = 1,
    Graph = 2,
    Viewport = 3,
    CanvasComposition = 4,
    Experimental = 5,
    Updates = 6
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
