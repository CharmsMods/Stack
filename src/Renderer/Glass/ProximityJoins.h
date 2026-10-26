#pragma once
#include "JoinedGeometry.h"
#include "glass_math.hpp"
#include <cstdint>
#include <unordered_map>

namespace Stack::Renderer::Glass {
struct JoinShape {
    std::uint32_t identity=0;
    stack::glass::Vec2 center{},halfSize{},strain{};
    float radius=0,joinWidth=0;
    std::array<float,4> clip{};
    bool SameGeometry(const JoinShape&) const;
};
class ProximityJoins {
public:
    using Components=std::array<int,JoinedGeometry::Capacity>;
    const Components* Resolve(std::uint32_t set,const JoinShape*,int count,std::uint64_t frame);
    std::uint64_t BuildCount() const {return builds_;}
    void Clear(){entries_.clear();builds_=0;}
private:
    struct Entry {
        std::array<JoinShape,JoinedGeometry::Capacity> previous{};
        Components components{};
        int count=0;
        std::uint64_t lastFrame=0;
    };
    std::unordered_map<std::uint32_t,Entry> entries_;
    std::uint64_t builds_=0;
};
}
