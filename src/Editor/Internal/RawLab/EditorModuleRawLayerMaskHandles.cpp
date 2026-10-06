#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

void EditorModule::RenderRawLayerMaskHandles(const RawWorkspaceEditContext& context,
    const ImVec2& minimum, const ImVec2& maximum) {
    using namespace Stack::Project;
    using Kind = EditorNodeGraph::MaskGeneratorKind;
    if (!context.canEdit || !m_EditingRawLayerMask) return;
    const auto reference = *m_EditingRawLayerMask;
    const auto* owner = FindRawAdjustmentLayer(m_Project->rawLayers.State(), reference.layerId);
    const auto* output = FindRawMaskOutput(m_Project->rawLayers.State(), reference);
    if (!owner || !output) return;
    const ImVec2 size(maximum.x - minimum.x, maximum.y - minimum.y);
    if (size.x <= 1 || size.y <= 1) return;

    // Follow the selected result upstream. Processing a generator through more
    // nodes does not replace that generator or its viewport handles.
    const auto* producer = FindRawPublishedNode(*owner, *output);
    if (!producer) return;
    std::vector<int> pending{producer->id}, generators;
    std::unordered_set<int> visited;
    while (!pending.empty()) {
        const int id = pending.back();
        pending.pop_back();
        if (!visited.insert(id).second) continue;
        const auto* node = owner->graph.FindNode(id);
        if (!node) continue;
        if (node->kind == EditorNodeGraph::NodeKind::MaskGenerator &&
            (node->maskKind == Kind::RadialGradient || node->maskKind == Kind::LinearGradient || node->maskKind == Kind::Square))
            generators.push_back(id);
        for (const auto& link : owner->graph.GetLinks())
            if (link.toNodeId == id) pending.push_back(link.fromNodeId);
    }
    if (generators.empty()) return;
    if (std::find(generators.begin(), generators.end(), m_RawLayerMaskGenerator) == generators.end())
        m_RawLayerMaskGenerator = generators.front();

    const ImVec2 previousCursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(minimum.x + 12, minimum.y + 12));
    ImGui::PushID("RawLayerMaskShape");
    const auto* generator = owner->graph.FindNode(m_RawLayerMaskGenerator);
    ImGui::SetNextItemWidth(190);
    if (generators.size() > 1 && ImGui::BeginCombo("##generator", generator->title.c_str())) {
        for (int id : generators) {
            const auto* choice = owner->graph.FindNode(id);
            ImGui::PushID(id);
            if (ImGui::Selectable(choice->title.c_str(), id == m_RawLayerMaskGenerator))
                m_RawLayerMaskGenerator = id;
            ImGui::PopID();
        }
        ImGui::EndCombo();
        generator = owner->graph.FindNode(m_RawLayerMaskGenerator);
    }
    if (ImGui::SmallButton("Done editing mask")) {
        m_EditingRawLayerMask.reset();
        m_RawLayerMaskDrag = -1;
        m_Project->rawLayers.EndGesture();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(previousCursor);
        return;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(previousCursor);

    const auto settings = generator->maskSettings;
    const auto kind = generator->maskKind;
    constexpr float pi = 3.14159265358979323846f;
    const float angle = settings.angle * pi / 180.0f;
    const float cs = std::cos(angle), sn = std::sin(angle);
    if (m_GraphOutputDescriptionRenderRevision != m_RenderRevision) BuildGraphSnapshot();
    const auto descriptor = m_GraphOutputDescriptions.find("raw/"+owner->id+"/"+
        EditorNodeGraph::GraphOutputIdentity(generator->id,"maskOut"));
    using Mapping = Stack::Editor::RawLabInternal::RawLabImageMapping;
    const auto mapping = descriptor != m_GraphOutputDescriptions.end() &&
        descriptor->second.descriptor.spatial.state == Stack::NodeMath::KnowledgeState::Known &&
        m_LastGraphOutputSemanticDescriptor.spatial.state == Stack::NodeMath::KnowledgeState::Known
        ? Mapping::FromSpatial(descriptor->second.descriptor.spatial.value,
            m_LastGraphOutputSemanticDescriptor.spatial.value,context.recipe.cropRotation) : std::nullopt;
    if (!mapping) {
        ImGui::GetWindowDrawList()->AddText({minimum.x+12,minimum.y+42},IM_COL32(230,230,230,255),
            "The mask generator's image coordinates are unavailable.");
        return;
    }
    const auto screen = [&](float u, float v) {
        const auto point=mapping->Canvas({u,1-v});
        return ImVec2(minimum.x+point.x*size.x,minimum.y+point.y*size.y);
    };
    const auto generatorPoint = [&](ImVec2 point) {
        const auto local=mapping->Local({(point.x-minimum.x)/size.x,(point.y-minimum.y)/size.y});
        return ImVec2(local.x,1-local.y);
    };
    const auto distance = [](ImVec2 a, ImVec2 b) { return std::hypot(a.x - b.x, a.y - b.y); };
    std::array<ImVec2, 5> handles;
    int count = 5;
    const float scale = std::max(settings.scale, .001f);
    float centerU = settings.centerX, centerV = settings.centerY;
    if (kind == Kind::LinearGradient) {
        centerU = .5f - cs * settings.offset / scale;
        centerV = .5f - sn * settings.offset / scale;
        handles[0] = screen(centerU, centerV);
        handles[1] = screen(centerU - cs * .5f / scale, centerV - sn * .5f / scale);
        handles[2] = screen(centerU + cs * .5f / scale, centerV + sn * .5f / scale);
        count = 3;
    } else {
        handles[0] = screen(centerU, centerV);
        handles[1] = screen(centerU + cs * settings.radius, centerV + sn * settings.radius);
        handles[2] = screen(centerU - sn * settings.radiusY, centerV + cs * settings.radiusY);
        handles[3] = screen(centerU + cs * (settings.radius + .08f), centerV + sn * (settings.radius + .08f));
        handles[4] = screen(centerU + cs * (settings.radius + settings.feather), centerV + sn * (settings.radius + settings.feather));
    }
    auto* draw = ImGui::GetWindowDrawList();
    draw->PushClipRect(minimum, maximum, true);
    const auto color = IM_COL32(255, 230, 145, 230);
    if (kind == Kind::LinearGradient) {
        draw->AddLine(handles[1], handles[2], color, 2);
        for (int i = 1; i <= 2; ++i) {
            const float d=(i==1 ? -.5f : .5f)/scale;
            const auto a=screen(centerU+cs*d-sn*.12f,centerV+sn*d+cs*.12f);
            const auto b=screen(centerU+cs*d+sn*.12f,centerV+sn*d-cs*.12f);
            draw->AddLine(a,b,color);
        }
    } else {
        std::vector<ImVec2> outline;
        for (int i = 0; i < 64; ++i) {
            const float t = 2 * pi * float(i) / 64;
            float x = std::cos(t), y = std::sin(t);
            if (kind == Kind::Square) {
                const float factor = std::max(std::abs(x), std::abs(y));
                x /= factor; y /= factor;
            }
            x *= settings.radius;
            y *= settings.radiusY;
            outline.push_back(screen(centerU + cs*x-sn*y, centerV+sn*x+cs*y));
        }
        draw->AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, 1.5f);
        draw->AddLine(handles[0], handles[3], color);
    }
    for (int i = 0; i < count; ++i) {
        draw->AddCircleFilled(handles[i], 5, IM_COL32(25, 25, 25, 230));
        draw->AddCircle(handles[i], 5, color, 0, 1.5f);
    }
    draw->PopClipRect();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (m_Project->rawLayers.CancelGesture()) { MarkDirty(); MarkRenderRefreshDirty(); }
        m_RawLayerMaskDrag = -1;
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if (ImGui::IsWindowHovered() && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        int nearest = -1;
        float best = 12;
        for (int i = 0; i < count; ++i) {
            const float d = distance(handles[i], mouse);
            if (d < best) { nearest = i; best = d; }
        }
        if (nearest >= 0) {
            m_RawLayerMaskDrag = nearest;
            m_RawLayerMaskDragStart = mouse;
            m_RawLayerMaskDragOriginal = settings;
            m_Project->rawLayers.BeginGesture();
        }
    }
    if (m_RawLayerMaskDrag >= 0 && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        distance(mouse, m_RawLayerMaskDragStart) > 0) {
        auto candidate = m_Project->rawLayers.State();
        auto& changed = FindRawAdjustmentLayer(candidate, reference.layerId)->graph.FindNode(m_RawLayerMaskGenerator)->maskSettings;
        changed = m_RawLayerMaskDragOriginal;
        const auto& original = m_RawLayerMaskDragOriginal;
        const float a = original.angle*pi/180;
        const float c = std::cos(a), s = std::sin(a);
        const auto point = generatorPoint(mouse), start = generatorPoint(m_RawLayerMaskDragStart);
        const float dx = point.x-start.x, dy = point.y-start.y;
        if (kind == Kind::LinearGradient) {
            if (m_RawLayerMaskDrag == 0) changed.offset -= (dx*c+dy*s)*original.scale;
            else {
                const float sign = m_RawLayerMaskDrag == 1 ? -1.0f : 1.0f;
                const float cx = .5f-c*original.offset/original.scale;
                const float cy = .5f-s*original.offset/original.scale;
                const float ux = (point.x-cx)*sign;
                const float uy = (point.y-cy)*sign;
                changed.angle = std::atan2(uy,ux)*180/pi;
                changed.scale = std::clamp(.5f/std::max(.001f,std::hypot(ux,uy)),.1f,100.0f);
                changed.offset = -((cx-.5f)*std::cos(changed.angle*pi/180)+
                    (cy-.5f)*std::sin(changed.angle*pi/180))*changed.scale;
            }
        } else if (m_RawLayerMaskDrag == 0) {
            changed.centerX = std::clamp(original.centerX+dx,-2.0f,3.0f);
            changed.centerY = std::clamp(original.centerY+dy,-2.0f,3.0f);
        } else if (m_RawLayerMaskDrag == 1) changed.radius = std::clamp(original.radius+dx*c+dy*s,.001f,4.0f);
        else if (m_RawLayerMaskDrag == 2) changed.radiusY = std::clamp(original.radiusY-dx*s+dy*c,.001f,4.0f);
        else if (m_RawLayerMaskDrag == 3) changed.angle = std::atan2(
            point.y-original.centerY,
            point.x-original.centerX)*180/pi;
        else changed.feather = std::clamp(original.feather+dx*c+dy*s,.0001f,2.0f);
        if (changed.centerX != settings.centerX || changed.centerY != settings.centerY ||
            changed.radius != settings.radius || changed.radiusY != settings.radiusY ||
            changed.angle != settings.angle || changed.feather != settings.feather ||
            changed.offset != settings.offset || changed.scale != settings.scale)
            ApplyRawLayerStackEdit(std::move(candidate));
    }
    if (m_RawLayerMaskDrag >= 0 && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_RawLayerMaskDrag = -1;
        m_Project->rawLayers.EndGesture();
    }
}
