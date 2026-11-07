#include "Editor/EditorModule.h"
#include "Hdr/Presentation.h"
#include "Persistence/ProjectIndex.h"
#include "Raw/RawGalleryInputPolicy.h"
#include "Raw/RawLoader.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"

namespace {
constexpr double SettleSeconds=.18;
constexpr int DetailPreviewEdge=704; // A square RGBA float upload fits the existing 8 MiB frame budget.
}

unsigned int EditorModule::GalleryThumbnailForDisplay(const std::string& cacheKey,
    RawWorkspaceThumbnailTexture& entry,const Stack::RawWorkspace::SourceRecord& source,
    const std::string* presetCardKey,int* width,int* height) {
    if(width) *width=entry.width;
    if(height) *height=entry.height;
    // Gallery tiles retain their SDR rendition. Refining each tile from RAW
    // changes its appearance after loading and spends rendering work on the
    // browsing strip/grid. Only explicitly requested large detail previews
    // may use the HDR refinement path.
    if (entry.detailEdge == 0 || presetCardKey || cacheKey.rfind("project-overlay:", 0) == 0)
        return entry.texture;
    if(!entry.texture || !ImGui::GetCurrentContext() || !Stack::Hdr::PreviewActiveForCurrentWindow()) return entry.texture;
    const Stack::RawWorkspace::SourceSetProjectCatalogEntry* card=nullptr;
    if(presetCardKey || cacheKey.compare(0,16,"project-overlay:")==0) {
        auto i=m_GalleryPresetProjects.find(presetCardKey ? *presetCardKey : cacheKey);
        if(i==m_GalleryPresetProjects.end() || i->second>=m_RawWorkspace.sourceSetProjects.size()) return entry.texture;
        card=&m_RawWorkspace.sourceSetProjects[i->second];
        auto revision=card->masterSource ? static_cast<std::uint64_t>(card->masterSource->modifiedTimeTicks) :
            card->persistedStorageRevision;
        if(entry.hdrProjectRevision!=revision) {
            if(m_GalleryHdrJob && m_GalleryHdrJob->key==cacheKey) m_GalleryHdrJob->canceled.store(true);
            ReleaseGalleryHdrThumbnail(entry);
            entry.hdrProjectRevision=revision;
        }
    }
    if(entry.hdrTexture) {
        if(width) *width=entry.hdrWidth;
        if(height) *height=entry.hdrHeight;
        return entry.hdrTexture;
    }
    if(entry.hdrAttempted || !m_RawRenderClientId) return entry.texture;
    const int frame=ImGui::GetFrameCount();
    const double now=ImGui::GetTime();
    if(entry.hdrDemandFrame<frame-1 || entry.hdrDemandFrame<0) entry.hdrVisibleSince=now;
    entry.hdrDemandFrame=frame;
    if(m_GalleryHdrJob) {
        if(m_GalleryHdrJob->key==cacheKey && m_GalleryHdrJob->sourceGeneration==entry.requestGeneration)
            m_GalleryHdrJob->lastDemandFrame=frame;
        return entry.texture;
    }
    if(now-entry.hdrVisibleSince<SettleSeconds || ImGui::IsAnyItemActive() || ImGui::GetIO().MouseWheel!=0 ||
        (m_PermanentGalleryWorkspace && m_RawWorkspaceGalleryLayoutAnimation.Active())) return entry.texture;

    Stack::GalleryHdr::Request request;
    request.source=source.absolutePath;
    request.edge=entry.detailEdge ? std::min(entry.detailEdge,DetailPreviewEdge) : Stack::RawWorkspace::kNeutralThumbnailMaxDimension;
    if(card) {
        if(card->masterSource) request.source=card->masterSource->absolutePath;
        else {
            request.project=card->absolutePath;
            request.projectRevision=entry.hdrProjectRevision;
            request.savedBracket=card->hasSavedBracketingResult;
            request.preset=presetCardKey && !request.savedBracket;
            if(request.preset) request.comparison=m_GalleryComparisonImage;
            else if(card->linkedOriginal) request.source=card->linkedOriginal->absolutePath;
            else if(request.source.empty() && !card->referenceSourceKey.empty()) {
                if(auto* reference=FindRawWorkspaceSourceByKey(card->referenceSourceKey)) request.source=reference->absolutePath;
            }
            // A multi-frame cover belongs to the saved merge. A single capture
            // from the set is not a faithful rendering of that project.
            if(!request.preset && card->multiFrameProject && !request.savedBracket) {
                entry.hdrAttempted=true;
                return entry.texture;
            }
        }
    }
    if(request.source.empty() && request.project.empty()) {
        entry.hdrAttempted=true;
        return entry.texture;
    }
    auto job=std::make_shared<Stack::GalleryHdr::Job>();
    job->key=cacheKey;
    job->sourceGeneration=entry.requestGeneration;
    job->lastDemandFrame=frame;
    if(ProjectTasks().Submit("Refining HDR thumbnail",[job,request=std::move(request)] {
        try { Stack::GalleryHdr::Render(request,*job); }
        catch(const std::exception&) { job->pixels.clear(); }
        job->complete.store(true,std::memory_order_release);
    })) m_GalleryHdrJob=std::move(job);
    else entry.hdrAttempted=true;
    return entry.texture;
}

void EditorModule::CancelObsoleteGalleryHdrJob() {
    if(!m_GalleryHdrJob) return;
    auto& job=*m_GalleryHdrJob;
    if(job.lastDemandFrame+1<ImGui::GetFrameCount()) job.canceled.store(true);
    if(job.canceled.load() && job.complete.load(std::memory_order_acquire)) m_GalleryHdrJob.reset();
}

void EditorModule::UploadGalleryHdrThumbnail() {
    if(!m_GalleryHdrJob || !m_GalleryHdrJob->complete.load(std::memory_order_acquire)) return;
    auto job=std::move(m_GalleryHdrJob);
    const auto i=m_RawWorkspaceThumbnailTextures.find(job->key);
    if(job->canceled.load() || i==m_RawWorkspaceThumbnailTextures.end() ||
        i->second.requestGeneration!=job->sourceGeneration) return;
    auto& entry=i->second;
    entry.hdrAttempted=true;
    if(job->pixels.empty() || job->width<=0 || job->height<=0) return;
    entry.hdrTexture=GLHelpers::CreateTextureFromData(job->pixels.data(),job->width,job->height,
        GL_RGBA16F,GL_RGBA,GL_FLOAT,GL_LINEAR);
    if(entry.hdrTexture) {
        const Stack::Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
        glBindTexture(GL_TEXTURE_2D,entry.hdrTexture);
        glGenerateMipmap(GL_TEXTURE_2D);
        if(glGetError()==GL_NO_ERROR)
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_LINEAR);
        binding.Restore();
        entry.hdrWidth=job->width;
        entry.hdrHeight=job->height;
        m_RawWorkspaceThumbnailTextureTrimNeeded=true;
    }
    ++m_RawWorkspaceThumbnailTextureUploadsThisFrame;
    m_RawWorkspaceThumbnailTextureUploadBytesThisFrame+=job->pixels.size()*sizeof(float);
}
void EditorModule::ReleaseGalleryHdrThumbnail(RawWorkspaceThumbnailTexture& entry) {
    QueueRawWorkspaceThumbnailTextureDelete(entry.hdrTexture);
    entry.hdrTexture=0;
    entry.hdrWidth=entry.hdrHeight=0;
    entry.hdrAttempted=false;
    entry.hdrDemandFrame=-1;
}
