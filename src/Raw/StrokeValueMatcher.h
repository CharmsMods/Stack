#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace Stack::RawAssetCapture {
// Exact sample coordinates, with a search tree for color and sorted values for
// luminance. The index accelerates distance queries; it never quantizes samples.
class StrokeValueMatcher {
public:
    StrokeValueMatcher(bool luminance, bool color, float tolerance)
        : m_Luma(luminance), m_Color(color), m_Dimensions((luminance ? 1 : 0) + (color ? 2 : 0)),
          m_Tolerance(std::max(.2, double(tolerance))) {}
    void Add(const std::array<float,3>& value) { m_Points.push_back(Coordinate(value)); }
    void Build() {
        std::sort(m_Points.begin(), m_Points.end());
        m_Points.erase(std::unique(m_Points.begin(), m_Points.end()), m_Points.end());
        if (m_Dimensions > 1) Partition(0, m_Points.size(), 0);
    }
    float Coverage(const std::array<float,3>& value) const {
        if (m_Points.empty()) return 0;
        const auto point = Coordinate(value);
        double distanceSquared = 1;
        if (m_Dimensions == 1) {
            const auto it = std::lower_bound(m_Points.begin(), m_Points.end(), point);
            if (it != m_Points.end()) distanceSquared = std::min(distanceSquared, Square((*it)[0]-point[0]));
            if (it != m_Points.begin()) distanceSquared = std::min(distanceSquared, Square((it-1)->at(0)-point[0]));
        } else Nearest(point, 0, m_Points.size(), 0, distanceSquared);
        const double t = std::sqrt(std::clamp(distanceSquared, 0.0, 1.0));
        return float(1-t*t*(3-2*t));
    }
private:
    using Point = std::array<double,3>;
    static double Square(double x) { return x*x; }
    Point Coordinate(const std::array<float,3>& value) const {
        Point point{}; int axis = 0;
        if (m_Luma) point[axis++] = std::copysign(std::log2(1+std::abs(double(value[0]))/.18),value[0]) / (.15*m_Tolerance);
        if (m_Color) { point[axis++] = value[1]/(.02*m_Tolerance); point[axis] = value[2]/(.02*m_Tolerance); }
        return point;
    }
    void Partition(std::size_t begin, std::size_t end, int depth) {
        if (begin >= end) return;
        const auto middle = begin+(end-begin)/2; const int axis = depth%m_Dimensions;
        std::nth_element(m_Points.begin()+begin,m_Points.begin()+middle,m_Points.begin()+end,
            [axis](const Point& a,const Point& b){return a[axis]<b[axis];});
        Partition(begin,middle,depth+1); Partition(middle+1,end,depth+1);
    }
    void Nearest(const Point& point, std::size_t begin, std::size_t end, int depth, double& best) const {
        if (begin >= end || best == 0) return;
        const auto middle = begin+(end-begin)/2; const auto& sample = m_Points[middle];
        double distance = 0; for (int i=0;i<m_Dimensions;++i) distance += Square(point[i]-sample[i]);
        best = std::min(best,distance);
        const double split = point[depth%m_Dimensions]-sample[depth%m_Dimensions];
        if (split < 0) {
            Nearest(point,begin,middle,depth+1,best);
            if (split*split < best) Nearest(point,middle+1,end,depth+1,best);
        } else {
            Nearest(point,middle+1,end,depth+1,best);
            if (split*split < best) Nearest(point,begin,middle,depth+1,best);
        }
    }
    bool m_Luma, m_Color; int m_Dimensions; double m_Tolerance;
    std::vector<Point> m_Points;
};
}
