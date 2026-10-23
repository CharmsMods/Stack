#pragma once

#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <algorithm>
#include <cmath>
#include <queue>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

// Shared finite separation rules for explicit layout operations.
namespace Stack::GraphLayout::Placement {
using namespace EditorNodeGraph;
constexpr float kGap = 36.f, kColumnGap = 100.f, kPadding = 28.f, kHeader = 44.f;
constexpr float kLimit = 20000.f, kCell = 512.f;
constexpr std::size_t kWorkLimit = 2000000;

inline GraphRect Offset(GraphRect r, Vec2 d) { return {{r.min.x+d.x,r.min.y+d.y},{r.max.x+d.x,r.max.y+d.y}}; }
inline bool Valid(GraphRect r) { return std::isfinite(r.min.x) && std::isfinite(r.min.y) &&
    std::isfinite(r.max.x) && std::isfinite(r.max.y) && r.max.x>r.min.x && r.max.y>r.min.y; }
inline bool Overlap(GraphRect a, GraphRect b, float gap = 0) { return a.min.x<b.max.x+gap && a.max.x+gap>b.min.x &&
    a.min.y<b.max.y+gap && a.max.y+gap>b.min.y; }
inline bool Contains(GraphRect a, Vec2 p) { return p.x>=a.min.x && p.x<=a.max.x && p.y>=a.min.y && p.y<=a.max.y; }
inline GraphRect Union(GraphRect a, GraphRect b) { return {{std::min(a.min.x,b.min.x),std::min(a.min.y,b.min.y)},
    {std::max(a.max.x,b.max.x),std::max(a.max.y,b.max.y)}}; }
inline GraphRect GroupRect(const NodeGroup& g) { return {g.position,{g.position.x+g.size.x,g.position.y+g.size.y}}; }
inline GraphRect Frame(GraphRect r) { return {{r.min.x-kPadding,r.min.y-kHeader},{r.max.x+kPadding,r.max.y+kPadding}}; }
inline bool InRange(GraphRect r) { return Valid(r) && r.min.x>=-kLimit && r.min.y>=-kLimit && r.max.x<=kLimit && r.max.y<=kLimit; }

struct Item { GraphRect rect; std::vector<int> nodes,groups; bool changed=false; int flow=0; };

// Short-lived spatial index, built only for this edit. Queries visit nearby
// rectangles; a shared work bound also covers dense/pathological group geometry.
class Reservations {
public:
    std::vector<Item> items;
    std::size_t work=0;
    std::size_t limit=kWorkLimit;
    GraphRect rootBounds;
    void Spend() { if (++work>limit) throw std::runtime_error("Graph layout exceeded its bounded placement budget."); }
    void Add(Item item) {
        const int id=static_cast<int>(items.size()); items.push_back(std::move(item));m_Visited.push_back(0);
        Cells(items.back().rect,[&](auto key){Spend();m_Cells[key].push_back(id);});
    }
    int Conflict(GraphRect rect, bool moved) {
        int found=-1;
        if(++m_Query==0){std::fill(m_Visited.begin(),m_Visited.end(),0);m_Query=1;}
        Cells(Offset({{rect.min.x-kGap,rect.min.y-kGap},{rect.max.x+kGap,rect.max.y+kGap}},{}),[&](auto key) {
            Spend();const auto bucket=m_Cells.find(key);if(bucket==m_Cells.end())return;
            for(int id:bucket->second) { Spend();if(m_Visited[id]==m_Query)continue;m_Visited[id]=m_Query;
                if((moved || items[id].changed) && Overlap(rect,items[id].rect,kGap) && (found<0 || id<found))found=id;
            }
        });return found;
    }
private:
    std::unordered_map<std::uint64_t,std::vector<int>> m_Cells;
    std::vector<std::uint64_t> m_Visited;
    std::uint64_t m_Query=0;
    template<class F> void Cells(GraphRect r,F&& f) {
        for(int x=static_cast<int>(std::floor(r.min.x/kCell));x<=static_cast<int>(std::floor(r.max.x/kCell));++x)
            for(int y=static_cast<int>(std::floor(r.min.y/kCell));y<=static_cast<int>(std::floor(r.max.y/kCell));++y)
                f((std::uint64_t(std::uint32_t(x))<<32)|std::uint32_t(y));
    }
};

inline Vec2 Place(const Item& item, Reservations& occupied) {
    struct Candidate { Vec2 delta; float cost; std::size_t order; };
    const auto compare=[](const Candidate& a,const Candidate& b){return a.cost==b.cost?a.order>b.order:a.cost>b.cost;};
    std::priority_queue<Candidate,std::vector<Candidate>,decltype(compare)> pending(compare);
    std::size_t order=0;pending.push({{},0,order++});
    std::unordered_set<std::uint64_t> seen;
    for(int attempt=0;!pending.empty() && attempt<512;++attempt) {
        occupied.Spend();const auto candidate=pending.top();pending.pop();
        const auto rect=Offset(item.rect,candidate.delta);
        if(!InRange(rect))continue;
        // Quantization deduplicates equivalent separation candidates only; it
        // never rounds authored coordinates or consumes sub-pixel edits.
        const auto key=(std::uint64_t(std::uint32_t(std::lround(candidate.delta.x*8)))<<32)|
            std::uint32_t(std::lround(candidate.delta.y*8));
        if(!seen.insert(key).second)continue;
        const int conflict=occupied.Conflict(rect,item.changed || candidate.cost>0);
        if(conflict<0)return candidate.delta;
        const auto b=occupied.items[conflict].rect;
        const Vec2 options[]={{b.min.x-kGap-item.rect.max.x,candidate.delta.y},
            {b.max.x+kGap-item.rect.min.x,candidate.delta.y},
            {candidate.delta.x,b.min.y-kGap-item.rect.max.y},
            {candidate.delta.x,b.max.y+kGap-item.rect.min.y}};
        for(int side=0;side<4;++side) {
            // Prefer the corresponding flow side without turning that preference
            // into an impossible constraint near a coordinate boundary.
            const auto d=options[side];const auto proposed=Offset(item.rect,d);
            const auto root=Valid(occupied.rootBounds)?occupied.rootBounds:occupied.items.front().rect;
            const bool flowSide=item.flow<0?proposed.max.x+kGap<=root.min.x:proposed.min.x>=root.max.x+kGap;
            const float span=root.max.x-root.min.x+item.rect.max.x-item.rect.min.x;
            const float penalty=item.flow && !flowSide?span*span*4:0;
            pending.push({d,d.x*d.x+d.y*d.y+penalty,order++});
        }
    }
    throw std::runtime_error("This move needs more graph space than the current placement limits allow.");
}
} // namespace Stack::GraphLayout::Placement
