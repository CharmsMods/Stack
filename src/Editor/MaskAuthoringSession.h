#pragma once
#include "Raw/MaskAuthoring.h"
#include "Project/NodeEditTarget.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include "Renderer/ScopedGLObjects.h"
#include <atomic>

namespace Stack::Editor {
struct MaskPreviewWork {
    std::atomic<bool> canceled{false},ready{false};
    std::shared_ptr<const Assets::FloatData> coverage;
    std::string error;
};
struct MaskAuthoringSession {
    enum class Phase { Idle, Destination, Drawing, Applying };
    Phase phase=Phase::Idle;
    MaskAuthoring::Recipe recipe;
    MaskAuthoring::Tool tool=MaskAuthoring::Tool::Values;
    MaskAuthoring::Role role=MaskAuthoring::Role::Protect;
    Project::NodeEditTarget target;
    std::string layerId,maskUuid,document,status;
    int frame=0,previousTab=0,selected=-1,dragHandle=-1,width=0,height=0,sourceNode=0,acceptAfter=0;
    std::uint64_t generation=0,load=0,edit=0,rawEdit=0;
    bool edited=false,sourceReady=false;
    bool dragging=false,gray=false,pendingSource=false,previewDirty=false;
    float radius=.035f,feather=.4f;
    MaskAuthoring::Point dragStart{};
    Raw::RawWorkingSpace workingSpace=Raw::RawWorkingSpace::LinearSrgbD65;
    RawLabInternal::RawLabImageMapping mapping;
    std::shared_ptr<std::atomic<bool>> cancel;
    std::shared_ptr<const RawAssetCapture::PreviewField> field;
    std::shared_ptr<MaskPreviewWork> work;
    Renderer::ScopedGLTexture texture;
    std::shared_ptr<const Assets::FloatData> preview;
    bool textureDirty=false;
    void Dirty() { edited=true;previewDirty=true;if(work)work->canceled.store(true); }
};
}

