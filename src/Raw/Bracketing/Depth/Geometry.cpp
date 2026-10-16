#include "Geometry.h"
#include "../CaptureCorrespondence.h"
#include <algorithm>
#include <cmath>

namespace Raw::Bracketing::Depth {
bool Triangulate(const PairGeometry& pair,Mfd::RawCoordinate a,Mfd::RawCoordinate b,
    double& inverseDepth,double& parallax,double& error) {
    const cv::Vec3d ray((a.x-pair.cx)/pair.focal,(a.y-pair.cy)/pair.focal,1);
    const double sourceFocal=pair.sourceFocal>0?pair.sourceFocal:pair.focal;
    const cv::Vec3d other((b.x-pair.cx)/sourceFocal,(b.y-pair.cy)/sourceFocal,1);
    const auto rotated=pair.rotation*ray;
    const auto cross=other.cross(rotated),offset=other.cross(pair.translation);
    const double denominator=cross.dot(cross);
    if(denominator<1e-16||rotated[2]<=0)return false;
    const double z=-cross.dot(offset)/denominator;
    const auto point=z*rotated+pair.translation;
    if(!std::isfinite(z)||z<=0||point[2]<=0)return false;
    error=std::hypot(sourceFocal*point[0]/point[2]+pair.cx-b.x,
        sourceFocal*point[1]/point[2]+pair.cy-b.y);
    parallax=std::hypot(sourceFocal*rotated[0]/rotated[2]+pair.cx-b.x,
        sourceFocal*rotated[1]/rotated[2]+pair.cy-b.y);
    inverseDepth=1/z;
    return std::isfinite(inverseDepth)&&std::isfinite(error)&&error<1.5&&parallax>=.75;
}
bool SolvePair(const PreparedDataset& data,std::size_t index,PairGeometry& pair,std::string& reason) {
    const auto& source=data.sources[index];
    if(!source.localMotion||!HasCurrentCaptureReliability(source)){reason="No measured local correspondence.";return false;}
    pair.source=index;pair.cx=(data.width-1)*.5;pair.cy=(data.height-1)*.5;
    std::vector<cv::Point2d> a,b;
    // Pose needs independent measured matches. Sampling at multiples of the
    // spacing misses the grid's nonzero origin and interpolates between
    // different surfaces. The disagreement guard correctly rejects those
    // synthetic points, even when their measured node centers are sound.
    for(const auto& node:source.localMotion->nodes) {
        if(node.state!=Mfd::MotionNodeState::Structured)continue;
        const auto point=node.centerRaw;
        const auto match=EvaluateCaptureCorrespondence(source,point);
        Mfd::LocalMotionFieldSample motion;
        if(match.confidence<.45||!EvaluateCaptureMotion(source,point,{},motion)||motion.alignmentConfidence<.4)continue;
        a.emplace_back(point.x,point.y);b.emplace_back(match.sourceRaw.x,match.sourceRaw.y);
    }
    return SolveGeometry(a,b,pair,reason);
}
}
