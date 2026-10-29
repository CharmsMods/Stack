#include "Editor/EditorModule.h"

void EditorModule::ShowRawWorkspaceSourceFolders() {
    if (!m_PermanentGalleryWorkspace) return;
    RememberRawWorkspaceFolderCards();
    RequestRawWorkspaceFolderCardPreviews();
    m_RawGallerySourceFoldersOpen = true;
    if(m_GalleryNavigationMotion)m_GalleryNavigationMotion->Folders(true,m_GalleryBrowseAnchor);
    m_RawWorkspaceGalleryLayoutAnimation.Reset();
    m_RawWorkspaceGalleryPreviewHovered = false;
    m_RawWorkspaceLabFilmstripHoverSourceKey.clear();
    m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
    m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.clear();
    m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.clear();
    m_RawWorkspaceLabFilmstripHoverOpacity = 0.f;
    m_RawWorkspaceLabFilmstripDrawerPointerInside = false;
    m_RawWorkspaceLabFilmstripDrawerInteractionRetained = false;
}

void EditorModule::RenderRawGallerySourceFolders(const ImVec2& size) {
    const auto& cards = GetRawWorkspaceSourceFolderCards();
    const auto action = m_RawGalleryFolderCards.Render(cards, size, GetWorkspaceBaseColor(),
        [this](const std::filesystem::path&, const Stack::RawWorkspace::SourceRecord& source) {
            // The revisioned card snapshot already supplies an independent,
            // root-qualified display key; borrow its metadata on warm frames.
            Stack::Editor::RawLabInternal::GalleryFolderCards::Texture result;
            result.handle = GetRawWorkspaceThumbnailTexture(source, &result.width, &result.height, false);
            if (const auto found = m_RawWorkspaceThumbnailTextures.find(source.relativePathKey);
                    found != m_RawWorkspaceThumbnailTextures.end())
                found->second.lastVisibleFrame = ImGui::GetFrameCount();
            return result;
        },m_GalleryNavigationMotion.get());
    const auto originFor=[&](const auto& image) {
        auto origin=CaptureGalleryTransitionOrigin(image.path,image.key,image.bounds);
        origin.uvMinimum=image.uvMinimum;origin.uvMaximum=image.uvMaximum;
        return origin;
    };
    if (action.openImage && m_RawGalleryOpenSelectionHandler) {
        RawGalleryOpenSelection selection;
        selection.items.push_back({action.openImage->path, false, action.openImage->sourceFolder,originFor(*action.openImage)});
        m_RawGalleryOpenSelectionHandler(std::move(selection), false);
    } else if (action.openFolder) {
        std::vector<Stack::Renderer::GalleryTransitionOrigin> pages;
        for(const auto& page:action.pages)pages.push_back(originFor(page));
        const ImVec2 position=ImGui::GetWindowPos();
        SetRawWorkspaceFolder(*action.openFolder);
        if(m_GalleryNavigationMotion) {
            m_GalleryNavigationMotion->FolderPages(std::move(pages));
            m_GalleryNavigationMotion->Folders(false,m_GalleryBrowseAnchor,
                {position,{position.x+size.x,position.y+size.y}},action.openFolder->lexically_normal().generic_u8string());
        }
    }
    else if (action.addFolder) OpenRawWorkspaceFolderDialog();
}
