#pragma once

#include "App/IAppModule.h"
#include "Color/LutCreator.h"

#include <functional>
#include <string>

class ToolsModule : public IAppModule {
public:
    struct PreviewImageState {
        ColorLut::LutCreatorImage image;
        unsigned int texture = 0;
    };

    ToolsModule();
    ~ToolsModule() override;

    void Initialize() override;
    void RenderUI() override {}
    void RenderUI(std::function<void(ColorLut::LutPayload)> onOpenGeneratedLut);
    const char* GetName() override { return "Tools"; }

private:
    enum class ToolPage {
        Overview = 0,
        LutCreator
    };

    void ReleasePreviewImage(PreviewImageState& state);
    bool LoadPreviewImage(
        const std::string& path,
        PreviewImageState& state,
        const char* imageRole);
    void InvalidateGeneratedLut();
    void ApplyRuntimeMetadataToGeneratedPayload();
    bool GenerateCurrentLut();
    bool SaveCurrentLut(
        bool openInEditor,
        const std::function<void(ColorLut::LutPayload)>& onOpenGeneratedLut);
    void RenderShellHeader();
    void RenderToolTabs();
    void RenderOverview();
    void RenderLutCreator(const std::function<void(ColorLut::LutPayload)>& onOpenGeneratedLut);
    void RenderStatusBanner();
    void RenderLutCreatorInputs();
    void RenderLutCreatorMetadataEditor();
    void RenderLutCreatorSolveControls();
    void RenderLutCreatorActionPanel(const std::function<void(ColorLut::LutPayload)>& onOpenGeneratedLut);
    void RenderLutCreatorReadoutPanel();
    void RenderImageCard(
        const char* label,
        PreviewImageState& state,
        const char* dialogTitle,
        const char* loadButtonLabel);
    std::string BuildCurrentGeneratedTitle() const;
    std::string BuildDefaultCubeFileName() const;
    std::string BuildPairStatusLabel() const;
    bool IsReadyToGenerate() const;
    void SetStatus(std::string message, bool isError);

    ToolPage m_SelectedPage = ToolPage::LutCreator;
    PreviewImageState m_SourceImage;
    PreviewImageState m_TargetImage;
    ColorLut::LutCreatorSettings m_LutSettings;
    ColorLut::LutCreatorStats m_LastStats;
    ColorLut::LutPayload m_GeneratedPayload;
    bool m_HasGeneratedPayload = false;
    bool m_StatusIsError = false;
    std::string m_StatusMessage;
    std::string m_LastSavedCubePath;
    char m_LabelBuffer[128] = {};
    char m_TitleBuffer[128] = {};
};
