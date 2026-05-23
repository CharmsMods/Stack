#pragma once

#include "settings/AppearanceTheme.h"
#include <string>

class StyleModule {
public:
    StyleModule();

    void Initialize(StackAppearance::AppearanceManager* appearanceManager);
    void RenderUI();
    void Shutdown();

    const char* GetName() const { return "Style"; }

private:
    void ApplyCurrentTheme();

    StackAppearance::AppearanceManager* m_Appearance = nullptr;
};
