#pragma once
#include "Async/TaskSystem.h"
#include "Raw/RawZoneArea.h"
#include <memory>

namespace Stack::Editor {
// CPU preview work uses the existing interactive task queue. Requests coalesce;
// mouse handling never waits for a mask or for the image renderer.
class RawZoneAreaPreview {
public:
    RawZoneAreaPreview();
    ~RawZoneAreaPreview();
    RawZoneAreaPreview(const RawZoneAreaPreview&)=delete;
    RawZoneAreaPreview& operator=(const RawZoneAreaPreview&)=delete;
    struct Result {
        std::shared_ptr<const RawRecipe::RawZoneAreaMaskPreview> mask;
        std::size_t referenceKey=0;
        bool provisional=false;
    };
    void Request(const RawRecipe::RawZoneArea& area,const RawRecipe::RawCropRotationRecipe& transform,
        std::shared_ptr<const RawRecipe::ImageGuide> guide,std::size_t referenceKey,
        Async::ActivityMetadata activity = {});
    Result Latest() const;
    bool Pending() const;
    void Cancel();
private:
    struct State;
    std::shared_ptr<State> m_State;
};
}
