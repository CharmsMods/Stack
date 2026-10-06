#include "Editor/GraphRenderPolicy.h"
#include "Editor/UI/GraphFrameTransitionPolicy.h"
#include "Editor/GraphRenderMemory.h"
#include "Editor/UI/ViewportNavigation.h"
#include <stdexcept>
#include <cmath>

void TestGraphRenderPolicyAndNavigation() {
    auto require = [](bool value) { if (!value) throw std::runtime_error("Graph rendering policy/navigation regression"); };
    using namespace Stack::GraphRendering;
    RequestTag active {true, 4, 0, false, 1, 123, 10};
    auto latest = active;
    latest.revision = 500;
    require(MayFinishActive(active, latest));
    require(MayAdopt(active, latest, 20, 19));
    require(!MayAdopt(active, latest, 19, 20));
    require(!MayAdopt(active, latest, 20, 20));
    require(!MayAdopt(latest, active, 21, 20));
    latest.outputNodeId++;
    require(!MayFinishActive(active, latest));
    latest = active; latest.sourceIdentity++;
    require(!MayAdopt(active, latest, 21, 20));
    latest = active; latest.structureRevision++;
    require(!MayFinishActive(active, latest));
    require(!MayFinishActive({}, {}));
    require(CacheAllowance(true, 100, 50, 120) == 30);
    require(CacheAllowance(true, 10, 20, 100) == 0);
    require(CacheAllowance(false, 0, 0, UINT64_MAX) > 0);
    require(AddBytes(UINT64_MAX, 1) == UINT64_MAX);
    require(ImageBytes(100, 200, 3) == 480000);
    require(ImageBytes(100, 200, UINT64_MAX) == UINT64_MAX);

    FramePresentation previous {active, 1920, 1080, false, 1.0};
    auto next = previous; next.completedAt = 1.1;
    require(std::abs(FrameBlendDuration(previous, next, 100) - 0.06) < 0.0001);
    next.completedAt = 1.2;
    require(std::abs(FrameBlendDuration(previous, next, 200) - 0.12) < 0.0001);
    require(FrameBlendDuration(previous, next, 8) == 0);
    next.completedAt = 100;
    require(std::abs(FrameBlendDuration(previous, next, 100) - 0.06) < 0.0001);
    next.identity.outputNodeId++;
    require(FrameBlendDuration(previous, next, 100) == 0);
    next = previous; next.width++;
    require(FrameBlendDuration(previous, next, 100) == 0);
    next = previous; next.encoded = true;
    require(FrameBlendDuration(previous, next, 100) == 0);

    float zoom = 1, target = 1, panX = 0, panY = 0;
    bool animating = false, panning = false;
    ImVec2 focus, uv;
    Stack::ViewportNavigation::State state {zoom, target, panX, panY, animating, panning, focus, uv};
    Stack::ViewportNavigation::Input input;
    input.hovered = true; input.mouse = {360, 250}; input.wheel = 5; input.seconds = 1.0f / 60;
    auto origin = [](ImVec2 size) { return ImVec2((800-size.x)/2, (600-size.y)/2); };
    auto update = [&] { Stack::ViewportNavigation::Update(state, input, {800, 600}, {0, 0}, {800, 600}, origin); };
    update();
    const auto anchor = focus;
    input.wheel = 0; input.mouse = {780, 10};
    for (int i=0; i<120; ++i) update();
    require(!animating && zoom > 1);
    auto imageOrigin = origin({800*zoom, 600*zoom});
    require(std::abs(imageOrigin.x+panX+uv.x*800*zoom-anchor.x) < 0.01f);
    const float settledPan = panX;
    input.mouse = {5, 5}; update();
    require(panX == settledPan);
    input.middleClicked = input.middleDown = true; input.delta = {20, 10}; update();
    require(panning && std::abs(panX-settledPan-20) < 0.01f);
    input.middleClicked = input.middleDown = false; input.delta = {100, 100}; update();
    const float releasedPan = panX; update();
    require(!panning && panX == releasedPan);
    // Replacing the displayed texture has no navigation input and cannot move it.
    update(); require(panX == releasedPan);
    // Enlarging the panel clamps an existing pan without resetting zoom.
    Stack::ViewportNavigation::Update(state, input, {800, 600}, {0, 0}, {2400, 1800},
        [](ImVec2 size) { return ImVec2((2400-size.x)/2, (1800-size.y)/2); });
    require(panX == 0 && panY == 0 && zoom > 1);
    input.reset = true; update();
    require(zoom == 1 && target == 1 && panX == 0 && panY == 0 && !panning);
    input.reset = false; input.wheel = 1000; update();
    require(target == 12);
    input.wheel = 0; input.hovered = false; input.middleDown = false;
    zoom = target = 1; animating = false; panX = 500;
    update(); require(panX == 0);

    input = {};
    input.panConstraint = Stack::ViewportNavigation::PanConstraint::KeepImageReachable;
    input.minimumZoom = .1f;
    input.hovered = true; input.mouse = {400, 300}; input.wheel = -1000; input.seconds = 1.f / 60;
    update(); require(target == .1f);
    input.wheel = 0;
    for (int i=0; i<120; ++i) update();
    require(!animating && std::abs(zoom - .1f) < .0001f);
    input.middleClicked = input.middleDown = true; input.delta = {200, 120};
    update(); require(panX == 200 && panY == 120);
    input.middleClicked = input.middleDown = false; input.delta = {};
    update(); require(panX == 200 && panY == 120);
    // An enlarged image's corner can stay at the center, including after a
    // texture replacement. Cover-viewport constraints would snap it back.
    zoom = target = 4; panX = 1600; panY = 1200;
    update();
    imageOrigin = origin({3200, 2400});
    require(imageOrigin.x + panX == 400 && imageOrigin.y + panY == 300);
    update(); require(panX == 1600 && panY == 1200);
    input.middleClicked = input.middleDown = true; input.delta = {10000, 10000};
    update();
    require(imageOrigin.x + panX == 736 && imageOrigin.y + panY == 536);
    input.middleClicked = false; input.delta = {-10000, -10000};
    update();
    require(imageOrigin.x + panX + 3200 == 64 && imageOrigin.y + panY + 2400 == 64);
    input.middleDown = false; input.reset = true;
    update(); require(zoom == 1 && target == 1 && panX == 0 && panY == 0 && !panning);
}
