#include "ProximityJoins.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace Stack::Renderer::Glass {
using stack::glass::Vec2;
bool JoinShape::SameGeometry(const JoinShape& b) const {
    return identity==b.identity && center.x==b.center.x && center.y==b.center.y &&
        halfSize.x==b.halfSize.x && halfSize.y==b.halfSize.y && strain.x==b.strain.x &&
        strain.y==b.strain.y && radius==b.radius && joinWidth==b.joinWidth && clip==b.clip;
}
namespace {
struct Support {
    float xx,xy,yy;
    explicit Support(Vec2 strain) {
        const float r=std::hypot(strain.x,strain.y),e=std::exp(r);
        const float c=.5f*(e+1.f/e),sh=r<1.e-4f?1.f+r*r/6.f:.5f*(e-1.f/e)/r;
        xx=c+sh*strain.x;xy=sh*strain.y;yy=c-sh*strain.x;
    }
    float Along(const JoinShape& s,Vec2 n) const {
        const Vec2 v{xx*n.x+xy*n.y,xy*n.x+yy*n.y};
        return std::abs(v.x)*(s.halfSize.x-s.radius)+std::abs(v.y)*(s.halfSize.y-s.radius)+
            s.radius*std::hypot(v.x,v.y);
    }
};
}
const ProximityJoins::Components* ProximityJoins::Resolve(std::uint32_t set,const JoinShape* input,int count,std::uint64_t frame) {
    if(!set || count<1 || count>JoinedGeometry::Capacity)return nullptr;
    if(entries_.find(set)==entries_.end() && entries_.size()>=16) {
        for(auto it=entries_.begin();it!=entries_.end();)
            if(it->second.lastFrame+1<frame)it=entries_.erase(it);else ++it;
        if(entries_.size()>=16)return nullptr;
    }
    auto& e=entries_[set];e.lastFrame=frame;
    bool changed=e.count!=count;
    // Compare only explicitly submitted optical geometry. No draw-list scan,
    // scene hash or pointer trajectory is used as a cache key.
    for(int i=0;i<count;++i) {
        changed|=!e.previous[i].SameGeometry(input[i]);e.previous[i]=input[i];
    }
    if(!changed)return &e.components;
    e.count=count;++builds_;
    std::array<int,JoinedGeometry::Capacity> order{};
    std::array<float,JoinedGeometry::Capacity> left{},right{},vertical{};
    std::iota(order.begin(),order.begin()+count,0);
    std::iota(e.components.begin(),e.components.begin()+count,0);
    for(int i=0;i<count;++i) {
        const Support t(input[i].strain);
        const float extent=t.Along(input[i],{1,0});
        left[i]=input[i].center.x-extent-input[i].joinWidth;
        right[i]=input[i].center.x+extent+input[i].joinWidth;
        vertical[i]=t.Along(input[i],{0,1});
    }
    std::sort(order.begin(),order.begin()+count,[&](int a,int b){return left[a]<left[b];});
    const auto root=[&](int i){while(e.components[i]!=i)i=e.components[i];return i;};
    for(int a=0;a<count;++a)for(int b=a+1;b<count && left[order[b]]<=right[order[a]];++b) {
        const int i=order[a],j=order[b];
        const auto& x=input[i];const auto& y=input[j];
        if(x.clip!=y.clip)continue;
        const float join=std::min(x.joinWidth,y.joinWidth);
        const Vec2 d{y.center.x-x.center.x,y.center.y-x.center.y};
        if(std::abs(d.y)>vertical[i]+vertical[j]+join)continue;
        const float distance=std::hypot(d.x,d.y);
        const Vec2 n=distance>1.e-5f?d*(1.f/distance):Vec2{1,0};
        if(distance-Support(x.strain).Along(x,n)-Support(y.strain).Along(y,n)>join)continue;
        const int ri=root(i),rj=root(j);
        e.components[std::max(ri,rj)]=std::min(ri,rj);
    }
    for(int i=0;i<count;++i)e.components[i]=root(i);
    return &e.components;
}
}
