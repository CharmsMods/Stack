#include "Editor/RawZoneAreaPreview.h"
#include "Raw/RawZoneAreaRasterizer.h"
#include "Async/TaskSystem.h"
#include <atomic>
#include <mutex>
#include <optional>

namespace Stack::Editor {
namespace {
bool Extends(const RawRecipe::RawZoneArea& before,const RawRecipe::RawZoneArea& after) {
    if(before.id!=after.id || before.sourceAspect!=after.sourceAspect || before.strokes.size()>after.strokes.size()) return false;
    for(std::size_t i=0;i<before.strokes.size();++i) {
        const auto& a=before.strokes[i];const auto& b=after.strokes[i];
        if(i+1<before.strokes.size() || before.strokes.size()<after.strokes.size()) {
            if(RawRecipe::ZoneStrokeFingerprint(a)!=RawRecipe::ZoneStrokeFingerprint(b)) return false;
        } else {
            if(a.erase!=b.erase || a.radius!=b.radius || a.opacity!=b.opacity || a.softness!=b.softness ||
                a.followEdges!=b.followEdges || a.edgeSensitivity!=b.edgeSensitivity || a.path.size()>b.path.size()) return false;
            for(std::size_t p=0;p<a.path.size();++p) if(a.path[p].u!=b.path[p].u || a.path[p].v!=b.path[p].v) return false;
        }
    }
    return true;
}
}
struct RawZoneAreaPreview::State {
    struct Request {
        RawRecipe::RawZoneArea area;
        RawRecipe::RawCropRotationRecipe transform;
        std::shared_ptr<const RawRecipe::ImageGuide> guide;
        std::size_t referenceKey=0, fingerprint=0, epoch=0;
    };
    mutable std::mutex mutex;
    std::optional<Request> pending, wanted;
    Result result;
    std::atomic<std::size_t> epoch{1};
    bool running=false;
    RawRecipe::ZoneAreaRasterizer rasterizer;
    static void Run(const std::shared_ptr<State>& state) {
        for(;;) {
            Request request;
            {
                std::lock_guard lock(state->mutex);
                if(!state->pending) {state->running=false;return;}
                request=std::move(*state->pending);state->pending.reset();
            }
            const auto cancelled=[&]{return state->epoch.load()!=request.epoch;};
            Result result;result.referenceKey=request.referenceKey;
            try {
                const bool guided=RawRecipe::ZoneAreaUsesGuidance(request.area);
                result.provisional=guided && !request.guide;
                const bool rotated=request.transform.rotationDegrees==90 || request.transform.rotationDegrees==270;
                const float aspect=rotated ? 1/request.area.sourceAspect : request.area.sourceAspect;
                const int w=request.guide ? request.guide->width : std::max(1,int(aspect>=1 ? 512 : 512*aspect));
                const int h=request.guide ? request.guide->height : std::max(1,int(aspect>=1 ? 512/aspect : 512));
                auto area=request.area;
                if(result.provisional) for(auto& stroke:area.strokes) stroke.followEdges=false;
                const auto* pixels=state->rasterizer.Evaluate(area,w,h,request.transform,request.guide.get(),cancelled);
                if(pixels && !cancelled()) result.mask=RawRecipe::MakeZoneAreaMaskPreview(*pixels,w,h,request.fingerprint,768,&request.area);
            } catch(...) {
                // Preserve the last usable overlay. A subsequent edit can retry.
                state->rasterizer.Clear();
            }
            {
                std::lock_guard lock(state->mutex);
                if(result.mask && !cancelled()) state->result=std::move(result);
                else if (!cancelled() && state->wanted && state->wanted->fingerprint==request.fingerprint) state->wanted.reset();
            }
            // Wake an idle desktop frame to consume completed coverage.
            Async::TaskSystem::Get().PostToMain([]{});
        }
    }
};
RawZoneAreaPreview::RawZoneAreaPreview():m_State(std::make_shared<State>()) {}
RawZoneAreaPreview::~RawZoneAreaPreview() {Cancel();}
void RawZoneAreaPreview::Cancel() {
    std::lock_guard lock(m_State->mutex);
    ++m_State->epoch;m_State->pending.reset();m_State->wanted.reset();m_State->result={};
}
void RawZoneAreaPreview::Request(const RawRecipe::RawZoneArea& area,const RawRecipe::RawCropRotationRecipe& transform,
    std::shared_ptr<const RawRecipe::ImageGuide> guide,std::size_t referenceKey,Async::ActivityMetadata activity) {
    const auto fingerprint=RawRecipe::ZoneAreaMaskFingerprint(area);
    bool launch=false;
    {
        std::lock_guard lock(m_State->mutex);
        const auto& previous=m_State->wanted;
        if(previous && previous->fingerprint==fingerprint && previous->referenceKey==referenceKey &&
            previous->area.id==area.id && previous->guide==guide) return;
        if(!previous || previous->referenceKey!=referenceKey || previous->guide!=guide || !Extends(previous->area,area)) {
            ++m_State->epoch;m_State->result={};
        }
        State::Request next{area,transform,std::move(guide),referenceKey,fingerprint,m_State->epoch.load()};
        m_State->wanted=next;m_State->pending=std::move(next);
        if(!m_State->running) {m_State->running=true;launch=true;}
    }
    activity.label="Updating painted area";
    activity.maintenance=true;
    if(launch && !Async::TaskSystem::Get().SubmitHighPriority(std::move(activity),[state=m_State]{State::Run(state);})) {
        std::lock_guard lock(m_State->mutex);m_State->running=false;m_State->wanted.reset();
    }
}
RawZoneAreaPreview::Result RawZoneAreaPreview::Latest() const {
    std::lock_guard lock(m_State->mutex);return m_State->result;
}
bool RawZoneAreaPreview::Pending() const {
    std::lock_guard lock(m_State->mutex);return m_State->running || m_State->pending.has_value();
}
}
