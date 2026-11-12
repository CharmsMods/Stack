#pragma once
#include <imgui_internal.h>
namespace Stack::Editor::RawLabInternal {
// Short-lived presentation geometry only. No renderer readback or image decoding.
class GalleryBrowserMotion {
public:
    ~GalleryBrowserMotion();
    void Capture(ImDrawList* draw,ImRect surface);
    void Start(bool outward,ImRect tile,unsigned cover=0,int width=0,int height=0);
    void Stop();
    void Anchor(ImRect tile){tile_=tile;}
    bool Active() const{return active_;}
    bool Outward() const{return outward_;}
    void Begin(ImRect surface,bool ready,bool reduced);
    void Finish(ImDrawList* draw,int firstVertex);
private:
    ImDrawList* captured_=nullptr;
    ImRect source_{},surface_{},tile_{};
    double started_=0;
    float progress_=0;
    bool active_=false,outward_=false,waiting_=false;
    unsigned cover_=0;
    int coverWidth_=0,coverHeight_=0;
};
}
