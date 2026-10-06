#pragma once

#include "Editor/NodeGraph/NodeGraphTypes.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace Stack::Editor::NodeGraphUILayout {

inline constexpr float kCollapsedNodeHeight = 48.0f;
inline constexpr float kTileNodeSize = 104.0f;
inline constexpr float kRawSourceWidth = 144.0f;
inline constexpr float kRawSourceHeight = 80.0f;
inline constexpr float kMediaNodeWidth = 200.0f;
inline constexpr float kMediaNodeHeight = 124.0f;
inline constexpr float kCompactNodeWidth = 264.0f;
inline constexpr float kStandardNodeWidth = 280.0f;
inline constexpr float kWideNodeWidth = 304.0f;
inline constexpr float kComplexNodeWidth = 336.0f;
inline constexpr float kSocketRadius = 5.5f;
inline constexpr float kSocketBodyGap = 6.0f;
inline constexpr float kSocketCenterOffset = kSocketRadius + kSocketBodyGap;
inline constexpr float kSocketLogicalHitPadding = 8.0f;
inline constexpr float kNodeGrabAreaHeight = 38.0f;

enum class NodeWidthClass {
    Tile,
    RawSource,
    Media,
    Compact,
    Standard,
    Wide,
    Complex
};

struct LogicalRect {
    EditorNodeGraph::Vec2 min {};
    EditorNodeGraph::Vec2 max {};

    float Width() const { return std::max(0.0f, max.x - min.x); }
    float Height() const { return std::max(0.0f, max.y - min.y); }
    bool IsValid() const { return max.x > min.x && max.y > min.y; }
};

inline LogicalRect Union(const LogicalRect& lhs, const LogicalRect& rhs) {
    if (!lhs.IsValid()) {
        return rhs;
    }
    if (!rhs.IsValid()) {
        return lhs;
    }
    return {
        { std::min(lhs.min.x, rhs.min.x), std::min(lhs.min.y, rhs.min.y) },
        { std::max(lhs.max.x, rhs.max.x), std::max(lhs.max.y, rhs.max.y) }
    };
}

inline LogicalRect Expanded(const LogicalRect& rect, float amount) {
    return {
        { rect.min.x - amount, rect.min.y - amount },
        { rect.max.x + amount, rect.max.y + amount }
    };
}

struct SocketLayout {
    std::string socketId;
    EditorNodeGraph::SocketDirection direction =
        EditorNodeGraph::SocketDirection::Input;
    EditorNodeGraph::Vec2 center {};
};

struct NodeControlSlot {
    std::string id;
    LogicalRect rect;
};

struct NodeBoundsSet {
    LogicalRect body;
    LogicalRect persistentVisual;
    LogicalRect overlay;
    LogicalRect interaction;
};

struct NodeLogicalLayout {
    NodeWidthClass widthClass = NodeWidthClass::Compact;
    LogicalRect body;
    LogicalRect header;
    LogicalRect content;
    NodeBoundsSet bounds;
    std::vector<std::string> titleLines;
    bool titleEllipsized = false;
    std::vector<NodeControlSlot> controlSlots;
    std::vector<SocketLayout> sockets;
};

struct NodeProjectedLayout {
    LogicalRect body;
    LogicalRect header;
    LogicalRect content;
    NodeBoundsSet bounds;
    std::vector<NodeControlSlot> controlSlots;
    std::vector<SocketLayout> sockets;
};

struct SocketLayoutSpec {
    std::string socketId;
    EditorNodeGraph::SocketDirection direction =
        EditorNodeGraph::SocketDirection::Input;
};

struct NodeLayoutBuildSpec {
    NodeWidthClass widthClass = NodeWidthClass::Compact;
    float width = kCompactNodeWidth;
    float height = kCollapsedNodeHeight;
    bool expanded = false;
    bool framelessMedia = false;
    bool sharedIdentityRow = false;
    float headerInsetX = 12.0f;
    float headerInsetY = 10.0f;
    float bodyInsetBottom = 10.0f;
    float sectionGap = 8.0f;
    float kindLabelHeight = 11.0f;
    float titleLineHeight = 13.0f;
    bool showKindLabel = false;
    std::vector<std::string> titleLines;
    bool titleEllipsized = false;
    std::vector<SocketLayoutSpec> sockets;
};

inline float WidthForClass(NodeWidthClass widthClass) {
    switch (widthClass) {
        case NodeWidthClass::Tile: return kTileNodeSize;
        case NodeWidthClass::RawSource: return kRawSourceWidth;
        case NodeWidthClass::Media: return kMediaNodeWidth;
        case NodeWidthClass::Compact: return kCompactNodeWidth;
        case NodeWidthClass::Standard: return kStandardNodeWidth;
        case NodeWidthClass::Wide: return kWideNodeWidth;
        case NodeWidthClass::Complex: return kComplexNodeWidth;
    }
    return kCompactNodeWidth;
}

// This switch is intentionally exhaustive. Tests iterate through NodeKind::Count
// and fail if a newly added kind does not have an explicit mapping.
inline bool TryDefaultWidthClassForKind(
    EditorNodeGraph::NodeKind kind,
    NodeWidthClass* outWidthClass) {
    using EditorNodeGraph::NodeKind;
    const auto assign = [&](NodeWidthClass value) {
        if (outWidthClass) {
            *outWidthClass = value;
        }
        return true;
    };
    switch (kind) {
        case NodeKind::Image:
        case NodeKind::Output:
        case NodeKind::RawProjectFrame:
            return assign(NodeWidthClass::Media);
        case NodeKind::RawSource:
            return assign(NodeWidthClass::RawSource);
        case NodeKind::RawDevelopment:
        case NodeKind::RawNeuralDenoise:
        case NodeKind::RawDecode:
        case NodeKind::RawDevelop:
        case NodeKind::RawDetailAutoMask:
        case NodeKind::RawDetailFusion:
        case NodeKind::HdrMerge:
        case NodeKind::Mfsr:
        case NodeKind::MultiFrameDenoise:
        case NodeKind::MultiFrameHdr:
        case NodeKind::RawProjectSourceSet:
        case NodeKind::Lut:
        case NodeKind::ChannelSplit:
        case NodeKind::ChannelCombine:
        case NodeKind::CustomMask:
            return assign(NodeWidthClass::Tile);
        case NodeKind::Layer:
        case NodeKind::MaskGenerator:
        case NodeKind::MaskCombine:
        case NodeKind::MaskUtility:
        case NodeKind::ImageToMask:
        case NodeKind::FrequencyMask:
            return assign(NodeWidthClass::Standard);
        case NodeKind::Composite:
        case NodeKind::Scope:
        case NodeKind::Mix:
        case NodeKind::ImageGenerator:
        case NodeKind::CombineSpectra:
        case NodeKind::SpectrumRecombine:
            return assign(NodeWidthClass::Wide);
        case NodeKind::Compound:
        case NodeKind::FrequencyFilter:
        case NodeKind::FrequencyResponse:
            return assign(NodeWidthClass::Complex);
        case NodeKind::Preview:
        case NodeKind::ConstantChannel:
        case NodeKind::DataMath:
        case NodeKind::Value:
        case NodeKind::RawOperation:
        case NodeKind::TechnicalImage:
        case NodeKind::FrequencyFft:
        case NodeKind::FrequencyIfft:
        case NodeKind::SpectrumView:
        case NodeKind::ApplyFrequencyResponse:
        case NodeKind::SpectrumSeparate:
        case NodeKind::SpectrumMath:
        case NodeKind::MagnitudePhase:
        case NodeKind::SpectrumAnalyzer:
        case NodeKind::FieldMean:
        case NodeKind::Reformat:
            return assign(NodeWidthClass::Compact);
        case NodeKind::Count:
            break;
    }
    return false;
}

inline NodeWidthClass DefaultWidthClassForKind(
    EditorNodeGraph::NodeKind kind) {
    NodeWidthClass widthClass = NodeWidthClass::Compact;
    const bool resolved =
        TryDefaultWidthClassForKind(kind, &widthClass);
    assert(resolved && "NodeKind lacks a canonical width-class mapping");
    return widthClass;
}

inline NodeWidthClass ResolveWidthClass(
    EditorNodeGraph::NodeKind kind,
    bool summaryOnly,
    bool routeSquare,
    bool richExpandedSurface) {
    if (kind == EditorNodeGraph::NodeKind::RawSource) {
        return NodeWidthClass::RawSource;
    }
    if (kind == EditorNodeGraph::NodeKind::Image ||
        kind == EditorNodeGraph::NodeKind::Output) {
        return NodeWidthClass::Media;
    }
    if (summaryOnly || routeSquare) {
        return NodeWidthClass::Tile;
    }
    if (richExpandedSurface) {
        return NodeWidthClass::Complex;
    }
    return DefaultWidthClassForKind(kind);
}

inline NodeLogicalLayout BuildLogicalNodeLayout(
    const NodeLayoutBuildSpec& spec) {
    const float width = std::max(1.0f, spec.width);
    const float height = std::max(1.0f, spec.height);
    const float kindLabelBlock =
        spec.showKindLabel ? spec.kindLabelHeight + 2.0f : 0.0f;
    const float titleBlock = spec.titleLines.empty()
        ? 0.0f
        : static_cast<float>(spec.titleLines.size()) *
                spec.titleLineHeight +
            static_cast<float>(spec.titleLines.size() - 1) * 2.0f;
    const float headerVisualHeight =
        spec.headerInsetY + kindLabelBlock + titleBlock;
    const float expandedHeaderHeight = std::max(
        headerVisualHeight +
            std::max(6.0f, spec.sectionGap * 0.65f),
        kNodeGrabAreaHeight);
    const float collapsedHeaderHeight = std::max(
        headerVisualHeight + spec.headerInsetY * 0.45f,
        height);
    const float headerBottom = std::clamp(
        spec.expanded && spec.sharedIdentityRow ? spec.headerInsetY :
        spec.expanded ? expandedHeaderHeight : collapsedHeaderHeight,
        0.0f,
        height);

    NodeLogicalLayout logical;
    logical.widthClass = spec.widthClass;
    logical.body = { { 0.0f, 0.0f }, { width, height } };
    logical.header = {
        { 0.0f, 0.0f },
        { width, headerBottom }
    };
    logical.content = {
        {
            spec.headerInsetX,
            spec.expanded ? headerBottom : spec.headerInsetY
        },
        {
            std::max(spec.headerInsetX, width - spec.headerInsetX),
            std::max(
                spec.expanded ? headerBottom : spec.headerInsetY,
                height - spec.bodyInsetBottom)
        }
    };
    if (spec.framelessMedia) {
        logical.header = logical.body;
        logical.content = {
            { 2.0f, 2.0f },
            {
                std::max(2.0f, width - 2.0f),
                std::max(2.0f, height - 2.0f)
            }
        };
    }
    logical.titleLines = spec.titleLines;
    logical.titleEllipsized = spec.titleEllipsized;
    logical.controlSlots.push_back(
        NodeControlSlot { "content", logical.content });

    const auto socketCount =
        [&](EditorNodeGraph::SocketDirection direction) {
            return static_cast<std::size_t>(std::count_if(
                spec.sockets.begin(),
                spec.sockets.end(),
                [&](const SocketLayoutSpec& socket) {
                    return socket.direction == direction;
                }));
        };
    const auto distributeAnchors =
        [&](EditorNodeGraph::SocketDirection direction) {
            const std::size_t count = socketCount(direction);
            if (count == 0) {
                return;
            }
            const float top = std::max(
                kSocketRadius + 4.0f,
                spec.headerInsetY * 0.9f);
            const float bottom = spec.expanded
                ? std::max(
                    top,
                    logical.content.max.y -
                        std::max(
                            kSocketRadius + 2.0f,
                            spec.bodyInsetBottom * 0.2f))
                : std::max(
                    top,
                    logical.body.max.y -
                        std::max(
                            kSocketRadius + 4.0f,
                            spec.headerInsetY * 0.9f));

            std::size_t index = 0;
            for (const SocketLayoutSpec& socket : spec.sockets) {
                if (socket.direction != direction) {
                    continue;
                }
                const float y = count == 1
                    ? (top + bottom) * 0.5f
                    : top +
                        ((bottom - top) *
                            static_cast<float>(index) /
                            static_cast<float>(count - 1));
                logical.sockets.push_back(SocketLayout {
                    socket.socketId,
                    direction,
                    {
                        direction ==
                                EditorNodeGraph::SocketDirection::Input
                            ? -kSocketCenterOffset
                            : width + kSocketCenterOffset,
                        y
                    }
                });
                ++index;
            }
        };
    distributeAnchors(EditorNodeGraph::SocketDirection::Input);
    distributeAnchors(EditorNodeGraph::SocketDirection::Output);

    logical.bounds.body = logical.body;
    logical.bounds.persistentVisual = logical.body;
    logical.bounds.interaction = logical.body;
    for (const SocketLayout& socket : logical.sockets) {
        const LogicalRect visualSocket {
            {
                socket.center.x - kSocketRadius,
                socket.center.y - kSocketRadius
            },
            {
                socket.center.x + kSocketRadius,
                socket.center.y + kSocketRadius
            }
        };
        const float hitRadius =
            kSocketRadius + kSocketLogicalHitPadding;
        const LogicalRect interactionSocket {
            {
                socket.center.x - hitRadius,
                socket.center.y - hitRadius
            },
            {
                socket.center.x + hitRadius,
                socket.center.y + hitRadius
            }
        };
        logical.bounds.persistentVisual = Union(
            logical.bounds.persistentVisual, visualSocket);
        logical.bounds.interaction = Union(
            logical.bounds.interaction, interactionSocket);
    }
    logical.bounds.overlay = logical.bounds.persistentVisual;
    return logical;
}

inline std::uint64_t LogicalLayoutHash(
    const NodeLogicalLayout& layout) {
    std::uint64_t seed = 1469598103934665603ull;
    const auto mix = [&](std::size_t value) {
        seed ^= static_cast<std::uint64_t>(value);
        seed *= 1099511628211ull;
    };
    const auto mixFloat = [&](float value) {
        mix(std::hash<float>{}(value));
    };
    mix(static_cast<std::size_t>(layout.widthClass));
    for (const LogicalRect* rect : {
            &layout.body,
            &layout.header,
            &layout.content,
            &layout.bounds.body,
            &layout.bounds.persistentVisual,
            &layout.bounds.overlay,
            &layout.bounds.interaction }) {
        mixFloat(rect->min.x);
        mixFloat(rect->min.y);
        mixFloat(rect->max.x);
        mixFloat(rect->max.y);
    }
    for (const std::string& line : layout.titleLines) {
        mix(std::hash<std::string>{}(line));
    }
    mix(layout.titleEllipsized ? 1u : 0u);
    for (const NodeControlSlot& slot : layout.controlSlots) {
        mix(std::hash<std::string>{}(slot.id));
        mixFloat(slot.rect.min.x);
        mixFloat(slot.rect.min.y);
        mixFloat(slot.rect.max.x);
        mixFloat(slot.rect.max.y);
    }
    for (const SocketLayout& socket : layout.sockets) {
        mix(std::hash<std::string>{}(socket.socketId));
        mix(static_cast<std::size_t>(socket.direction));
        mixFloat(socket.center.x);
        mixFloat(socket.center.y);
    }
    return seed;
}

inline EditorNodeGraph::Vec2 ProjectPoint(
    const EditorNodeGraph::Vec2& local,
    const EditorNodeGraph::Vec2& nodePosition,
    const EditorNodeGraph::Vec2& canvasOrigin,
    const EditorNodeGraph::Vec2& pan,
    float zoom) {
    return {
        canvasOrigin.x + pan.x + (nodePosition.x + local.x) * zoom,
        canvasOrigin.y + pan.y + (nodePosition.y + local.y) * zoom
    };
}

inline LogicalRect ProjectRect(
    const LogicalRect& rect,
    const EditorNodeGraph::Vec2& nodePosition,
    const EditorNodeGraph::Vec2& canvasOrigin,
    const EditorNodeGraph::Vec2& pan,
    float zoom) {
    return {
        ProjectPoint(rect.min, nodePosition, canvasOrigin, pan, zoom),
        ProjectPoint(rect.max, nodePosition, canvasOrigin, pan, zoom)
    };
}

inline NodeProjectedLayout ProjectNodeLayout(
    const NodeLogicalLayout& logical,
    const EditorNodeGraph::Vec2& nodePosition,
    const EditorNodeGraph::Vec2& canvasOrigin,
    const EditorNodeGraph::Vec2& pan,
    float zoom) {
    NodeProjectedLayout projected;
    projected.body = ProjectRect(
        logical.body, nodePosition, canvasOrigin, pan, zoom);
    projected.header = ProjectRect(
        logical.header, nodePosition, canvasOrigin, pan, zoom);
    projected.content = ProjectRect(
        logical.content, nodePosition, canvasOrigin, pan, zoom);
    projected.bounds.body = ProjectRect(
        logical.bounds.body, nodePosition, canvasOrigin, pan, zoom);
    projected.bounds.persistentVisual = ProjectRect(
        logical.bounds.persistentVisual,
        nodePosition,
        canvasOrigin,
        pan,
        zoom);
    projected.bounds.overlay = ProjectRect(
        logical.bounds.overlay, nodePosition, canvasOrigin, pan, zoom);
    projected.bounds.interaction = ProjectRect(
        logical.bounds.interaction,
        nodePosition,
        canvasOrigin,
        pan,
        zoom);
    projected.controlSlots.reserve(logical.controlSlots.size());
    for (const NodeControlSlot& slot : logical.controlSlots) {
        projected.controlSlots.push_back(NodeControlSlot {
            slot.id,
            ProjectRect(
                slot.rect,
                nodePosition,
                canvasOrigin,
                pan,
                zoom)
        });
    }
    projected.sockets.reserve(logical.sockets.size());
    for (const SocketLayout& socket : logical.sockets) {
        SocketLayout projectedSocket = socket;
        projectedSocket.center = ProjectPoint(
            socket.center, nodePosition, canvasOrigin, pan, zoom);
        projected.sockets.push_back(std::move(projectedSocket));
    }
    return projected;
}

inline LogicalRect AbsoluteGraphBounds(
    const NodeLogicalLayout& logical,
    const EditorNodeGraph::Vec2& nodePosition) {
    return {
        {
            nodePosition.x + logical.bounds.persistentVisual.min.x,
            nodePosition.y + logical.bounds.persistentVisual.min.y
        },
        {
            nodePosition.x + logical.bounds.persistentVisual.max.x,
            nodePosition.y + logical.bounds.persistentVisual.max.y
        }
    };
}

} // namespace Stack::Editor::NodeGraphUILayout
