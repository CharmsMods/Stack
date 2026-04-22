#pragma once

#include "RenderTab/Runtime/Geometry/RenderSceneGeometry.h"

#include <vector>

struct RenderBvhNode {
    RenderBounds bounds {};
    int leftChild = -1;
    int rightChild = -1;
    int firstPrimitive = 0;
    int primitiveCount = 0;
    int depth = 0;

    bool IsLeaf() const { return primitiveCount > 0; }
};

std::vector<RenderBvhNode> BuildRenderBvh(std::vector<RenderPrimitiveRef>& primitiveRefs);
