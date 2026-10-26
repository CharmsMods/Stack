#include "Renderer/GlassChrome.h"
#include <algorithm>
#include <cmath>

namespace Stack::Renderer {
void GlassChrome::PrepareJoins(Backdrop& backdrop) {
    constexpr int capacity=Glass::JoinedGeometry::Capacity;
    std::array<ImGuiID,capacity> seen{};
    int seenCount=0;
    for(auto& first:objects) {
        const auto set=first.join.set;
        if(first.backdrop!=&backdrop || !set ||
            std::find(seen.begin(),seen.begin()+seenCount,set)!=seen.begin()+seenCount)continue;
        if(seenCount==capacity)break;
        seen[seenCount++]=set;
        std::array<Object*,capacity> members{};
        std::array<Glass::JoinShape,capacity> shapes{};
        int count=0;bool overflow=false;
        for(auto& object:objects)if(object.backdrop==&backdrop && object.join.set==set) {
            if(count==capacity){overflow=true;break;}
            members[count]=&object;
            shapes[count++]={object.join.item,{object.center.x,object.center.y},
                {object.halfSize.x,object.halfSize.y},object.effects.logStrain,
                object.radius,object.join.width,object.clip};
        }
        if(overflow)continue; // Keep separate lenses when the bounded contract is exceeded.
        const auto* components=joins.Resolve(set,shapes.data(),count,frame);
        if(!components)continue;
        std::array<int,capacity> sizes{};
        for(int i=0;i<count;++i)++sizes[(*components)[i]];
        for(int root=0;root<count;++root) {
            if(sizes[root]<2 || compoundCount>=64)continue;
            if(compoundCount==compounds.size())compounds.emplace_back();
            auto& compound=compounds[compoundCount];compound.geometry.count=0;
            const auto& anchor=*members[root];
            compound.left=anchor.left;compound.bottom=anchor.bottom;
            compound.right=anchor.left+anchor.width;compound.top=anchor.bottom+anchor.height;
            compound.width=anchor.join.width;
            for(int i=root;i<count;++i)if((*components)[i]==root) {
                auto& object=*members[i];
                object.compound=int(compoundCount);object.joinLeader=i==root;
                const auto strain=object.effects.logStrain;
                const float r=std::hypot(strain.x,strain.y),e=std::exp(r);
                const float c=.5f*(e+1.f/e),sh=r<1.e-4f?1.f+r*r/6.f:.5f*(e-1.f/e)/r;
                const auto slot=compound.geometry.count++;
                compound.geometry.shapes[slot]={object.center.x-anchor.center.x,object.center.y-anchor.center.y,
                    object.halfSize.x,object.halfSize.y};
                compound.geometry.metrics[slot]={object.radius,c-sh*strain.x,-sh*strain.y,c+sh*strain.x};
                compound.width=std::min(compound.width,object.join.width);
                compound.left=std::min(compound.left,object.left);compound.bottom=std::min(compound.bottom,object.bottom);
                compound.right=std::max(compound.right,object.left+object.width);compound.top=std::max(compound.top,object.bottom+object.height);
            }
            ++compoundCount;
        }
    }
}
}
