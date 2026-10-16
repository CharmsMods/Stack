#pragma once
#include "Depth.h"
#include "../ProcessingInternal.h"
#include <opencv2/core.hpp>

namespace Raw::Bracketing::Depth {
struct PairGeometry {
    std::size_t source=0;
    cv::Matx33d rotation=cv::Matx33d::eye();
    cv::Vec3d translation{};
    double focal=0,sourceFocal=0,cx=0,cy=0,parallax=0,calibrationConfidence=1;
    unsigned inliers=0;
};
bool SolvePair(const PreparedDataset&,std::size_t,PairGeometry&,std::string&);
bool SolveGeometry(const std::vector<cv::Point2d>& reference,const std::vector<cv::Point2d>& source,
    PairGeometry&,std::string&);
bool Triangulate(const PairGeometry&,Mfd::RawCoordinate reference,Mfd::RawCoordinate source,
    double& inverseDepth,double& parallax,double& error);
}
