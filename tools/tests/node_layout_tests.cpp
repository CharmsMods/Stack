#include "Editor/NodeGraph/UI/EditorNodeGraphUILayout.h"

#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using namespace Stack::Editor::NodeGraphUILayout;

void RequireLayout(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

static_assert(
    static_cast<std::size_t>(EditorNodeGraph::NodeKind::Count) == 50,
    "Update node layout coverage when adding a NodeKind");

bool NearlyEqual(float lhs, float rhs, float epsilon = 0.0005f) {
    return std::abs(lhs - rhs) <= epsilon;
}

void TestEveryNodeKindHasCanonicalWidth() {
    for (std::size_t index = 0;
         index <
            static_cast<std::size_t>(
                EditorNodeGraph::NodeKind::Count);
         ++index) {
        const EditorNodeGraph::NodeKind kind =
            static_cast<EditorNodeGraph::NodeKind>(index);
        NodeWidthClass widthClass = NodeWidthClass::Compact;
        RequireLayout(
            TryDefaultWidthClassForKind(kind, &widthClass),
            "every NodeKind must have an explicit width-class mapping");
        RequireLayout(
            WidthForClass(widthClass) >= kTileNodeSize,
            "every NodeKind must resolve to a valid canonical width");
    }

    RequireLayout(
        DefaultWidthClassForKind(EditorNodeGraph::NodeKind::ConstantChannel) ==
            NodeWidthClass::Compact,
        "Constant Channel must use the canonical compact layout");
    RequireLayout(
        DefaultWidthClassForKind(EditorNodeGraph::NodeKind::TechnicalImage) ==
            NodeWidthClass::Compact,
        "Technical Image must use the canonical compact layout");
    RequireLayout(
        DefaultWidthClassForKind(EditorNodeGraph::NodeKind::Compound) ==
            NodeWidthClass::Complex,
        "Compound must use the canonical complex layout");
    RequireLayout(
        DefaultWidthClassForKind(EditorNodeGraph::NodeKind::RawProjectFrame) ==
            NodeWidthClass::Media,
        "RAW Project Frame must use the canonical media layout");
    RequireLayout(
        DefaultWidthClassForKind(EditorNodeGraph::NodeKind::MultiFrameDenoise) ==
            NodeWidthClass::Tile,
        "Multi-Frame Denoise must use the canonical compact tile layout");
    RequireLayout(
        DefaultWidthClassForKind(
            EditorNodeGraph::NodeKind::RawProjectSourceSet) ==
            NodeWidthClass::Tile,
        "RAW Project Source Set must use the canonical compact tile layout");
}

NodeLayoutBuildSpec MakeLayoutSpec(
    EditorNodeGraph::NodeKind kind,
    bool expanded,
    std::size_t inputCount,
    std::size_t outputCount,
    float expandedHeight = 286.0f) {
    NodeLayoutBuildSpec spec;
    spec.widthClass = DefaultWidthClassForKind(kind);
    spec.width = WidthForClass(spec.widthClass);
    spec.expanded = expanded;
    spec.framelessMedia =
        spec.widthClass == NodeWidthClass::Media;
    if (spec.widthClass == NodeWidthClass::Tile) {
        spec.height = kTileNodeSize;
    } else if (spec.widthClass == NodeWidthClass::RawSource) {
        spec.height = kRawSourceHeight;
    } else if (spec.widthClass == NodeWidthClass::Media) {
        spec.height = kMediaNodeHeight;
    } else {
        spec.height = expanded
            ? expandedHeight
            : kCollapsedNodeHeight;
    }
    spec.showKindLabel = expanded && !spec.framelessMedia;
    spec.titleLines = {
        "Canonical title",
        "fixed second line"
    };
    for (std::size_t index = 0; index < inputCount; ++index) {
        spec.sockets.push_back(SocketLayoutSpec {
            "input_" + std::to_string(index),
            EditorNodeGraph::SocketDirection::Input
        });
    }
    for (std::size_t index = 0; index < outputCount; ++index) {
        spec.sockets.push_back(SocketLayoutSpec {
            "output_" + std::to_string(index),
            EditorNodeGraph::SocketDirection::Output
        });
    }
    return spec;
}

void TestEveryNodeKindBuildsInBothStates() {
    for (std::size_t index = 0;
         index <
            static_cast<std::size_t>(
                EditorNodeGraph::NodeKind::Count);
         ++index) {
        const EditorNodeGraph::NodeKind kind =
            static_cast<EditorNodeGraph::NodeKind>(index);
        for (const bool expanded : { false, true }) {
            const NodeLayoutBuildSpec spec =
                MakeLayoutSpec(kind, expanded, 3, 4);
            const NodeLogicalLayout layout =
                BuildLogicalNodeLayout(spec);
            RequireLayout(
                NearlyEqual(
                    layout.body.Width(),
                    WidthForClass(spec.widthClass)),
                "built node width must match its canonical class");
            RequireLayout(
                layout.controlSlots.size() == 1 &&
                    layout.controlSlots.front().id == "content",
                "every node must expose its canonical content slot");
            if (expanded && !spec.framelessMedia) {
                RequireLayout(
                    layout.header.max.y <=
                        layout.content.min.y + 0.0005f,
                    "expanded header and control content must not overlap");
            }
            for (const SocketLayout& socket : layout.sockets) {
                const bool input =
                    socket.direction ==
                    EditorNodeGraph::SocketDirection::Input;
                RequireLayout(
                    input
                        ? socket.center.x + kSocketRadius <=
                            -kSocketBodyGap + 0.0005f
                        : socket.center.x - kSocketRadius >=
                            layout.body.max.x +
                                kSocketBodyGap - 0.0005f,
                    "all node sockets must be fully outside the body");
                RequireLayout(
                    socket.center.y >= 0.0f &&
                        socket.center.y <= layout.body.max.y,
                    "socket rows must remain vertically attached to the body");
            }
        }
    }
}

void TestProjectionIsAffineAtEveryGraphZoom() {
    NodeLayoutBuildSpec spec = MakeLayoutSpec(
        EditorNodeGraph::NodeKind::Layer,
        true,
        2,
        3,
        412.5f);
    spec.titleLines = {
        "Long Unicode title",
        "second line ..."
    };
    spec.titleEllipsized = true;
    const NodeLogicalLayout logical =
        BuildLogicalNodeLayout(spec);
    const std::uint64_t canonicalHash =
        LogicalLayoutHash(logical);
    const std::vector<std::string> canonicalTitleLines =
        logical.titleLines;

    constexpr std::array<float, 8> kZooms {
        0.16f, 0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 4.5f
    };
    const EditorNodeGraph::Vec2 position { 31.25f, -18.5f };
    const EditorNodeGraph::Vec2 origin { 420.0f, 260.0f };
    const EditorNodeGraph::Vec2 pan { 37.0f, -11.0f };

    for (const float zoom : kZooms) {
        const NodeProjectedLayout projected =
            ProjectNodeLayout(logical, position, origin, pan, zoom);
        RequireLayout(
            LogicalLayoutHash(logical) == canonicalHash &&
                logical.titleLines == canonicalTitleLines,
            "zoom must not rebuild logical layout or title wrap points");
        const auto inverse = [&](const EditorNodeGraph::Vec2& point) {
            return EditorNodeGraph::Vec2 {
                ((point.x - origin.x - pan.x) / zoom) - position.x,
                ((point.y - origin.y - pan.y) / zoom) - position.y
            };
        };
        const EditorNodeGraph::Vec2 bodyMax = inverse(projected.body.max);
        const EditorNodeGraph::Vec2 contentMin =
            inverse(projected.content.min);
        const EditorNodeGraph::Vec2 input =
            inverse(projected.sockets.front().center);
        const EditorNodeGraph::Vec2 slotMin =
            inverse(projected.controlSlots.front().rect.min);
        RequireLayout(
            NearlyEqual(bodyMax.x, logical.body.max.x) &&
                NearlyEqual(bodyMax.y, logical.body.max.y) &&
                NearlyEqual(contentMin.x, logical.content.min.x) &&
                NearlyEqual(contentMin.y, logical.content.min.y) &&
                NearlyEqual(input.x, logical.sockets.front().center.x) &&
                NearlyEqual(input.y, logical.sockets.front().center.y) &&
                NearlyEqual(
                    slotMin.x,
                    logical.controlSlots.front().rect.min.x) &&
                NearlyEqual(
                    slotMin.y,
                    logical.controlSlots.front().rect.min.y),
            "projected node geometry must inverse-project to one canonical layout");
    }
}

void TestSocketsAndBoundsRemainOutsideBody() {
    const float inputCenter = -kSocketCenterOffset;
    const float outputCenter = kStandardNodeWidth + kSocketCenterOffset;
    RequireLayout(
        inputCenter + kSocketRadius <= -kSocketBodyGap + 0.0001f,
        "input sockets must be fully detached from the node body");
    RequireLayout(
        outputCenter - kSocketRadius >=
            kStandardNodeWidth + kSocketBodyGap - 0.0001f,
        "output sockets must be fully detached from the node body");

    const NodeLogicalLayout logical = BuildLogicalNodeLayout(
        MakeLayoutSpec(
            EditorNodeGraph::NodeKind::Layer,
            false,
            1,
            1));
    const LogicalRect absolute =
        AbsoluteGraphBounds(logical, { 50.0f, 75.0f });
    RequireLayout(
        absolute.min.x < 50.0f &&
            absolute.max.x > 50.0f + kStandardNodeWidth,
        "persistent graph bounds must include detached sockets on both sides");
}

void TestDynamicContentAndAppearanceIndependence() {
    NodeLayoutBuildSpec shortSpec = MakeLayoutSpec(
        EditorNodeGraph::NodeKind::ConstantChannel,
        true,
        1,
        1,
        224.0f);
    NodeLayoutBuildSpec advancedSpec = shortSpec;
    advancedSpec.height = 468.5f;
    advancedSpec.sockets.push_back(SocketLayoutSpec {
        "advanced_input",
        EditorNodeGraph::SocketDirection::Input
    });
    const NodeLogicalLayout shortLayout =
        BuildLogicalNodeLayout(shortSpec);
    const NodeLogicalLayout advancedLayout =
        BuildLogicalNodeLayout(advancedSpec);
    RequireLayout(
        NearlyEqual(shortLayout.body.Height(), 224.0f) &&
            NearlyEqual(advancedLayout.body.Height(), 468.5f),
        "dynamic and optional content must deterministically own node height");
    RequireLayout(
        advancedLayout.sockets.size() ==
            shortLayout.sockets.size() + 1,
        "advanced socket rows must be represented in logical layout");

    const NodeLogicalLayout classic =
        BuildLogicalNodeLayout(advancedSpec);
    const NodeLogicalLayout black =
        BuildLogicalNodeLayout(advancedSpec);
    const NodeLogicalLayout spotlight =
        BuildLogicalNodeLayout(advancedSpec);
    RequireLayout(
        LogicalLayoutHash(classic) == LogicalLayoutHash(black) &&
            LogicalLayoutHash(classic) ==
                LogicalLayoutHash(spotlight),
        "appearance palettes must not participate in node geometry");
}

} // namespace

void RunNodeLayoutTests() {
    TestEveryNodeKindHasCanonicalWidth();
    TestEveryNodeKindBuildsInBothStates();
    TestProjectionIsAffineAtEveryGraphZoom();
    TestSocketsAndBoundsRemainOutsideBody();
    TestDynamicContentAndAppearanceIndependence();
    NodeLayoutBuildSpec shared;
    shared.width = 264.0f;
    shared.height = 64.0f;
    shared.expanded = true;
    shared.sharedIdentityRow = true;
    shared.sockets = { { "in", EditorNodeGraph::SocketDirection::Input },
        { "out", EditorNodeGraph::SocketDirection::Output } };
    const auto compact = BuildLogicalNodeLayout(shared);
    RequireLayout(NearlyEqual(compact.content.min.y, shared.headerInsetY),
        "a shared identity row must reclaim the separate title area");
    shared.sharedIdentityRow = false;
    shared.titleLines = { "User-assigned title" };
    const auto named = BuildLogicalNodeLayout(shared);
    RequireLayout(named.content.min.y > compact.content.min.y,
        "a renamed node must reserve space for its identity");
    RequireLayout(named.sockets.front().center.x == compact.sockets.front().center.x,
        "identity treatment must preserve socket attachment offsets");
}
