#pragma once

#include "settings/AppearanceTheme.h"

#include <string>

class SettingsModule {
public:
    SettingsModule();

    void Initialize(StackAppearance::AppearanceManager* appearanceManager);
    void RenderUI();
    void Shutdown();

    const char* GetName() const { return "Settings"; }

private:
    void RenderAppearancePanel();
    void RenderPresetLibrary();
    void RenderThemeEditor();
    void SyncPresetNameBuffer();
    void ApplyCurrentTheme();
    void SetStatus(const std::string& message, bool isError = false);

    StackAppearance::AppearanceManager* m_Appearance = nullptr;
    char m_PresetNameBuffer[128] = {};
    std::string m_StatusMessage;
    bool m_StatusIsError = false;
};
