#pragma once
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace EditorNodeGraph {
// Slot identities survive gaps. Relative weights let surviving inputs keep their
// proportions when a wire is removed. An untouched slot has weight one.
struct MixContributions {
    std::array<int, kMaxDataMathInputCount> slots{};
    Stack::NodeMath::MixWeights shares{};
    int count = 0;

    MixContributions(const Graph& graph, const Node& node) {
        double total = 0;
        for (int i = 0; i < kMaxDataMathInputCount; ++i) {
            if (!graph.FindAnyInputLink(node.id, DataMathInputSocketId(i))) continue;
            slots[count] = i;
            shares[count++] = node.dataMathSettings.mixWeights[i];
            total += node.dataMathSettings.mixWeights[i];
        }
        for (int i = 0; i < count; ++i)
            shares[i] = total > 0 ? shares[i] / total : 1.0 / count;
    }

    void SetShare(int index, double share) {
        if (count < 2) return;
        share = std::clamp(share, 0.0, 1.0);
        double peers = 0;
        for (int i = 0; i < count; ++i) if (i != index) peers += shares[i];
        for (int i = 0; i < count; ++i) if (i != index)
            shares[i] = (1 - share) * (peers > 0 ? shares[i] / peers : 1.0 / (count - 1));
        shares[index] = share;
    }

    void MoveBoundary(int left, double delta) {
        delta = std::clamp(delta, -shares[left], shares[left + 1]);
        shares[left] += delta;
        shares[left + 1] -= delta;
    }

    void Store(Node& node) const {
        for (int i = 0; i < count; ++i)
            node.dataMathSettings.mixWeights[slots[i]] = std::clamp(shares[i], 0.0, 1.0) * count;
    }
};
}
