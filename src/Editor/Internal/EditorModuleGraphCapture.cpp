#include "App/settings/PrimaryAction.h"
#include "Editor/EditorModule.h"

#include "Renderer/GLLoader.h"
#include "Utils/FileDialogs.h"
#include "Utils/ImGuiExtras.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <limits>

namespace {

namespace GraphCapture = Stack::EditorGraphCapture;
constexpr GLenum kGlMaxRenderbufferSize = 0x84E8;

std::string SanitizeCaptureFileStem(const std::string& value) {
    std::string result;
    result.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '_' || ch == '-') {
            result.push_back(static_cast<char>(ch));
        } else if (std::isspace(ch)) {
            result.push_back('_');
        }
    }
    while (!result.empty() && result.front() == '_') result.erase(result.begin());
    while (!result.empty() && result.back() == '_') result.pop_back();
    return result.empty() ? std::string("editor") : result;
}

GraphCapture::ResolutionLimits QueryCaptureLimits() {
    GLint textureLimit = 0;
    GLint renderbufferLimit = 0;
    GLint viewportLimits[2] = { 0, 0 };
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &textureLimit);
    glGetIntegerv(kGlMaxRenderbufferSize, &renderbufferLimit);
    glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewportLimits);

    int hardwareLimit = 16384;
    for (GLint value : { textureLimit, renderbufferLimit, viewportLimits[0], viewportLimits[1] }) {
        if (value > 0) {
            hardwareLimit = std::min(hardwareLimit, static_cast<int>(value));
        }
    }
    GraphCapture::ResolutionLimits limits;
    limits.maxEdge = std::max(256, hardwareLimit);
    return limits;
}

const char* BackgroundLabel(GraphCapture::Background background) {
    switch (background) {
        case GraphCapture::Background::CurrentAppearance: return "Current appearance";
        case GraphCapture::Background::SolidTheme: return "Solid theme";
        case GraphCapture::Background::Transparent: return "Transparent";
    }
    return "Current appearance";
}

const char* NodeStateLabel(GraphCapture::NodeState state) {
    switch (state) {
        case GraphCapture::NodeState::AsShown: return "As shown";
        case GraphCapture::NodeState::ExpandAll: return "Expand all";
        case GraphCapture::NodeState::CollapseAll: return "Collapse all";
    }
    return "As shown";
}

} // namespace

void EditorModule::OpenGraphCaptureWindow() {
    m_GraphCaptureWindowOpen = true;
    m_GraphCaptureFocusRequested = true;
}

bool EditorModule::ConsumeGraphCaptureRequest(GraphCapture::Request& outRequest) {
    if (!m_PendingGraphCaptureRequest.has_value()) {
        return false;
    }
    outRequest = std::move(*m_PendingGraphCaptureRequest);
    m_PendingGraphCaptureRequest.reset();
    return true;
}

void EditorModule::SetGraphCaptureProgress(std::string statusText) {
    m_GraphCaptureStatusText = std::move(statusText);
    GetNotifier().UpdateActivity(m_GraphCaptureActivity, m_GraphCaptureStatusText);
}

bool EditorModule::BeginGraphCaptureSave(GraphCapture::Request request, std::string& error) {
    std::error_code checkError;
    const auto approval = Stack::FileSave::CaptureTargetApproval(
        std::filesystem::u8path(request.targetPath), checkError);
    if (!approval) {
        error = "The graph image destination could not be checked: " + checkError.message();
        return false;
    }
    request.targetApproval = *approval;
    m_GraphCaptureActivity = GetNotifier().BeginActivity("Capturing graph");
    if (m_GraphCaptureActivity) m_NotificationDecisionOwners.push_back({m_GraphCaptureActivity.operationId,
        GetProjectDocumentId(), m_Project->files, m_Project->files->load.generation});
    m_PendingGraphCaptureRequest = std::move(request);
    m_GraphCaptureSavePending = false;
    m_GraphCaptureBusy = true;
    m_GraphCaptureStatusText = "Rendering graph off-screen...";
    GetNotifier().UpdateActivity(m_GraphCaptureActivity, m_GraphCaptureStatusText);
    return true;
}

void EditorModule::CompleteGraphCapture(GraphCapture::Result result) {
    m_LastGraphCaptureResult = std::move(result);
    m_GraphCaptureBusy = false;
    m_GraphCaptureStatusText = m_LastGraphCaptureResult.message;

    const bool fileSaved = m_LastGraphCaptureResult.fileSaved;
    const bool clipboardCopied = m_LastGraphCaptureResult.ClipboardSucceeded();
    const bool clipboardPartial = m_LastGraphCaptureResult.ClipboardPartiallySucceeded();
    const bool complete = fileSaved &&
        (!m_LastGraphCaptureResult.clipboardRequested || clipboardCopied);
    const auto outcome = complete ? Stack::Notifications::Outcome::Success :
        fileSaved || clipboardPartial ? Stack::Notifications::Outcome::Partial : Stack::Notifications::Outcome::Failure;
    const auto message = m_LastGraphCaptureResult.message.empty()
        ? std::string("Graph capture did not complete.") : m_LastGraphCaptureResult.message;
    GetNotifier().FinishActivity(m_GraphCaptureActivity, outcome, message, m_LastGraphCaptureResult.targetPath);
}

void EditorModule::RenderGraphCaptureCanvas(
    EditorNodeGraphUI& renderer,
    EditorNodeGraph::Graph& graph,
    StackAppearance::AppearanceManager* captureAppearance,
    const GraphCapture::Request& request,
    const ImVec2& canvasMin,
    const ImVec2& canvasMax) {
    StackAppearance::AppearanceManager* previousOverride = m_GraphCaptureAppearanceOverride;
    m_GraphCaptureAppearanceOverride = captureAppearance;
    renderer.RenderGraphCapture(
        this,
        graph,
        &m_Project->layers,
        canvasMin,
        canvasMax,
        request.settings,
        request.viewport);
    m_GraphCaptureAppearanceOverride = previousOverride;
}

void EditorModule::RenderGraphCaptureWindow() {
    if (!m_GraphCaptureWindowOpen || !m_Appearance) {
        return;
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        m_GraphCaptureWindowOpen = false;
        return;
    }

    const GraphCapture::GraphViewportSnapshot viewport =
        m_Sidebar.GetNodeGraphUI().GetGraphCaptureViewportSnapshot();
    if (viewport.IsValid()) {
        GraphCapture::ResolveLinkedResolution(m_GraphCaptureSettings, viewport.AspectRatio());
    }

    ImGuiViewport* mainViewport = ImGui::GetMainViewport();
    const float popupWidth = std::min(
        std::clamp(mainViewport->Size.x * 0.48f, 620.0f, 820.0f),
        std::max(320.0f, mainViewport->Size.x - 40.0f));
    const float popupHeight = std::min(
        std::clamp(mainViewport->Size.y * 0.80f, 620.0f, 880.0f),
        std::max(360.0f, mainViewport->Size.y - 40.0f));
    ImGui::SetNextWindowPos(mainViewport->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(popupWidth, popupHeight), ImGuiCond_Always);
    ImGui::SetNextWindowViewport(mainViewport->ID);
    if (m_GraphCaptureFocusRequested) {
        ImGui::SetNextWindowFocus();
        m_GraphCaptureFocusRequested = false;
    }

    const StackAppearance::RuntimeSurfacePalette palette = m_Appearance->GetRuntimeSurfacePalette();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f, 22.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 18.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, m_Appearance->GetEffectivePopupBackgroundColor());
    ImGui::PushStyleColor(ImGuiCol_Border, palette.border);
    ImGui::PushStyleColor(ImGuiCol_Separator, palette.separator);

    bool open = m_GraphCaptureWindowOpen;
    const bool began = ImGui::Begin(
        "##EditorGraphCapture",
        &open,
        ImGuiWindowFlags_NoDocking |
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoTitleBar);
    if (began) {
        ImGui::TextUnformatted("Capture Editor Graph");
        ImGui::SameLine();
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 74.0f));
        if (ImGui::Button("Close", ImVec2(74.0f, 0.0f))) {
            open = false;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::TextWrapped("Render a clean graph image at a resolution independent of the display. The selected image theme does not change Stack.");
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        const float footerHeight = 58.0f;
        ImGui::BeginChild("##GraphCaptureOptions", ImVec2(0.0f, -footerHeight), false);

        ImGuiExtras::RichSectionLabel("CAPTURE AREA", 4.0f);
        int scopeValue = static_cast<int>(m_GraphCaptureSettings.scope);
        if (ImGui::RadioButton("Entire graph", scopeValue == static_cast<int>(GraphCapture::Scope::EntireGraph))) {
            m_GraphCaptureSettings.scope = GraphCapture::Scope::EntireGraph;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Visible view", scopeValue == static_cast<int>(GraphCapture::Scope::VisibleView))) {
            m_GraphCaptureSettings.scope = GraphCapture::Scope::VisibleView;
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGuiExtras::RichSectionLabel("RESOLUTION", 4.0f);
        ImGui::TextDisabled("Aspect %.4f:1", viewport.AspectRatio());
        if (ImGui::BeginTable("##GraphCaptureDimensions", 2, ImGuiTableFlags_SizingStretchSame)) {
            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Width");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            int width = m_GraphCaptureSettings.width;
            if (ImGui::InputInt("##GraphCaptureWidth", &width, 0, 0)) {
                m_GraphCaptureSettings.width = std::max(1, width);
                m_GraphCaptureSettings.resolutionDriver = GraphCapture::ResolutionDriver::Width;
                m_GraphCaptureSettings.resolutionInitialized = true;
                GraphCapture::ResolveLinkedResolution(m_GraphCaptureSettings, viewport.AspectRatio());
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted("Height");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
            int height = m_GraphCaptureSettings.height;
            if (ImGui::InputInt("##GraphCaptureHeight", &height, 0, 0)) {
                m_GraphCaptureSettings.height = std::max(1, height);
                m_GraphCaptureSettings.resolutionDriver = GraphCapture::ResolutionDriver::Height;
                m_GraphCaptureSettings.resolutionInitialized = true;
                GraphCapture::ResolveLinkedResolution(m_GraphCaptureSettings, viewport.AspectRatio());
            }
            ImGui::EndTable();
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGuiExtras::RichSectionLabel("IMAGE APPEARANCE", 4.0f);
        std::string themePreview = "Current — " + m_Appearance->GetActivePresetDisplayName();
        if (!m_GraphCaptureSettings.themePresetId.empty()) {
            if (const StackAppearance::ThemeDefinition* preset =
                    m_Appearance->GetPresetById(m_GraphCaptureSettings.themePresetId)) {
                themePreview = preset->displayName;
            } else {
                m_GraphCaptureSettings.themePresetId.clear();
            }
        }
        ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("Capture palette", themePreview.c_str())) {
            if (ImGui::Selectable("Current palette preview", m_GraphCaptureSettings.themePresetId.empty())) {
                m_GraphCaptureSettings.themePresetId.clear();
            }
            for (const StackAppearance::ThemeDefinition& theme : m_Appearance->GetFactoryThemes()) {
                if (ImGui::Selectable(theme.displayName.c_str(), m_GraphCaptureSettings.themePresetId == theme.id)) {
                    m_GraphCaptureSettings.themePresetId = theme.id;
                }
            }
            for (const StackAppearance::CreamPalette& theme : m_Appearance->GetLibrary().creamVariants) {
                const std::string label = theme.name + "##CaptureCreamVariant" + theme.id;
                if (ImGui::Selectable(label.c_str(), m_GraphCaptureSettings.themePresetId == theme.id)) {
                    m_GraphCaptureSettings.themePresetId = theme.id;
                }
            }
            ImGui::EndCombo();
        }

        int backgroundValue = static_cast<int>(m_GraphCaptureSettings.background);
        ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
        if (ImGui::BeginCombo("Background", BackgroundLabel(m_GraphCaptureSettings.background))) {
            for (GraphCapture::Background background : {
                     GraphCapture::Background::CurrentAppearance,
                     GraphCapture::Background::SolidTheme,
                     GraphCapture::Background::Transparent }) {
                if (ImGui::Selectable(
                        BackgroundLabel(background),
                        backgroundValue == static_cast<int>(background))) {
                    m_GraphCaptureSettings.background = background;
                    if (background == GraphCapture::Background::Transparent) {
                        m_GraphCaptureSettings.format = GraphCapture::Format::Png;
                    }
                }
            }
            ImGui::EndCombo();
        }
        ImGui::Checkbox("Show background grid", &m_GraphCaptureSettings.showGrid);

        if (m_GraphCaptureSettings.scope == GraphCapture::Scope::EntireGraph) {
            ImGui::Dummy(ImVec2(0.0f, 10.0f));
            ImGuiExtras::RichSectionLabel("ENTIRE GRAPH LAYOUT", 4.0f);
            ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
            if (ImGui::BeginCombo("Node state", NodeStateLabel(m_GraphCaptureSettings.nodeState))) {
                for (GraphCapture::NodeState state : {
                         GraphCapture::NodeState::AsShown,
                         GraphCapture::NodeState::ExpandAll,
                         GraphCapture::NodeState::CollapseAll }) {
                    if (ImGui::Selectable(NodeStateLabel(state), m_GraphCaptureSettings.nodeState == state)) {
                        m_GraphCaptureSettings.nodeState = state;
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SetNextItemWidth(std::min(420.0f, ImGui::GetContentRegionAvail().x));
            ImGui::SliderFloat("Padding", &m_GraphCaptureSettings.paddingPercent, 0.0f, 25.0f, "%.0f%%");
        }

        ImGui::Dummy(ImVec2(0.0f, 10.0f));
        ImGuiExtras::RichSectionLabel("FILE FORMAT", 4.0f);
        if (ImGui::RadioButton("PNG", m_GraphCaptureSettings.format == GraphCapture::Format::Png)) {
            m_GraphCaptureSettings.format = GraphCapture::Format::Png;
        }
        ImGui::SameLine();
        const bool transparent = m_GraphCaptureSettings.background == GraphCapture::Background::Transparent;
        ImGui::BeginDisabled(transparent);
        if (ImGui::RadioButton("BMP", m_GraphCaptureSettings.format == GraphCapture::Format::Bmp)) {
            m_GraphCaptureSettings.format = GraphCapture::Format::Bmp;
        }
        ImGui::EndDisabled();
        if (transparent) {
            ImGui::TextDisabled("Transparent disk output requires PNG. Clipboard alpha remains enabled.");
        }

        const GraphCapture::ResolutionLimits limits = QueryCaptureLimits();
        std::string validationError;
        bool valid = viewport.IsValid() && GraphCapture::ValidateResolution(
            m_GraphCaptureSettings.width,
            m_GraphCaptureSettings.height,
            limits,
            &validationError);
        const int drivenEdge = m_GraphCaptureSettings.resolutionDriver == GraphCapture::ResolutionDriver::Width
            ? m_GraphCaptureSettings.width
            : m_GraphCaptureSettings.height;
        if (valid && drivenEdge < limits.minDrivenEdge) {
            valid = false;
            validationError = "The edited resolution edge must be at least 256 pixels.";
        }
        if (valid && m_GraphCaptureSettings.scope == GraphCapture::Scope::EntireGraph && m_Project->graph.GetNodes().empty()) {
            valid = false;
            validationError = "Entire graph capture requires at least one node.";
        }
        if (!viewport.IsValid()) {
            validationError = "The Editor graph canvas is not currently available.";
        }

        const std::uint64_t pixels =
            static_cast<std::uint64_t>(std::max(0, m_GraphCaptureSettings.width)) *
            static_cast<std::uint64_t>(std::max(0, m_GraphCaptureSettings.height));
        const double workingMiB = static_cast<double>(pixels) * 8.0 / (1024.0 * 1024.0);
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Text("Output: %d x %d", m_GraphCaptureSettings.width, m_GraphCaptureSettings.height);
        ImGui::TextDisabled("Estimated GPU + readback memory: %.1f MiB", workingMiB);
        if (!validationError.empty()) {
            const ImVec4 errorColor=m_Appearance
                ? m_Appearance->ResolveSemanticUiColor(StackAppearance::SemanticUiColor::Error,ImVec4(0.94f,0.52f,0.44f,1.0f))
                : ImVec4(0.94f,0.52f,0.44f,1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, errorColor);
            ImGui::TextWrapped("%s", validationError.c_str());
            ImGui::PopStyleColor();
        }
        if (!m_GraphCaptureStatusText.empty()) {
            ImGui::TextWrapped("%s", m_GraphCaptureStatusText.c_str());
        }

        ImGui::EndChild();
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));

        const GraphCapture::ResolutionLimits footerLimits = QueryCaptureLimits();
        std::string footerValidation;
        bool captureEnabled = viewport.IsValid() && GraphCapture::ValidateResolution(
            m_GraphCaptureSettings.width,
            m_GraphCaptureSettings.height,
            footerLimits,
            &footerValidation);
        const int footerDrivenEdge = m_GraphCaptureSettings.resolutionDriver == GraphCapture::ResolutionDriver::Width
            ? m_GraphCaptureSettings.width
            : m_GraphCaptureSettings.height;
        captureEnabled = captureEnabled && footerDrivenEdge >= footerLimits.minDrivenEdge;
        captureEnabled = captureEnabled &&
            !(m_GraphCaptureSettings.scope == GraphCapture::Scope::EntireGraph && m_Project->graph.GetNodes().empty());
        captureEnabled = captureEnabled && !m_GraphCaptureBusy && !m_GraphCaptureSavePending;

        ImGui::BeginDisabled(!captureEnabled);
        if (StackAppearance::PrimaryActionButton(
                m_GraphCaptureBusy ? "Capturing..." : "Capture",
                ImVec2(std::min(180.0f, ImGui::GetContentRegionAvail().x), 36.0f),
                m_Appearance ? &m_Appearance->GetResolvedCreamPalette().primaryAction : nullptr)) {
            const bool allowBmp = m_GraphCaptureSettings.background != GraphCapture::Background::Transparent;
            const FileDialogs::RasterImageFormat preferred =
                m_GraphCaptureSettings.format == GraphCapture::Format::Bmp
                    ? FileDialogs::RasterImageFormat::Bmp
                    : FileDialogs::RasterImageFormat::Png;
            const std::string stem = SanitizeCaptureFileStem(
                m_Project->name.empty() ? std::string("editor") : m_Project->name) + "_graph";
            const std::string defaultName = stem +
                (preferred == FileDialogs::RasterImageFormat::Bmp ? ".bmp" : ".png");
            const FileDialogs::RasterImageSaveResult save = FileDialogs::SaveGraphImageFileDialog(
                "Save Editor Graph Image",
                defaultName.c_str(),
                preferred,
                allowBmp);
            if (save) {
                m_GraphCaptureSettings.format = save.format == FileDialogs::RasterImageFormat::Bmp
                    ? GraphCapture::Format::Bmp
                    : GraphCapture::Format::Png;
                GraphCapture::Request request;
                request.settings = m_GraphCaptureSettings;
                request.viewport = viewport;
                request.targetPath = save.path;
                if (m_GraphCaptureSettings.themePresetId.empty()) {
                    request.theme = m_Appearance->GetWorkingTheme();
                } else if (const StackAppearance::ThemeDefinition* theme =
                               m_Appearance->GetPresetById(m_GraphCaptureSettings.themePresetId)) {
                    request.theme = *theme;
                } else {
                    request.theme = m_Appearance->GetWorkingTheme();
                    request.settings.themePresetId.clear();
                }
                std::error_code checkError;
                const auto review = Stack::FileSave::ReviewNormalizedTarget(
                    std::filesystem::u8path(save.selectedPath), std::filesystem::u8path(save.path), checkError);
                if (review == Stack::FileSave::NormalizedTargetAction::Unavailable) {
                    m_GraphCaptureStatusText = "The graph image destination could not be checked: " + checkError.message();
                    GetNotifier().Error(m_GraphCaptureStatusText, "Graph image was not saved");
                } else if (review == Stack::FileSave::NormalizedTargetAction::ConfirmReplacement) {
                    namespace N = Stack::Notifications;
                    const auto document = GetProjectDocumentId();
                    const auto revision = m_Project->graph.GetStructureRevision();
                    const auto generation = ++m_GraphCaptureSaveGeneration;
                    m_GraphCaptureSavePending = true;
                    m_GraphCaptureStatusText = "Waiting for replacement confirmation.";
                    N::NoticeSpec notice;
                    notice.title = "Replace graph image?";
                    notice.message = "An image already exists at the filename for the selected format.";
                    notice.details = save.path;
                    notice.route = N::Route::Center;
                    notice.foreground = m_NotificationForeground;
                    notice.operationId = GetNotifier().NewOperation();
                    N::ActionSpec replace;
                    replace.label = "Replace";
                    replace.destructive = true;
                    replace.canInvoke = [this, document, revision, generation] {
                        return GetProjectDocumentId() == document &&
                            m_Project->graph.GetStructureRevision() == revision &&
                            m_GraphCaptureSaveGeneration == generation && m_GraphCaptureSavePending && !m_GraphCaptureBusy;
                    };
                    replace.invoke = [this, request] {
                        std::string error;
                        return BeginGraphCaptureSave(request, error)
                            ? N::ActionResult::Success() : N::ActionResult::Failure(std::move(error));
                    };
                    N::ActionSpec cancel;
                    cancel.label = "Cancel";
                    cancel.safeCancel = true;
                    cancel.invoke = [this, generation] {
                        if (m_GraphCaptureSaveGeneration == generation) {
                            m_GraphCaptureSavePending = false;
                            m_GraphCaptureStatusText = "Graph image save cancelled.";
                        }
                        return N::ActionResult::Success();
                    };
                    notice.actions = {std::move(replace), std::move(cancel)};
                    RequestNotificationDecision(std::move(notice));
                } else {
                    std::string error;
                    if (!BeginGraphCaptureSave(std::move(request), error)) {
                        m_GraphCaptureStatusText = error;
                        GetNotifier().Error(std::move(error), "Graph image was not saved");
                    }
                }
            }
        }
        ImGui::EndDisabled();
        if (m_GraphCaptureBusy) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_GraphCaptureStatusText.c_str());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar(3);
    m_GraphCaptureWindowOpen = open;
}
