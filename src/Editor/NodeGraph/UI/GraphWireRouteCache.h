#pragma once
#include "Editor/NodeGraph/GraphWireRouting.h"
#include "ContinuousLinkStroke.h"
#include <memory>
#include <unordered_map>

namespace Stack::Editor::NodeGraphUIVisuals {
struct ProjectedWire {
    GraphRouting::Path geometry;
    std::vector<CurveSample> samples;
    ImVec2 source{},target{},origin{},via{},exit{};
    bool hasVia=false;
    float zoom=0;
    std::uint64_t routeRevision=0;
    bool straight=false;
};
// One queued latest request and one serial pool job per canvas. The job owns
// only geometry; closing/replacing the canvas cancels publication and work.
class WireRouteCache {
public:
    WireRouteCache();
    ~WireRouteCache();
    void Request(GraphRouting::Scene scene);
    bool Poll();
    const ProjectedWire& Project(const std::string& key,ImVec2 source,ImVec2 target,ImVec2 origin,float zoom,bool straight,const ImVec2* via=nullptr,const ImVec2* exit=nullptr);
    bool Pending() const;
    const std::string& Error() const { return m_Result.error; }
private:
    struct Work;
    std::shared_ptr<Work> m_Work;
    GraphRouting::Result m_Result;
    std::unordered_map<std::string,ProjectedWire> m_Projected;
};
}
