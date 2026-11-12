#include "Editor/EditorModule.h"
#include "Editor/Internal/RawLab/RawLabImageMapping.h"
#include "Editor/Internal/RawLab/RawLayerMaskPlacement.h"
#include "Editor/Internal/RawLab/RawLabUiSupport.h"

#include "Utils/ImGuiExtras.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_set>

void EditorModule::RenderRawLayerMaskHandles(const RawWorkspaceEditContext& context,
    const ImVec2& minimum, const ImVec2& maximum) {
    if (IsRawImageNavigationActive()) return;
    using namespace Stack::Project;
    using Kind = EditorNodeGraph::MaskGeneratorKind;
    if (!context.canEdit || !m_EditingRawLayerMask) return;
    const auto reference = *m_EditingRawLayerMask;
    const auto* owner = FindRawAdjustmentLayer(m_Project->rawLayers.State(), reference.layerId);
    const auto* output = FindRawMaskOutput(m_Project->rawLayers.State(), reference);
    if (!owner || !output) return;
    const ImVec2 size(maximum.x - minimum.x, maximum.y - minimum.y);
    if (size.x <= 1 || size.y <= 1) return;
    const float uiScale = std::max(.75f, ImGui::GetFontSize() / 16.f);

    // Follow the selected result upstream. Processing a generator through more
    // nodes does not replace that generator or its viewport handles.
    const auto* producer = FindRawPublishedNode(*owner, *output);
    if (!producer) return;
    auto& cache = m_RawMaskHandleGenerators;
    if (cache.documentId != GetProjectDocumentId() || cache.layerId != owner->id ||
        cache.revision != m_Project->rawLayers.Revision() || cache.producer != producer->id) {
        cache.documentId = GetProjectDocumentId();
        cache.layerId = owner->id;
        cache.revision = m_Project->rawLayers.Revision();
        cache.producer = producer->id;
        cache.generators.clear();
        std::vector<int> pending{producer->id};
        std::unordered_set<int> visited;
        while (!pending.empty()) {
            const int id = pending.back();
            pending.pop_back();
            if (!visited.insert(id).second) continue;
            const auto* node = owner->graph.FindNode(id);
            if (!node) continue;
            if ((node->kind == EditorNodeGraph::NodeKind::MaskGenerator || node->kind == EditorNodeGraph::NodeKind::MaskTexture) &&
                (node->maskKind == Kind::RadialGradient || node->maskKind == Kind::LinearGradient || node->maskKind == Kind::Square))
                cache.generators.push_back(id);
            for (const auto& link : owner->graph.GetLinks())
                // The sizing image can contain earlier masked edits; those shapes
                // do not belong to this coverage branch.
                if (link.toNodeId == id && link.toSocketId != "matchExtent") pending.push_back(link.fromNodeId);
        }
    }
    const auto& generators = cache.generators;
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
            if (ImGui::Selectable(choice->title.c_str(), id == m_RawLayerMaskGenerator)) {
                m_Project->rawLayers.EndGesture();
                m_RawLayerMaskDrag = -1;
                m_RawLayerMaskGenerator = id;
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
        generator = owner->graph.FindNode(m_RawLayerMaskGenerator);
    }
    if (Stack::Editor::RawLabInternal::BareTextButton("Done")) {
        m_EditingRawLayerMask.reset();
        m_RawLayerMaskDrag = -1;
        m_Project->rawLayers.EndGesture();
        ImGui::PopID();
        ImGui::SetCursorScreenPos(previousCursor);
        return;
    }
    ImGui::SameLine();
    if(Stack::Editor::RawLabInternal::BareTextButton("Invert",generator->maskSettings.invert)) {
        auto candidate=m_Project->rawLayers.State();
        auto* edited=FindRawAdjustmentLayer(candidate,reference.layerId)->graph.FindNode(m_RawLayerMaskGenerator);
        edited->maskSettings.invert=!edited->maskSettings.invert;
        ApplyRawLayerStackEdit(std::move(candidate));
        ImGui::PopID();ImGui::SetCursorScreenPos(previousCursor);return;
    }
    ImGui::PopID();
    ImGui::SetCursorScreenPos(previousCursor);

    auto settings = generator->maskSettings;
    const auto kind = generator->maskKind;
    constexpr float pi = 3.14159265358979323846f;
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
    using namespace Stack::Editor::RawLabInternal;
    std::array<ImVec2, 7> handles;
    int count = 7;
    float scale = 1, centerU = .5f, centerV = .5f, cs = 1, sn = 0;
    const auto updateHandles = [&] {
        const float angle = settings.angle * pi / 180.0f;
        cs = std::cos(angle); sn = std::sin(angle);
        scale = std::max(settings.scale, .001f);
        centerU = settings.centerX; centerV = settings.centerY;
        if (kind == Kind::LinearGradient) {
            const float correction = (centerU-.5f)*cs + (centerV-.5f)*sn + settings.offset/scale;
            centerU -= cs*correction;
            centerV -= sn*correction;
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
            handles[5]=screen(centerU-cs*settings.radius,centerV-sn*settings.radius);
            handles[6]=screen(centerU+sn*settings.radiusY,centerV-cs*settings.radiusY);
        }
    };
    updateHandles();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (m_Project->rawLayers.CancelGesture()) { MarkDirty(); MarkRenderRefreshDirty(); }
        m_RawLayerMaskDrag = -1;
        return;
    }
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    if(m_RawLayerMaskDrag==MaskPlacementDrag && ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::GetIO().AppFocusLost) {
        ImGuiExtras::CursorCaptureRequest request;
        request.mode=ImGuiExtras::CursorCaptureMode::LockedPan;
        request.anchorScreenPos=m_RawLayerMaskDragStart;
        request.restoreScreenPos=m_RawLayerMaskDragStart;
        ImGuiExtras::SubmitCursorCaptureRequest(request);
    }
    const auto* canvasDraw = ImGui::GetWindowDrawList();
    const bool hovered = ImGui::IsWindowHovered() && !RawFloatingSurfaceOwnsPointer() &&
        ImRect(canvasDraw->GetClipRectMin(), canvasDraw->GetClipRectMax()).Contains(mouse);
    const bool inImage = ImRect(minimum,maximum).Contains(mouse) && hovered;
    if (m_RawLayerMaskDrag == MaskAwaitingPlacement && inImage)
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    if (hovered && !ImGui::IsAnyItemActive() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        int nearest = -1;
        float best = 12.f * uiScale;
        if (m_RawLayerMaskDrag == MaskAwaitingPlacement) {
            if (inImage) nearest = MaskPlacementDrag;
        } else for (int i = 0; i < count; ++i) {
            const float d = distance(handles[i], mouse);
            if (d < best) { nearest = i; best = d; }
        }
        if (nearest < 0 && m_RawLayerMaskDrag != MaskAwaitingPlacement) {
            const auto point = generatorPoint(mouse);
            const float x = (point.x-centerU)*cs+(point.y-centerV)*sn;
            const float y = -(point.x-centerU)*sn+(point.y-centerV)*cs;
            const float tolerance = 8.f / std::max(size.x, size.y);
            if (kind == Kind::LinearGradient) {
                if (std::abs(x+.5f/scale) < tolerance) nearest = 1;
                else if (std::abs(x-.5f/scale) < tolerance) nearest = 2;
                else if (std::abs(x) < tolerance) nearest = 7;
            } else if (kind == Kind::RadialGradient) {
                const float radius = std::hypot(x/std::max(.001f,settings.radius),y/std::max(.001f,settings.radiusY));
                if (radius > 1.f && radius < 1.f+tolerance/std::max(.001f,std::min(settings.radius,settings.radiusY))) nearest = 3;
            }
        }
        if (nearest >= 0) {
            m_RawLayerMaskDrag = nearest;
            m_RawLayerMaskDragStart = mouse;
            m_RawLayerMaskDragOriginal = settings;
            m_Project->rawLayers.BeginGesture();
        }
    }
    if (m_RawLayerMaskDrag >= 0 &&
        (ImGui::IsMouseDown(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left)) &&
        (m_RawLayerMaskDrag == MaskPlacementDrag || distance(mouse, m_RawLayerMaskDragStart) > 0)) {
        auto candidate = m_Project->rawLayers.State();
        auto& changed = FindRawAdjustmentLayer(candidate, reference.layerId)->graph.FindNode(m_RawLayerMaskGenerator)->maskSettings;
        changed = m_RawLayerMaskDragOriginal;
        const auto& original = m_RawLayerMaskDragOriginal;
        const float a = original.angle*pi/180;
        const float c = std::cos(a), s = std::sin(a);
        const float originalScale=std::max(.001f,original.scale);
        const float correction=(original.centerX-.5f)*c+(original.centerY-.5f)*s+original.offset/originalScale;
        const float originalCenterX=original.centerX-c*correction;
        const float originalCenterY=original.centerY-s*correction;
        const auto point = generatorPoint(mouse), start = generatorPoint(m_RawLayerMaskDragStart);
        const float dx = point.x-start.x, dy = point.y-start.y;
        if (m_RawLayerMaskDrag == MaskPlacementDrag) {
            ImVec2 anchor = start;
            if (ImGui::GetIO().KeyAlt) {
                anchor = kind == Kind::LinearGradient
                    ? ImVec2(2*start.x-point.x,2*start.y-point.y)
                    : ImVec2((start.x+point.x)*.5f,(start.y+point.y)*.5f);
            }
            PlaceLayerMask(changed,kind,anchor,point);
        }
        else if (kind == Kind::LinearGradient) {
            if (m_RawLayerMaskDrag == 0) {
                changed.centerX += dx;
                changed.centerY += dy;
                changed.offset -= (dx*c+dy*s)*original.scale;
            }
            else if (m_RawLayerMaskDrag == 7) {
                // A bounded tangential drag has no angular singularity at the
                // center. Near-center drags become progressively less sensitive.
                const float sx=start.x-originalCenterX, sy=start.y-originalCenterY;
                const float radius=std::hypot(sx,sy);
                const float reference=std::max(.12f,.5f/std::max(.1f,original.scale));
                const float denominator=std::max(radius*radius,reference*reference);
                changed.angle=original.angle+.25f*(sx*dy-sy*dx)/denominator*180/pi;
                const float angle = changed.angle*pi/180;
                changed.offset = -((originalCenterX-.5f)*std::cos(angle)+(originalCenterY-.5f)*std::sin(angle))*changed.scale;
            }
            else {
                const int fixedEnd = m_RawLayerMaskDrag == 1 ? 2 : 1;
                const float sign=fixedEnd==1?-1.f:1.f;
                const ImVec2 fixed(originalCenterX+sign*c*.5f/originalScale,originalCenterY+sign*s*.5f/originalScale);
                // Scale along the original axis; damp the tangential component
                // so resizing a short gradient does not whip its rotation.
                const float along=dx*c+dy*s, across=-dx*s+dy*c;
                const float radius=std::hypot(start.x-originalCenterX,start.y-originalCenterY);
                const float gain=.25f*std::clamp(radius/.15f,0.f,1.f);
                const float movingSign=-sign;
                const float span=std::max(.01f,1.f/originalScale+movingSign*along);
                const float sideways=across*gain*std::min(1.f,span*originalScale);
                const ImVec2 gentle(fixed.x+movingSign*c*span-s*sideways,
                    fixed.y+movingSign*s*span+c*sideways);
                if (fixedEnd == 1) PlaceLayerMask(changed, kind, fixed, gentle);
                else PlaceLayerMask(changed, kind, gentle, fixed);
            }
        } else if (m_RawLayerMaskDrag == 0) {
            changed.centerX = std::clamp(original.centerX+dx,-2.0f,3.0f);
            changed.centerY = std::clamp(original.centerY+dy,-2.0f,3.0f);
        } else if (m_RawLayerMaskDrag == 1) changed.radius = std::clamp(original.radius+dx*c+dy*s,.001f,4.0f);
        else if (m_RawLayerMaskDrag == 2) changed.radiusY = std::clamp(original.radiusY-dx*s+dy*c,.001f,4.0f);
        else if (m_RawLayerMaskDrag == 3) changed.angle = original.angle +
            (std::atan2(point.y-original.centerY,point.x-original.centerX) -
             std::atan2(start.y-original.centerY,start.x-original.centerX))*180/pi;
        else if(m_RawLayerMaskDrag==5)changed.radius=std::clamp(original.radius-dx*c-dy*s,.001f,4.f);
        else if(m_RawLayerMaskDrag==6)changed.radiusY=std::clamp(original.radiusY+dx*s-dy*c,.001f,4.f);
        else changed.feather = std::clamp(original.feather+dx*c+dy*s,.0001f,2.0f);
        if (ImGui::GetIO().KeyShift) {
            if (m_RawLayerMaskDrag == 0) {
                if (std::abs(dx*mapping->sourceAspect) > std::abs(dy)) changed.centerY = original.centerY;
                else changed.centerX = original.centerX;
                if (kind == Kind::LinearGradient)
                    changed.offset = original.offset - ((changed.centerX-original.centerX)*c+
                        (changed.centerY-original.centerY)*s)*original.scale;
            }
            else if (kind == Kind::LinearGradient || m_RawLayerMaskDrag == 3)
            {
                changed.angle = std::round(changed.angle / 15.f) * 15.f;
                if (kind == Kind::LinearGradient) {
                    const float angle = changed.angle*pi/180;
                    changed.offset = -((changed.centerX-.5f)*std::cos(angle)+
                        (changed.centerY-.5f)*std::sin(angle))*changed.scale;
                }
            }
            else if (m_RawLayerMaskDrag == MaskPlacementDrag)
                changed.radiusY = changed.radius * mapping->sourceAspect;
            else if (m_RawLayerMaskDrag == 1 || m_RawLayerMaskDrag == 5)
                changed.radiusY = changed.radius * original.radiusY / std::max(original.radius, .001f);
            else if (m_RawLayerMaskDrag == 2 || m_RawLayerMaskDrag == 6)
                changed.radius = changed.radiusY * original.radius / std::max(original.radiusY, .001f);
        }
        if (changed.centerX != settings.centerX || changed.centerY != settings.centerY ||
            changed.radius != settings.radius || changed.radiusY != settings.radiusY ||
            changed.angle != settings.angle || changed.feather != settings.feather ||
            changed.offset != settings.offset || changed.scale != settings.scale) {
            const auto updated = changed;
            if (ApplyRawLayerStackEdit(std::move(candidate))) {
                settings = updated;
                updateHandles();
            }
        }
    }
    // Input suppression or focus loss can consume the release event. End the
    // gesture when the button is no longer held so another control cannot
    // inherit the shape drag or its undo group.
    if (m_RawLayerMaskDrag >= 0 && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        m_RawLayerMaskDrag = -1;
        m_Project->rawLayers.EndGesture();
    }

    // Paint after applying the gesture so the outline follows this frame's
    // pointer position even while its GPU image result is still pending.
    auto* draw = ImGui::GetWindowDrawList();
    // Retain the host View clip so handles remain usable beyond the photo.
    const auto color = IM_COL32(255, 255, 255, 245);
    const auto outlineColor = IM_COL32(12, 12, 12, 235);
    if (m_RawLayerMaskDrag == MaskAwaitingPlacement)
        draw->AddText({minimum.x+12,minimum.y+42},color,"Drag on the image to place this mask.");
    if (kind == Kind::LinearGradient) {

        for (int i = 0; i < 3; ++i) {
            const float d=(i==0 ? 0.f : (i==1 ? -.5f : .5f))/scale;
            const auto a=screen(centerU+cs*d-sn*4.f,centerV+sn*d+cs*4.f);
            const auto b=screen(centerU+cs*d+sn*4.f,centerV+sn*d-cs*4.f);
            draw->AddLine(a,b,outlineColor,3.5f * uiScale);
            draw->AddLine(a,b,color,1.5f * uiScale);
        }
    } else {
        std::array<ImVec2,64> outline;
        for (int i = 0; i < 64; ++i) {
            const float t = 2 * pi * float(i) / 64;
            float x = std::cos(t), y = std::sin(t);
            if (kind == Kind::Square) {
                const float factor = std::max(std::abs(x), std::abs(y));
                x /= factor; y /= factor;
            }
            x *= settings.radius;
            y *= settings.radiusY;
            outline[i]=screen(centerU + cs*x-sn*y, centerV+sn*x+cs*y);
        }
        draw->AddPolyline(outline.data(), static_cast<int>(outline.size()), outlineColor, ImDrawFlags_Closed, 3.5f * uiScale);
        draw->AddPolyline(outline.data(), static_cast<int>(outline.size()), color, ImDrawFlags_Closed, 1.5f * uiScale);
        if(kind==Kind::RadialGradient)for(float boundary:{std::max(0.f,1.f-settings.feather/std::max(.001f,settings.radius)),1.f+settings.feather/std::max(.001f,settings.radius)}) {
            for(int i=0;i<64;++i){const float t=2*pi*i/64;const float x=std::cos(t)*settings.radius*boundary,y=std::sin(t)*settings.radiusY*boundary;
                outline[i]=screen(centerU+cs*x-sn*y,centerV+sn*x+cs*y);}
            draw->AddPolyline(outline.data(),64,IM_COL32(12,12,12,190),ImDrawFlags_Closed,3.f * uiScale);
            draw->AddPolyline(outline.data(),64,IM_COL32(255,255,255,190),ImDrawFlags_Closed,1.f * uiScale);
        }

    }
    for (int i = 0; i < count; ++i) {
        const bool active = m_RawLayerMaskDrag == i;
        const bool hot = hovered && distance(handles[i], mouse) < 12.f * uiScale;
        const float radius = (active || hot ? 7.f : 6.f) * uiScale;
        draw->AddCircleFilled(handles[i], radius + 2.f * uiScale, outlineColor);
        draw->AddCircleFilled(handles[i], radius, color);
        if (active || hot)
            draw->AddCircleFilled(handles[i], 3.f * uiScale, ImGui::GetColorU32(ImGuiCol_CheckMark));
    }
}
