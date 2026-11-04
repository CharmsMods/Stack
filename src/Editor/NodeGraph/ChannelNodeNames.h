#pragma once

#include "Editor/NodeGraph/NodeGraphTypes.h"

namespace EditorNodeGraph {
inline constexpr char kSplitChannelsTitle[] = "Split Channels";
inline constexpr char kCombineChannelsTitle[] = "Combine Channels";

// Catalog and editor wording can change independently of the hashed declaration.
inline const char* NodeDisplayLabel(NodeKind kind, const std::string& declaredLabel) {
    if (kind == NodeKind::ChannelSplit) return kSplitChannelsTitle;
    if (kind == NodeKind::ChannelCombine) return kCombineChannelsTitle;
    return declaredLabel.c_str();
}
}
