#include "GraphFlowLayout.h"
#include "GraphLayoutPlacement.h"

namespace Stack::GraphLayout {
std::unordered_map<int, EditorNodeGraph::Vec2> ArrangeFlow(
    std::vector<std::vector<int>> columns,
    const std::unordered_map<int, FlowSize>& sizes,
    const std::vector<FlowEdge>& edges, EditorNodeGraph::Vec2 origin) {
    using namespace Placement;
    auto measured=sizes;
    std::unordered_map<int, int> rank;
    for(std::size_t r=0;r<columns.size();++r)for(int id:columns[r])rank[id]=int(r);
    std::vector<FlowEdge> segments;
    std::unordered_map<int,float> branchHeight;
    for(const auto& [id,size]:sizes)branchHeight[id]=size.height;
    int virtualId=-1;
    for(const auto& edge:edges) {
        const int first=rank.at(edge.from),last=rank.at(edge.to);
        if(first>=last)continue;
        int previous=edge.from;float sourceY=edge.sourceY;
        for(int r=first+1;r<last;++r) {
            if(virtualId < -4096)throw std::runtime_error("Graph expansion exceeds the wire passage budget.");
            columns[r].push_back(virtualId);measured.emplace(virtualId,FlowSize{0,14});
            branchHeight[virtualId]=sizes.at(edge.from).height;
            segments.push_back({previous,virtualId,sourceY,7});
            previous=virtualId--;sourceY=7;
        }
        segments.push_back({previous,edge.to,sourceY,edge.targetY});
    }
    std::unordered_map<int,std::vector<FlowEdge>> inputs,outputs;
    for(const auto& segment:segments){inputs[segment.to].push_back(segment);outputs[segment.from].push_back(segment);}
    std::unordered_map<int,float> top;
    const auto rowGap=[](int a,int b) {
        // Parallel node branches get generous breathing room. Long-wire
        // passage slots stay narrower than a real branch, rather than making
        // every reservation consume the same amount of vertical space.
        return a>=0 && b>=0?128.f:a<0 && b<0?28.f:52.f;
    };
    const auto placeColumn=[&](const std::vector<int>& column,const std::unordered_map<int,float>* targets=nullptr) {
        if(column.empty())return;
        float height=0;
        for(std::size_t i=0;i<column.size();++i)height+=measured.at(column[i]).height+(i?rowGap(column[i-1],column[i]):0);
        float y=origin.y-height*.5f;
        if(!targets) {
            for(std::size_t i=0;i<column.size();++i) {
                top[column[i]]=y;
                y+=measured.at(column[i]).height+(i+1<column.size()?rowGap(column[i],column[i+1]):0);
            }
            return;
        }
        // Project desired rows onto non-overlapping rows in linear time (PAVA).
        // A continuing branch follows its upstream lane instead of every
        // column independently collapsing back onto the center line.
        struct Band {std::size_t begin,end;float sum,weight;};
        std::vector<Band> bands;std::vector<float> offsets;bands.reserve(column.size());offsets.reserve(column.size());
        float offset=0;
        for(std::size_t i=0;i<column.size();++i) {
            const int id=column[i];const float weight=id>=0?1.f:.25f;
            offsets.push_back(offset);bands.push_back({i,i+1,(targets->at(id)-offset)*weight,weight});
            while(bands.size()>1) {
                auto& right=bands.back();auto& left=bands[bands.size()-2];
                if(left.sum/left.weight<=right.sum/right.weight)break;
                left.end=right.end;left.sum+=right.sum;left.weight+=right.weight;bands.pop_back();
            }
            offset+=measured.at(id).height+(i+1<column.size()?rowGap(id,column[i+1]):0);
        }
        for(const auto& band:bands)for(std::size_t i=band.begin;i<band.end;++i)
            top[column[i]]=band.sum/band.weight+offsets[i];
    };
    for(const auto& column:columns)placeColumn(column);
    // Reserve a distinct approach band for each feeder into a merge. In
    // particular, a short feeder and a much longer chain must not collapse
    // onto the same centerline just because they occupy different columns.
    // Virtual passages carry the producer's footprint back through those
    // columns; they remain temporary layout data, not routing control points.
    std::unordered_map<int,std::unordered_map<int,float>> approach;
    for(const auto& [target,incoming]:inputs) {
        if(target<0)continue;
        auto ordered=incoming;
        std::stable_sort(ordered.begin(),ordered.end(),[&](const auto& a,const auto& b) {
            if(a.targetY!=b.targetY)return a.targetY<b.targetY;
            if(top.at(a.from)!=top.at(b.from))return top.at(a.from)<top.at(b.from);
            return a.from<b.from;
        });
        std::vector<int> feeders;
        std::unordered_set<int> seenFeeders;
        for(const auto& edge:ordered)if(seenFeeders.insert(edge.from).second)
            feeders.push_back(edge.from);
        if(feeders.size()<2)continue;
        float extent=0;
        for(int id:feeders)extent+=branchHeight.at(id);
        extent+=144.f*float(feeders.size()-1);
        float cursor=-extent*.5f;
        for(int id:feeders) {
            approach[target][id]=cursor+branchHeight.at(id)*.5f;
            cursor+=branchHeight.at(id)+144.f;
        }
    }
    // Actual port positions order branches. Dummy passages propagate the order
    // of long edges through every crossed column, as in layered graph drawing.
    for(int sweep=0;sweep<2;++sweep)for(std::size_t step=0;step<columns.size();++step) {
        auto& column=columns[sweep==0?step:columns.size()-step-1];
        std::unordered_map<int,float> scores;
        for(int id:column) {
            const auto& neighbors=sweep==0?inputs[id]:outputs[id];
            float sum=0;
            for(const auto& edge:neighbors)sum+=sweep==0
                ?top.at(edge.from)+edge.sourceY-edge.targetY
                :top.at(edge.to)+edge.targetY-edge.sourceY+
                    (approach.count(edge.to) && approach.at(edge.to).count(edge.from)
                        ?approach.at(edge.to).at(edge.from):0.f);
            scores[id]=neighbors.empty()?top[id]:sum/neighbors.size();
        }
        std::stable_sort(column.begin(),column.end(),[&](int a,int b){return scores[a]<scores[b];});
        placeColumn(column,&scores);
    }
    std::vector<std::vector<std::pair<float,float>>> traffic(columns.size());
    // Count overlapping vertical intervals in each gap. Quiet gaps retain the
    // existing spacing; only a crowded routing corridor widens.
    std::unordered_map<int,int> placedRank;
    for(std::size_t r=0;r<columns.size();++r)for(int id:columns[r])placedRank[id]=int(r);
    for(const auto& edge:segments) {
        const float a=top.at(edge.from)+edge.sourceY,b=top.at(edge.to)+edge.targetY;
        traffic[placedRank.at(edge.from)].push_back({std::min(a,b),std::max(a,b)});
    }
    std::unordered_map<int,Vec2> result;
    float x=origin.x;
    for(std::size_t r=0;r<columns.size();++r) {
        float width=0;
        for(int id:columns[r]) {
            width=std::max(width,measured.at(id).width);
            if(id>=0)result.emplace(id,Vec2{x,top.at(id)});
        }
        std::vector<std::pair<float,int>> events;
        for(auto interval:traffic[r]){events.push_back({interval.first,1});events.push_back({interval.second,-1});}
        std::sort(events.begin(),events.end(),[](auto a,auto b){return a.first==b.first?a.second>b.second:a.first<b.first;});
        int count=0,peak=0;
        for(auto event:events){count+=event.second;peak=std::max(peak,count);}
        x+=width+std::max(kColumnGap,32.f+14.f*peak);
    }
    return result;
}
}
