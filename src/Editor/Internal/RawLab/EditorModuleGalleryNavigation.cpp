#include "Editor/EditorModule.h"
#include <algorithm>

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
            if(project!=projects.end())key=project->referenceSourceKey;
        }
        if(key.empty() && ImGui::GetCurrentContext())key=m_RawWorkspaceGalleryLayoutAnimation.ImageAt(ImGui::GetIO().MousePos);
    }
    origin.thumbnailKey=key;
    ImRect bounds=explicitBounds;
    if(bounds.GetArea()<=0 && !m_RawWorkspaceGalleryLayoutAnimation.PresentedBounds(key,bounds))return origin;
    origin.minimum=bounds.Min;origin.maximum=bounds.Max;
    const auto found=m_RawWorkspaceThumbnailTextures.find(key);
    if(found==m_RawWorkspaceThumbnailTextures.end())return origin;
    const auto& cached=found->second;
    origin.sourceIdentity=cached.sourceIdentity;
    if(!cached.texture || !cached.width || !cached.height)return origin;
    auto lease=m_GalleryThumbnailLeases[cached.texture].lock();
    if(!lease) {
        auto allocated=std::make_shared<Stack::Renderer::GalleryThumbnailLease>();
        allocated->texture=cached.texture;allocated->width=cached.width;allocated->height=cached.height;
        const std::size_t base=std::size_t(cached.width)*std::size_t(cached.height)*4;
        allocated->bytes=base+base/3;
        lease=std::move(allocated);m_GalleryThumbnailLeases[cached.texture]=lease;
    }
    origin.thumbnail=std::move(lease);
    return origin;
}
