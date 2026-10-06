#pragma once

#include "AppLegal.h"
#include "UpdateManager.h"
#include "settings/AppearanceTheme.h"
#include "settings/PaletteWorkshop.h"
#include "Notifications/Notifier.h"
#include <unordered_map>

class EditorModule;

namespace AppSettingsPopup {

enum class Category {
    Appearance = 0,
    Background = 1,
    Raw = 2,
    Graph = 3,
    Viewport = 4,
    CanvasComposition = 5,
    Experimental = 6,
    Updates = 7,
    Legal = 8
};

struct State {
    Category activeCategory = Category::Appearance;
    bool showInstallConfirmPopup = false;
    bool requestShowLegalGate = false;
    std::string lastActionError;
    Stack::Notifications::Notifier notifier;
    std::unordered_map<std::string, Stack::Notifications::EventId> actionErrors;
    StackAppearance::PaletteWorkshopState paletteWorkshop;
    Stack::Notifications::OperationId installOperation = 0;
    std::string installTarget;
};

void UpdateNotifications(AppUpdate::UpdateManager* updateManager, State& state);

void RenderContents(
    StackAppearance::AppearanceManager* appearance,
    EditorModule* editor,
    AppUpdate::UpdateManager* updateManager,
    AppLegal::Manager* legalManager,
    State& state);

} // namespace AppSettingsPopup
