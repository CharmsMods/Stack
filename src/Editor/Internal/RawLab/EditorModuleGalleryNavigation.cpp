#include "Editor/EditorModule.h"
#include <algorithm>
#include "Hdr/Presentation.h"

void EditorModule::PresentGalleryOpeningImage(ImVec2& minimum,ImVec2& maximum,unsigned int texture) {
    if(!m_GalleryNavigationMotion || !m_GalleryNavigationMotion->Owns(m_GalleryNavigationWorkspace))return;
    if(!texture && m_GalleryNavigationMotion->ImageMoving(m_GalleryNavigationWorkspace)) {
        int width=0,height=0;
        if(!TryGetViewportPreviewOverview(texture,width,height)) {
            Stack::Renderer::ViewportPreviewGeometry geometry;
            if(!TryGetViewportPreviewGeometry(geometry) || !geometry.resident.Partial())
                TryGetActiveRawWorkspacePresentationTexture(texture,width,height);
        }
    }
    const auto rect=m_GalleryNavigationMotion->PresentImage(m_GalleryNavigationWorkspace,{minimum,maximum},texture);
    minimum=rect.Min;maximum=rect.Max;
}

Stack::Renderer::GalleryTransitionOrigin EditorModule::CaptureGalleryTransitionOrigin(
    const std::filesystem::path& subject,const std::string& explicitKey,const ImRect& explicitBounds) {
    Stack::Renderer::GalleryTransitionOrigin origin;
    origin.subject=subject.lexically_normal().generic_u8string();
    std::string key=explicitKey;
    if(key.empty()) {
        // Resolve only on an actual navigation request, never per rendered tile.
        if(const auto source=std::find_if(m_RawWorkspace.sources.begin(),m_RawWorkspace.sources.end(),
            [&](const auto& item){return item.absolutePath.lexically_normal()==subject.lexically_normal();});
            source!=m_RawWorkspace.sources.end())key=source->relativePathKey;
        else {
            const auto& projects=GetRawWorkspaceGalleryPresentation().projects;
            const auto project=std::find_if(projects.begin(),projects.end(),
                [&](const auto& item){return item.projectPath==subject;});
            if(project!=projects.end()) {
                key=project->projectCoverKey;
            }
        }
        if(key.empty() && ImGui::GetCurrentContext())key=m_RawWorkspaceGalleryLayoutAnimation.ImageAt(ImGui::GetIO().MousePos);
    }
    origin.thumbnailKey=key;
    ImRect bounds=explicitBounds;
    if(bounds.GetArea()<=0)m_RawWorkspaceGalleryLayoutAnimation.PresentedBounds(key,bounds);
    origin.minimum=bounds.Min;origin.maximum=bounds.Max;
    const auto found=m_RawWorkspaceThumbnailTextures.find(key);
    if(found==m_RawWorkspaceThumbnailTextures.end())return origin;
    const auto& cached=found->second;
    origin.sourceIdentity=cached.sourceIdentity;
    const bool hdr=cached.hdrTexture && Stack::Hdr::PreviewActiveForCurrentWindow();
    const auto texture=hdr ? cached.hdrTexture : cached.texture;
    const int width=hdr ? cached.hdrWidth : cached.width;
    const int height=hdr ? cached.hdrHeight : cached.height;
    if(!texture || !width || !height)return origin;
    auto lease=m_GalleryThumbnailLeases[texture].lock();
    if(!lease) {
        auto allocated=std::make_shared<Stack::Renderer::GalleryThumbnailLease>();
        allocated->texture=texture;allocated->width=width;allocated->height=height;
        const std::size_t base=std::size_t(width)*height*(hdr ? 8 : 4);
        allocated->bytes=base+base/3;
        lease=std::move(allocated);m_GalleryThumbnailLeases[texture]=lease;
    }
    origin.thumbnail=std::move(lease);
    return origin;
}

bool EditorModule::GalleryLoadFailed() const {
    return m_Project->files->load.state==Async::TaskState::Failed ||
        m_RawWorkspaceProjectLoadTaskState==Async::TaskState::Failed ||
        m_SourceLoadTaskState==Async::TaskState::Failed ||
        m_DeferredLoadedProjectApply.failed;
}
