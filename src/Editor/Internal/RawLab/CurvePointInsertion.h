#pragma once
#include "Raw/RawDevelopmentRecipe.h"
#include <algorithm>
#include <array>

namespace Stack::Editor::RawLabInternal {
// Split the existing cubic, preserving its shape and the neighboring tangents.
// This runs only on insertion, never while drawing an unchanged plot.
inline int InsertCurvePointOnLine(std::vector<RawRecipe::RawBezierCurvePoint>& points,
    float x, std::vector<RawRecipe::RawBezierSegment> segments) {
    const auto at=std::lower_bound(points.begin(),points.end(),x,[](const auto& p,float value){return p.x<value;});
    const int index=int(at-points.begin());
    if(index<=0 || index>=int(points.size()) || at->x==x)return -1;
    for(size_t i=0;i<segments.size();++i) {
        const auto& s=segments[i];
        points[i].outgoing={1,true,s.leftHandleX-points[i].x,s.leftHandleY-points[i].y};
        points[i+1].incoming={1,true,s.rightHandleX-points[i+1].x,s.rightHandleY-points[i+1].y};
    }
    using P=std::array<float,2>;
    const auto mix=[](P a,P b,float t){return P{a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t};};
    const auto& s=segments[index-1];
    const P a{s.left.x,s.left.y},b{s.leftHandleX,s.leftHandleY},c{s.rightHandleX,s.rightHandleY},d{s.right.x,s.right.y};
    float lo=0,hi=1;
    for(int i=0;i<28;++i) {
        const float t=(lo+hi)*.5f;
        const auto p=mix(mix(mix(a,b,t),mix(b,c,t),t),mix(mix(b,c,t),mix(c,d,t),t),t);
        if(p[0]<x)lo=t;else hi=t;
    }
    const float t=(lo+hi)*.5f;
    const auto ab=mix(a,b,t),bc=mix(b,c,t),cd=mix(c,d,t);
    const auto abc=mix(ab,bc,t),bcd=mix(bc,cd,t),p=mix(abc,bcd,t);
    points[index-1].outgoing={1,true,ab[0]-a[0],ab[1]-a[1]};
    points[index].incoming={1,true,cd[0]-d[0],cd[1]-d[1]};
    RawRecipe::RawBezierCurvePoint added{x,p[1],{1,true,abc[0]-x,abc[1]-p[1]},
        {1,true,bcd[0]-x,bcd[1]-p[1]}};
    points.insert(points.begin()+index,added);
    return index;
}
}
