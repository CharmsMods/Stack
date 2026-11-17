#include "Editor/EditorRenderWorker.h"
#include "Renderer/ViewportSectionStore.h"
#include "Renderer/ViewportTextureCopy.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/GLStateGuards.h"
#include <algorithm>

namespace {
bool ReadNative(unsigned texture,int width,int height,std::vector<float>& pixels) {
    Stack::Renderer::GLState::TextureBinding binding(GL_TEXTURE_2D,GL_TEXTURE_BINDING_2D);
    GLint pack=0,row=0,skipRows=0,skipPixels=0,alignment=0;
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PACK_ROW_LENGTH,&row);
    glGetIntegerv(GL_PACK_SKIP_ROWS,&skipRows);glGetIntegerv(GL_PACK_SKIP_PIXELS,&skipPixels);
    glGetIntegerv(GL_PACK_ALIGNMENT,&alignment);
    pixels.resize(std::size_t(width)*height*4);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glPixelStorei(GL_PACK_ROW_LENGTH,0);
    glPixelStorei(GL_PACK_SKIP_ROWS,0);glPixelStorei(GL_PACK_SKIP_PIXELS,0);glPixelStorei(GL_PACK_ALIGNMENT,1);
    glBindTexture(GL_TEXTURE_2D,texture);
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_FLOAT,pixels.data());
    const bool ok=glGetError()==GL_NO_ERROR;
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glPixelStorei(GL_PACK_ROW_LENGTH,row);
    glPixelStorei(GL_PACK_SKIP_ROWS,skipRows);glPixelStorei(GL_PACK_SKIP_PIXELS,skipPixels);glPixelStorei(GL_PACK_ALIGNMENT,alignment);
    return ok;
}
}
bool EditorRenderWorker::RestoreViewportSection(const Snapshot& snapshot,Result& result) {
    if(!snapshot.viewportSections || snapshot.viewportSection<0 || !snapshot.viewportSections->Has(snapshot.viewportSection))return false;
    std::vector<float> pixels,hdr;
    if(!snapshot.viewportSections->Load(snapshot.viewportSection,pixels,hdr,result.error)) {result.success=false;return true;}
    const auto region=snapshot.viewportSections->Raster(snapshot.viewportSection);
    auto& output=result.outputTexture;output.width=region.width;output.height=region.height;
    output.texture=GLHelpers::CreateTextureFromData(pixels.data(),region.width,region.height,GL_RGBA32F,GL_RGBA,GL_FLOAT,GL_LINEAR);
    if(!hdr.empty()) {
        const auto texture=GLHelpers::CreateTextureFromData(hdr.data(),region.width,region.height,GL_RGBA32F,GL_RGBA,GL_FLOAT,GL_LINEAR);
        output.hdr=Raw::RawGpuImageLease::AdoptOwned(texture,region.width,region.height,Raw::RawGpuImageFamily::Presentation,16);
        if(!output.hdr) result.error="Cannot reload HDR native section.";
    }
    result.success=output.EnsureLease() && result.error.empty();
    if(!result.success && result.error.empty())result.error="Cannot reload native section.";
    result.viewportSectionRestored=true;
    result.viewportPreviewGeometry={region.fullWidth,region.fullHeight,region,region.width,region.height};
    result.rawWorkspace.viewportRegion=region;
    result.rawWorkspace.expectedNativeOutputWidth=region.fullWidth;result.rawWorkspace.expectedNativeOutputHeight=region.fullHeight;
    output.readyFence=glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);glFlush();
    return true;
}
void EditorRenderWorker::PreserveViewportSection(Result& result) {
    try {
    if(!result.viewportSections || result.viewportSection<0 || !result.success ||
        !result.error.empty() || result.denoisePending || result.telemetry.superseded)return;
    const auto region=result.viewportSections->Raster(result.viewportSection);
    if(!result.viewportPreviewGeometry.NativeSamples() || result.viewportPreviewGeometry.resident!=region) {
        result.success=false;result.error="Native section returned incompatible coverage.";return;
    }
    auto& output=result.outputTexture;
    if(!result.viewportSectionRestored) {
        std::vector<float> pixels,hdr;
        if(!ReadNative(output.texture,region.width,region.height,pixels) ||
            (output.hdr && !ReadNative(output.hdr.Texture(),region.width,region.height,hdr))) {
            result.success=false;result.error="Cannot preserve native section pixels.";return;
        }
        if(!result.viewportSections->Store(result.viewportSection,std::move(pixels),std::move(hdr),result.error)) {
            result.success=false;return;
        }
    }
    // Display copies are disposable; the store always keeps the native floats.
    auto& preview=result.viewportSectionPreview;
    preview.width=(region.width+3)/4;preview.height=(region.height+3)/4;
    preview.texture=Stack::Renderer::CopyViewportTexture(output.texture,region.width,region.height,preview.width,preview.height);
    if(output.hdr) {
        const auto texture=Stack::Renderer::CopyViewportTexture(output.hdr.Texture(),region.width,region.height,preview.width,preview.height);
        preview.hdr=Raw::RawGpuImageLease::AdoptOwned(texture,preview.width,preview.height,Raw::RawGpuImageFamily::Presentation,16);
    }
    if(!preview.EnsureLease() || (output.hdr && !preview.hdr)) {result.success=false;result.error="Cannot prepare native section display copy.";}
    } catch (const std::exception& error) {
        result.success=false;result.error=std::string("Native section cache failed: ")+error.what();
    } catch (...) {
        result.success=false;result.error="Native section cache failed.";
    }
}
