#include "Editor/EditorModule.h"
#include "App/WorkspacePresentation.h"
#include "RawLabGalleryDetails.h"

void EditorModule::RenderRawGalleryProperties(
    const Stack::RawWorkspace::SourceRecord* source,
    const std::shared_ptr<const Raw::RawMetadata>& metadata,
    const std::filesystem::path& projectPath,
    const std::function<void(const ImVec2&)>& thumbnail) {
    using Action = Stack::RawGalleryActions::Action;
    using namespace Stack::Editor::RawLabInternal;
    const auto count = m_RawWorkspace.selectedSourceKeys.size() + m_RawWorkspaceLabSelectedProjectPaths.size();
    // Availability needs one focused edit. Resolve the full multi-selection
    // only on a command; the panel must not rebuild bracket/source indexes.
    const auto editContext = count ? CaptureRawGalleryActionContext(true) : Stack::RawGalleryActions::Context{};
    GalleryInspectorActions actions;
    actions.selectionCount = count;
    if (editContext.focusedIndex < editContext.items.size()) {
        const auto& focused = editContext.items[editContext.focusedIndex];
        actions.focusedPath = &focused.path;
        actions.copyVisible = m_RawWorkspaceGalleryContentMode != Stack::RawWorkspace::GalleryContentMode::Gallery &&
            focused.kind == Stack::RawGalleryActions::ItemKind::Project;
    }
    const auto copy = actions.copyVisible ? Stack::RawGalleryActions::GetAvailability(editContext, Action::CopyEdits) :
        Stack::RawGalleryActions::Availability{};
    const auto paste = Stack::RawGalleryActions::GetAvailability(editContext, Action::PasteEdits);
    actions.copyEnabled = copy.enabled;
    actions.pasteEnabled = paste.enabled;
    actions.copyReason = copy.reason.c_str();
    actions.pasteReason = paste.reason.c_str();
    const bool hasLocation = !projectPath.empty() || (source && !source->absolutePath.empty());
    actions.locationEnabled = hasLocation && !Stack::Workspace::IsPreview();
    if (!m_RawWorkspaceGalleryDetails) m_RawWorkspaceGalleryDetails = std::make_shared<GalleryDetails>();
    const auto command = m_RawWorkspaceGalleryDetails->Render(source, metadata, projectPath,
        actions, GetWorkspaceBaseColor(), thumbnail);
    if (!command) return;
    if (*command == Action::ShowInExplorer) {
        Stack::RawGalleryActions::Context location;
        location.workspaceRoot = m_RawWorkspace.workspaceRoot;
        location.catalogGeneration = m_RawWorkspaceScanGeneration;
        location.previewOnly = Stack::Workspace::IsPreview();
        location.focusedIndex = 0;
        Stack::RawGalleryActions::Item item;
        item.kind = projectPath.empty() ? Stack::RawGalleryActions::ItemKind::Source : Stack::RawGalleryActions::ItemKind::Project;
        item.path = projectPath.empty() ? source->absolutePath : projectPath;
        location.items.push_back(std::move(item));
        ExecuteRawGalleryAction(*command, location);
    } else if (ExecuteRawGalleryAction(*command, CaptureRawGalleryActionContext()) && *command == Action::CopyEdits) {
        m_RawWorkspaceGalleryDetails->EditsCopied();
    }
}
