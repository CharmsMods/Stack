#pragma once
#include "Notifications/Notifier.h"
#include <array>
#include <string>

namespace StackAppearance {
class AppearanceManager;
struct PaletteWorkshopState {
    std::string error;
    std::array<char, 128> name{};
    Stack::Notifications::EventId saveError = 0;
};
void RenderPaletteWorkshop(AppearanceManager& appearance, PaletteWorkshopState& state,
    const Stack::Notifications::Notifier& notifier);
}
