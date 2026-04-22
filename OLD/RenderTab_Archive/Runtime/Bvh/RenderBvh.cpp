#include "RenderBvh.h"

#include <algorithm>

namespace {

constexpr int kLeafPrimitiveCount = 2;

RenderBounds ComputeRangeBounds(const std::vector<RenderPrimitiveRef>& refs, int start, int count) {
    RenderBounds bounds = refs[static_cast<std::size_t>(start)].bounds;
    for (int i = 1; i < count; ++i) {
        bounds = UnionBounds(bounds, refs[static_cast<std::size_t>(start + i)].bounds);
    }
    return bounds;
}

RenderBounds ComputeCentroidBounds(const std::vector<RenderPrimitiveRef>& refs, int start, int count) {
    const RenderFloat3 firstCentroid = BoundsCentroid(refs[static_cast<std::size_t>(start)].bounds);
    RenderBounds bounds { firstCentroid, firstCentroid };

    for (int i = 1; i < count; ++i) {
        const RenderFloat3 centroid = BoundsCentroid(refs[static_cast<std::size_t>(start + i)].bounds);
        bounds.min.x = std::min(bounds.min.x, centroid.x);
        bounds.min.y = std::min(bounds.min.y, centroid.y);
        bounds.min.z = std::min(bounds.min.z, centroid.z);
        bounds.max.x = std::max(bounds.max.x, centroid.x);
        bounds.max.y = std::max(bounds.max.y, centroid.y);
        bounds.max.z = std::max(bounds.max.z, centroid.z);
    }

    return bounds;
}

int LongestAxis(const RenderBounds& bounds) {
    const RenderFloat3 extent = BoundsExtent(bounds);
    if (extent.y > extent.x && extent.y >= extent.z) {
        return 1;
    }
    if (extent.z > extent.x && extent.z >= extent.y) {
        return 2;
    }
    return 0;
}

float AxisValue(const RenderFloat3& value, int axis) {
    if (axis == 1) {
        return value.y;
    }
    if (axis == 2) {
        return value.z;
    }
    return value.x;
}

class RenderBvhBuilder {
public:
    explicit RenderBvhBuilder(std::vector<RenderPrimitiveRef>& refs)
        : m_Refs(refs) {
        m_Nodes.reserve(m_Refs.size() * 2);
    }

    std::vector<RenderBvhNode> Build() {
        if (m_Refs.empty()) {
            return {};
        }

        BuildNode(0, static_cast<int>(m_Refs.size()), 0);
        return m_Nodes;
    }

private:
    int BuildNode(int start, int count, int depth) {
        const RenderBounds bounds = ComputeRangeBounds(m_Refs, start, count);
        const int nodeIndex = static_cast<int>(m_Nodes.size());
        m_Nodes.push_back(RenderBvhNode {});

        m_Nodes[nodeIndex].bounds = bounds;
        m_Nodes[nodeIndex].depth = depth;

        if (count <= kLeafPrimitiveCount) {
            m_Nodes[nodeIndex].firstPrimitive = start;
            m_Nodes[nodeIndex].primitiveCount = count;
            return nodeIndex;
        }

        const RenderBounds centroidBounds = ComputeCentroidBounds(m_Refs, start, count);
        const RenderFloat3 centroidExtent = BoundsExtent(centroidBounds);
        if (NearlyEqual(centroidExtent.x, 0.0f) &&
            NearlyEqual(centroidExtent.y, 0.0f) &&
            NearlyEqual(centroidExtent.z, 0.0f)) {
            m_Nodes[nodeIndex].firstPrimitive = start;
            m_Nodes[nodeIndex].primitiveCount = count;
            return nodeIndex;
        }

        const int axis = LongestAxis(centroidBounds);
        const int mid = start + count / 2;
        std::nth_element(
            m_Refs.begin() + start,
            m_Refs.begin() + mid,
            m_Refs.begin() + start + count,
            [axis](const RenderPrimitiveRef& left, const RenderPrimitiveRef& right) {
                return AxisValue(BoundsCentroid(left.bounds), axis) < AxisValue(BoundsCentroid(right.bounds), axis);
            });

        const int left = BuildNode(start, mid - start, depth + 1);
        const int right = BuildNode(mid, count - (mid - start), depth + 1);
        m_Nodes[nodeIndex].leftChild = left;
        m_Nodes[nodeIndex].rightChild = right;
        m_Nodes[nodeIndex].firstPrimitive = -1;
        m_Nodes[nodeIndex].primitiveCount = 0;
        return nodeIndex;
    }

    std::vector<RenderPrimitiveRef>& m_Refs;
    std::vector<RenderBvhNode> m_Nodes;
};

} // namespace

std::vector<RenderBvhNode> BuildRenderBvh(std::vector<RenderPrimitiveRef>& primitiveRefs) {
    RenderBvhBuilder builder(primitiveRefs);
    return builder.Build();
}
