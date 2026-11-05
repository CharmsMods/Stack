#include "Editor/NodeEditors/ColorWarpFeedback.h"

#include <cmath>

namespace Stack::Editor {
namespace {
template<class T> void Hash(std::size_t& seed, const T& value) {
    seed ^= std::hash<T>{}(value) + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u);
}

template<class Values, class Read>
float Sample(const RawRecipe::RawColorWarpMaskGuide& guide, const Values& values,
    float u, float v, const Read& read) {
    const float x = std::clamp(u * guide.width - 0.5f, 0.0f, float(guide.width - 1));
    const float y = std::clamp(v * guide.height - 0.5f, 0.0f, float(guide.height - 1));
    const int x0 = int(x), y0 = int(y);
    const int x1 = std::min(x0 + 1, guide.width - 1);
    const int y1 = std::min(y0 + 1, guide.height - 1);
    const float fx = x - x0, fy = y - y0;
    const auto at = [&](int px, int py) { return read(values[std::size_t(py) * guide.width + px]); };
    return (at(x0, y0) * (1.0f - fx) + at(x1, y0) * fx) * (1.0f - fy) +
        (at(x0, y1) * (1.0f - fx) + at(x1, y1) * fx) * fy;
}
}

void ColorWarpFeedbackData::SampleMasks(const RawRecipe::RawColorWarpRecipe& recipe,
    const RawRecipe::RawColorWarpCoordinate& input, float u, float v,
    std::vector<RawRecipe::RawColorWarpMaskSample>& samples) const {
    samples.assign(recipe.pins.size(), {});
    if (guide.width <= 0 || guide.height <= 0 || guide.pixels.empty()) return;
    const float a = Sample(guide, guide.pixels, u, v, [](const auto& p) { return p.a; });
    const float b = Sample(guide, guide.pixels, u, v, [](const auto& p) { return p.b; });
    const float ev = Sample(guide, guide.pixels, u, v, [](const auto& p) { return p.sceneEv; });
    const float distance = std::hypot(input.a - a, input.b - b) * 36.0f +
        std::abs(input.sceneEv - ev) * 0.28f;
    const float guided = std::clamp(std::exp(-distance), 0.04f, 1.0f);
    const auto byte = [](std::uint8_t value) { return value / 255.0f; };
    // Guide attenuation belongs only to selections with a spatial restriction.
    const bool spatial = std::any_of(fields.begin(), fields.end(),
        [](const auto& field) { return field.spatialAvailable; });
    if (!spatial) return;
    for (std::size_t index = 0; index < fields.size() && index < samples.size(); ++index) {
        const auto& field = fields[index];
        if (!field.spatialAvailable || field.gate.empty()) continue;
        samples[index] = { Sample(guide, field.gate, u, v, byte) * guided,
            Sample(guide, field.support, u, v, byte) * guided,
            Sample(guide, field.boundary, u, v, byte) };
    }
}

void ColorWarpFeedback::Update(const RawDevelopmentGraphScopeReadback& scope,
    const std::string& targetIdentity, std::size_t inputFingerprint,
    const RawRecipe::RawColorWarpRecipe& recipe, Raw::RawWorkingSpace workingSpace,
    const std::function<bool(std::function<void()>)>& submit, std::uint64_t nodeRevision) {
    const std::size_t count = scope.width > 0 && scope.height > 0
        ? std::size_t(scope.width) * scope.height : 0u;
    if (!scope.valid || scope.stage != RawDevelopmentGraphScopeStage::ColorWarpInput ||
        count == 0 || scope.pixels.size() < count * 3u) {
        return; // Keep the last settled presentation while its replacement input is pending.
    }
    const std::size_t inputKey=scope.graphInputFingerprint?scope.graphInputFingerprint:inputFingerprint;
    if(!inputKey)return; // Analysis must have an input identity; never hash an image during drawing.
    const bool selectionChanged=!m_Valid || m_NodeRevision!=nodeRevision;
    if(selectionChanged) {
        m_SelectionKey=RawRecipe::ColorWarpSelectionFingerprint(recipe);
        m_NodeRevision=nodeRevision;
    }
    if(!m_Valid || m_InputKey!=inputKey || m_Width!=scope.width || m_Height!=scope.height ||
        m_SourceWidth!=scope.sourceWidth || m_SourceHeight!=scope.sourceHeight || m_WorkingSpace!=int(workingSpace) ||
        m_DesiredKey==0 || selectionChanged) {
        // This bounded key uses retained selection analysis, never pixel data.
        std::size_t key=m_SelectionKey;
        Hash(key,inputKey);Hash(key,int(workingSpace));
        Hash(key,scope.width);Hash(key,scope.height);Hash(key,scope.sourceWidth);Hash(key,scope.sourceHeight);
        m_DesiredKey=key?key:1;
        m_InputKey=inputKey;m_Width=scope.width;m_Height=scope.height;
        m_SourceWidth=scope.sourceWidth;m_SourceHeight=scope.sourceHeight;m_WorkingSpace=int(workingSpace);m_Valid=true;
    }
    if (m_FailedKey == m_DesiredKey) return;
    if (m_Request && m_Request->key == m_DesiredKey && !m_Request->cancelled) return;
    // One job at a time; a superseded request cancels cooperatively before
    // the latest request is submitted on a subsequent drawing frame.
    if (m_Request && !m_Request->done) {
        m_Request->cancelled = true;
        return;
    }
    m_FailedKey = m_DesiredKey;
    try {
        auto request = std::make_shared<Request>();
        request->key = m_DesiredKey;
        const bool submitted = submit([request, scope, recipe, workingSpace, count]() {
            const auto cancelled = [&]() { return request->cancelled.load(); };
            try {
                if (!cancelled()) {
                    auto data = std::make_shared<ColorWarpFeedbackData>();
                    data->selectionFingerprint = RawRecipe::ColorWarpSelectionFingerprint(recipe);
                    auto& guide = data->guide;
                    guide.width = scope.width; guide.height = scope.height;
                    guide.sourceWidth = scope.sourceWidth > 0 ? scope.sourceWidth : scope.width;
                    guide.sourceHeight = scope.sourceHeight > 0 ? scope.sourceHeight : scope.height;
                    guide.pixels.resize(count);
                    for (std::size_t index = 0; index < count; ++index) {
                        if ((index & 4095u) == 0u && cancelled()) break;
                        const auto offset = index * 3u;
                        guide.pixels[index] = RawRecipe::WorkingRgbToColorWarpCoordinate(
                            {scope.pixels[offset], scope.pixels[offset + 1u], scope.pixels[offset + 2u]},
                            workingSpace);
                    }
                    data->fields.resize(recipe.pins.size());
                    for (std::size_t index = 0; index < recipe.pins.size() && !cancelled(); ++index) {
                        const auto& pin = recipe.pins[index];
                        const auto region = std::find_if(recipe.regions.begin(), recipe.regions.end(),
                            [&](const auto& candidate) { return candidate.id == pin.regionId; });
                        if(region!=recipe.regions.end() && region->spatialMode!=RawRecipe::RawColorWarpSpatialMode::AllMatches)
                            data->fields[index]=RawRecipe::BuildRawColorWarpMaskFields(guide,pin,&*region,cancelled);
                    }
                    if (!cancelled()) {
                        std::lock_guard<std::mutex> lock(request->mutex);
                        request->data = std::move(data);
                    }
                }
            } catch (...) {
                // A failed diagnostic must not publish unrestricted membership.
                // Failure is terminal for this input; supersession alone may
                // schedule an automatic replacement on the next drawing frame.
                request->failed = true;
            }
            request->done = true;
        });
        if (submitted) {
            m_Request = std::move(request);
            m_FailedKey = 0;
        }
    } catch (...) {
        // m_FailedKey also owns allocation, input-copy and submit exceptions.
    }
}

std::shared_ptr<const ColorWarpFeedbackData> ColorWarpFeedback::Current() const {
    if (!m_Request || m_DesiredKey == 0 || m_Request->key != m_DesiredKey ||
        m_Request->cancelled || m_Request->failed || !m_Request->done) return {};
    std::lock_guard<std::mutex> lock(m_Request->mutex);
    return m_Request->data;
}

bool ColorWarpFeedback::Failed() const {
    return m_DesiredKey != 0 && (m_FailedKey == m_DesiredKey ||
        (m_Request && m_Request->key == m_DesiredKey &&
         !m_Request->cancelled && m_Request->failed));
}

void ColorWarpFeedback::RetryFailed() {
    if (!Failed()) return;
    m_FailedKey = 0;
    if (m_Request && m_Request->key == m_DesiredKey) {
        m_Request->cancelled = true;
        m_Request.reset();
    }
}

std::vector<unsigned char> BuildColorWarpFeedbackOverlay(const ColorWarpFeedbackData& data,
    const RawRecipe::RawColorWarpRecipe& recipe,
    const std::vector<RawRecipe::RawColorWarpPin>& inspectedPins) {
    std::vector<bool> inspected(recipe.pins.size(), false);
    std::vector<float> spatialSupport(recipe.pins.size(), 0.0f);
    for (std::size_t pin = 0; pin < recipe.pins.size(); ++pin) {
        inspected[pin] = std::any_of(inspectedPins.begin(), inspectedPins.end(),
            [&](const auto& candidate) { return candidate.id == recipe.pins[pin].id; });
        spatialSupport[pin] = RawRecipe::ColorWarpPinSpatialSupport(recipe, recipe.pins[pin]);
    }
    auto memberships=recipe.pins;
    for(auto& pin:memberships){pin.enabled=true;pin.strength=1;}
    const auto& guide = data.guide;
    std::vector<unsigned char> pixels(guide.pixels.size() * 4u, 0u);
    for (std::size_t index = 0; index < guide.pixels.size(); ++index) {
        float selected = 0.0f, strongest = 0.0f;
        std::array<float, 3> color {48.0f, 210.0f, 224.0f};
        for (std::size_t pinIndex = 0; pinIndex < recipe.pins.size(); ++pinIndex) {
            if(!inspected[pinIndex])continue;
            const auto& pin=memberships[pinIndex];
            const auto& field = data.fields[pinIndex];
            const RawRecipe::RawColorWarpMaskSample mask = field.gate.empty()?RawRecipe::RawColorWarpMaskSample{}:RawRecipe::RawColorWarpMaskSample{
                field.gate[index] / 255.0f, field.support[index] / 255.0f,
                field.boundary[index] / 255.0f };
            const auto& input = guide.pixels[index];
            const float weight = RawRecipe::EvaluateColorWarpPinWeight(pin, input, mask,
                spatialSupport[pinIndex]);
            if (!inspected[pinIndex]) continue;
            selected = std::max(selected,weight);
            if (weight <= strongest) continue;
            strongest = weight;
            if ((!field.edge.empty() && field.edge[index] != 0u) || mask.boundary < 0.98f)
                color = {174.0f, 104.0f, 242.0f};
            else if (mask.support * spatialSupport[pinIndex] > mask.gate)
                color = {238.0f, 160.0f, 62.0f};
            else color = {48.0f, 210.0f, 224.0f};
        }
        const float influence = std::clamp(selected,0.f,1.f);
        for (std::size_t channel = 0; channel < 3u; ++channel)
            pixels[index * 4u + channel] = static_cast<unsigned char>(std::lround(color[channel] * influence));
        pixels[index * 4u + 3u] = static_cast<unsigned char>(std::lround(205.0f + 13.0f * influence));
    }
    return pixels;
}

} // namespace Stack::Editor
