#include "PhotoPairs.h"
#include "Renderer/GLLoader.h"
#include <imgui.h>
#include <unordered_map>
namespace Stack::Hdr {
void SetPhotoHeadroomUniform(int location) {
    if(location>=0) glUniform1f(location,CurrentTargetHeadroom());
}
namespace {
struct Pair {unsigned base; int frame; ImGuiContext* context;};
thread_local std::unordered_map<unsigned,Pair> pairs;
}
unsigned RegisterPhotoPair(unsigned sdr,unsigned hdr) {
    if(!sdr || !hdr || !ImGui::GetCurrentContext()) return hdr;
    const int frame=ImGui::GetFrameCount();
    auto found=pairs.find(hdr);
    if(found!=pairs.end()) {found->second={sdr,frame,ImGui::GetCurrentContext()};return hdr;}
    // Pruning happens only when a new published texture arrives, never on an
    // unchanged draw. Stale names cannot be consumed in another UI frame.
    if(pairs.size()>=4096) {
        for(auto it=pairs.begin();it!=pairs.end();) {
            if(it->second.frame<frame-1) it=pairs.erase(it); else ++it;
        }
    }
    if(pairs.size()<4096) pairs.emplace(hdr,Pair{sdr,frame,ImGui::GetCurrentContext()});
    return hdr;
}
unsigned PhotoBaseForFrame(unsigned hdr) {
    if(!ImGui::GetCurrentContext()) return 0;
    const auto found=pairs.find(hdr);
    return found!=pairs.end() && found->second.context==ImGui::GetCurrentContext() &&
        found->second.frame==ImGui::GetFrameCount() ? found->second.base : 0;
}
}
