#include "Editor/NodeEditors/ColorWarpCloudPreparation.h"
#include "Editor/NodeEditors/ColorWarpFeedback.h"
#include "Editor/Internal/RawLab/RawLabColorProjection.h"
#include "Raw/PreparedColorWarp.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <tuple>
#include <type_traits>

namespace Stack::Editor {
namespace {
// Infrastructure admission, not project state. At most two cloud jobs may be
// queued/running across all node owners, using ordinary background workers.
std::atomic<unsigned> cloudJobs{0};
struct CloudJobAdmission {
    bool held = false;
    CloudJobAdmission() {
        unsigned count = cloudJobs.load(std::memory_order_relaxed);
        while (count < 2) {
            if (cloudJobs.compare_exchange_weak(count, count + 1, std::memory_order_relaxed)) {
                held = true;
                break;
            }
        }
    }
    ~CloudJobAdmission() { if (held) cloudJobs.fetch_sub(1, std::memory_order_relaxed); }
};
struct Sample { std::array<float,3> rgb; float u = 0, v = 0; };
using Samples = std::vector<Sample>;

// Adjustment values may arrive one completed job behind a moving control.
// Selection membership and interpretation may not: reject that older result.
std::size_t SelectionIdentity(const RawRecipe::RawColorWarpRecipe& recipe) {
    std::size_t seed = RawRecipe::ColorWarpSelectionFingerprint(recipe);
    const auto hash = [&](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
    };
    hash(recipe.enabled); hash(recipe.limitHslToSelections); hash(static_cast<int>(recipe.combine));
    for (const auto& pin : recipe.pins) { hash(pin.enabled); hash(pin.protectColor); }
    hash(recipe.hslSelections.size());
    for (const auto& selection : recipe.hslSelections) {
        hash(selection.id); hash(selection.enabled); hash(selection.allColors);
        hash(selection.hue); hash(selection.hueWidth); hash(selection.hueSoftness);
        hash(selection.areaEnabled); hash(selection.areaSoftness); hash(selection.influence);
        for (bool own : selection.own) hash(own);
        for (const auto* bands : {&selection.bands, &selection.excludedBands}) {
            hash(bands->size());
            for (const auto& band : *bands) { hash(band.low); hash(band.high); hash(band.softness); }
        }
        hash(selection.outline.size());
        for (const auto& point : selection.outline) { hash(point[0]); hash(point[1]); }
    }
    return seed;
}

struct Key {
    ColorWarpCloudDemand demand;
    std::size_t selection = 0;
    int width = 0, height = 0;
    bool Compatible(const Key& other) const {
        return std::tie(demand.inputKey, demand.feedbackKey, demand.workingSpace,
                   demand.selected, demand.live, demand.preview, selection, width, height) ==
            std::tie(other.demand.inputKey, other.demand.feedbackKey, other.demand.workingSpace,
                   other.demand.selected, other.demand.live, other.demand.preview,
                   other.selection, other.width, other.height);
    }
    bool operator==(const Key& other) const {
        return Compatible(other) && demand.nodeRevision == other.demand.nodeRevision &&
            demand.localRevision == other.demand.localRevision;
    }
};
struct Request {
    Key key;
    std::atomic<bool> cancelled{false}, done{false};
    bool failed = false;
    ColorWarpCloudPreparation::Vertices vertices;
};
}

struct ColorWarpCloudPreparation::State {
    std::shared_ptr<const Samples> samples;
    std::size_t inputKey = 0, selection = 0;
    Raw::RawWorkingSpace space = Raw::RawWorkingSpace::LinearSrgbD65;
    int width = 0, height = 0;
    std::uint64_t nodeRevision = ~std::uint64_t(0), localRevision = ~std::uint64_t(0);
    bool inputChanged = false, selectionCurrent = false;
    std::chrono::steady_clock::time_point retryAt{};
    std::shared_ptr<Request> request;
    std::optional<Key> accepted, failed;
};

ColorWarpCloudPreparation::ColorWarpCloudPreparation() : m_State(std::make_unique<State>()) {}
ColorWarpCloudPreparation::~ColorWarpCloudPreparation() {
    if (m_State->request) m_State->request->cancelled.store(true, std::memory_order_relaxed);
}
bool ColorWarpCloudPreparation::InputChanged() const { return m_State->inputChanged; }
bool ColorWarpCloudPreparation::SelectionCurrent() const { return m_State->selectionCurrent; }

std::optional<ColorWarpCloudPreparation::Vertices> ColorWarpCloudPreparation::Update(
    const RawDevelopmentGraphScopeReadback& scope, const RawRecipe::RawColorWarpRecipe& recipe,
    std::shared_ptr<const ColorWarpFeedbackData> feedback,
    const ColorWarpCloudDemand& demand, const Submit& submit) {
    auto& state = *m_State;
    state.inputChanged = false;
    std::optional<Vertices> ready;
    try {
        const bool valid = scope.valid && scope.stage == RawDevelopmentGraphScopeStage::ColorWarpInput &&
            demand.inputKey && scope.width > 0 && scope.height > 0 &&
            scope.pixels.size() / 3u >= std::size_t(scope.width) * scope.height;
        if (valid && (!state.samples || state.inputKey != demand.inputKey || state.space != demand.workingSpace ||
                state.width != scope.width || state.height != scope.height)) {
            auto samples = std::make_shared<Samples>();
            const int width = std::min(64, scope.width), height = std::min(64, scope.height);
            samples->reserve(std::size_t(width) * height);
            // Capture only the displayed sample grid, never the full readback.
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const int sx = std::min(scope.width - 1, int((x + .5f) * scope.width / width));
                const int sy = std::min(scope.height - 1, int((y + .5f) * scope.height / height));
                const auto at = (std::size_t(sy) * scope.width + sx) * 3u;
                samples->push_back({{scope.pixels[at], scope.pixels[at+1], scope.pixels[at+2]},
                    (sx + .5f) / scope.width, (sy + .5f) / scope.height});
            }
            state.samples = std::move(samples); state.inputKey = demand.inputKey; state.space = demand.workingSpace;
            state.width = scope.width; state.height = scope.height; state.inputChanged = true;
            state.accepted.reset(); state.failed.reset();
        }
        if (!state.samples) return ready;
        if (state.nodeRevision != demand.nodeRevision || state.localRevision != demand.localRevision) {
            state.selection = SelectionIdentity(recipe);
            state.nodeRevision = demand.nodeRevision; state.localRevision = demand.localRevision;
        }
        Key key{demand, state.selection, state.width, state.height};
        // While fresh analysis is pending, retained samples keep their actual
        // source identity; a parameter edit never relabels them as new input.
        key.demand.inputKey = state.inputKey; key.demand.workingSpace = state.space;
        state.selectionCurrent = state.accepted && state.accepted->Compatible(key);
        if (state.request && !state.request->key.Compatible(key))
            state.request->cancelled.store(true, std::memory_order_relaxed);
        if (state.request && state.request->done.load(std::memory_order_acquire)) {
            auto completed = std::move(state.request);
            if (!completed->cancelled.load(std::memory_order_relaxed) && completed->key.Compatible(key)) {
                if (completed->failed) state.failed = completed->key;
                else {
                    state.accepted = completed->key;
                    state.selectionCurrent = true;
                    ready = std::move(completed->vertices);
                }
            }
        }
        if (state.request || (state.accepted && *state.accepted == key) ||
            (state.failed && *state.failed == key) || !submit ||
            std::chrono::steady_clock::now() < state.retryAt) return ready;
        auto admission = std::make_shared<CloudJobAdmission>();
        if (!admission->held) return ready;
        auto request = std::make_shared<Request>();
        request->key = key;
        const auto samples = state.samples;
        const bool submitted = submit([request, samples, recipe, feedback = std::move(feedback), admission]() {
            const auto cancelled = [&] { return request->cancelled.load(std::memory_order_relaxed); };
            try {
                if (!cancelled()) {
                    const auto prepared = RawRecipe::PrepareColorWarpEvaluation(recipe);
                    const auto& d = request->key.demand;
                    const bool spatial = RawRecipe::UsesColorWarpSpatialMasks(recipe);
                    const auto* pin = d.selected >= 0 && d.selected < int(recipe.pins.size()) ? &recipe.pins[d.selected] : nullptr;
                    const auto region = pin ? std::find_if(recipe.regions.begin(), recipe.regions.end(),
                        [&](const auto& candidate) { return candidate.id == pin->regionId; }) : recipe.regions.end();
                    const bool selectedSpatial = (pin && !RawRecipe::UsesColorWarpHsl(recipe, *pin) && pin->smoothSelection > 0) ||
                        (region != recipe.regions.end() && region->spatialMode != RawRecipe::RawColorWarpSpatialMode::AllMatches);
                    std::vector<RawRecipe::RawColorWarpMaskSample> masks;
                    request->vertices.reserve(samples->size());
                    for (const auto& sample : *samples) {
                        if (cancelled()) break;
                        const auto input = RawRecipe::WorkingRgbToColorWarpCoordinate(sample.rgb, d.workingSpace);
                        if (!std::isfinite(input.lightness) || !std::isfinite(input.a) || !std::isfinite(input.b)) continue;
                        if (feedback) feedback->SampleMasks(recipe, input, sample.u, sample.v, masks);
                        float caught = 0;
                        if (d.preview && pin && ((!spatial && !selectedSpatial) || feedback))
                            caught = RawRecipe::ColorWarpPreviewInfluence(recipe, std::size_t(d.selected), input, masks);
                        auto rgb = sample.rgb; auto coordinate = input;
                        if (d.live && (!spatial || feedback)) {
                            rgb = RawRecipe::ApplyPreparedColorWarp(prepared, rgb, d.workingSpace,
                                spatial && feedback ? &masks : nullptr, {sample.u, sample.v});
                            coordinate = RawRecipe::WorkingRgbToColorWarpCoordinate(rgb, d.workingSpace);
                        }
                        const auto normalized = RawRecipe::ColorWarpSelectionCoordinate(coordinate);
                        const auto projected = RawLabInternal::ProjectColorDisc(normalized[0] * .7f, normalized[1] * .7f);
                        const auto display = RawLabInternal::ColorDiscDisplayRgb(rgb, d.workingSpace);
                        if (std::isfinite(projected.x) && std::isfinite(projected.y))
                            request->vertices.push_back({projected.x, projected.y, display[0], display[1], display[2], coordinate.sceneEv, caught});
                    }
                }
            } catch (...) { request->failed = true; }
            request->done.store(true, std::memory_order_release);
        });
        if (submitted) state.request = std::move(request);
        else state.retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    } catch (...) {
        // Optional visualization preserves the last accepted GPU buffer.
        state.retryAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
    }
    return ready;
}
}
