#include "Utils/ImGuiExtras.h"
#include "Editor/NodeGraph/UI/EditorNodeGraphUIVisuals.h"

#include <cstdint>
#include <imgui_internal.h>

namespace Stack::Editor::NodeGraphUIVisuals {

void DrawSocketPin(ImDrawList* draw, const ImVec2& pin, float radius,
    ImU32 baseColor, const GraphStyleTokens& tokens, bool hovered,
    const EditorNodeGraph::SocketDefinition& socket, bool connected,
    float interactionEmphasis, unsigned int imageSocketIconTexture) {
    using Type = EditorNodeGraph::SocketType;
    // Geometry follows the supplied SVGs. Use the existing anchor and visual bounds.
    const float scale = radius / 7.1f;
    const float stroke = 1.2f * scale;
    const ImVec4 base = ImGui::ColorConvertU32ToFloat4(baseColor);
    const ImU32 outline = ImGui::GetColorU32(base);
    ImGui::PushID(static_cast<int>(socket.direction)); ImGui::PushID(socket.id.c_str());
    const auto hoverKey=ImGui::GetID("##socketHover"), targetKey=ImGui::GetID("##socketTarget");
    auto* storage=ImGui::GetStateStorage();
    const float dt=std::min(ImGui::GetIO().DeltaTime,0.05f);
    const float haloStrength=ImGuiExtras::AnimateTowards(storage->GetFloat(hoverKey),hovered ? 0.65f : 0.0f,dt,20);
    const float targetStrength=ImGuiExtras::AnimateTowards(storage->GetFloat(targetKey),std::clamp(interactionEmphasis,0.0f,1.0f),dt,22);
    storage->SetFloat(hoverKey,haloStrength); storage->SetFloat(targetKey,targetStrength);
    ImGui::PopID(); ImGui::PopID();
    const float emphasis=std::max(haloStrength,targetStrength);
    if (emphasis>0.01f) {
        ImVec4 halo=tokens.enabled ? tokens.nodeAppearance.socketHalo : tokens.text;
        halo.w*=emphasis;
        draw->AddCircle(pin,radius+2.2f*scale,ImGui::GetColorU32(halo),24,
            (1.0f+targetStrength)*scale);
    }
    const ImU32 interior = ImGui::GetColorU32(tokens.canvas);
    const ImU32 ink = connected ? interior : outline;
    const auto point = [&](float x, float y) {
        return ImVec2(pin.x + (x - 9.0f) * scale, pin.y + (y - 9.0f) * scale);
    };
    const auto line = [&](float x1, float y1, float x2, float y2) {
        draw->AddLine(point(x1, y1), point(x2, y2), ink, stroke);
    };
    const auto rect = [&](float x1, float y1, float x2, float y2, float rounding) {
        draw->AddRectFilled(point(x1, y1), point(x2, y2), connected ? outline : interior, rounding * scale);
        draw->AddRect(point(x1, y1), point(x2, y2), outline, rounding * scale, 0, stroke);
    };
    const auto polygon = [&](std::initializer_list<ImVec2> points) {
        for (const auto& p : points) draw->PathLineTo(point(p.x, p.y));
        draw->PathFillConvex(connected ? outline : interior);
        for (const auto& p : points) draw->PathLineTo(point(p.x, p.y));
        draw->PathStroke(outline, ImDrawFlags_Closed, stroke);
    };
    switch (socket.type) {
        case Type::Image:
        case Type::ImageOrChannel: {
            rect(2, 3, 16, 15, 4);
            if (imageSocketIconTexture != 0) {
                draw->AddImage(
                    (ImTextureID)(intptr_t)imageSocketIconTexture,
                    point(4.1f, 4.1f),
                    point(13.9f, 13.9f),
                    ImVec2(0, 0),
                    ImVec2(1, 1),
                    ink);
            }
            break;
        }
        case Type::Mask:
            polygon({{9, 1.9f}, {16.1f, 9}, {9, 16.1f}, {1.9f, 9}});
            draw->AddQuadFilled(point(9, 6), point(12, 9), point(9, 12), point(6, 9), ink);
            break;
        case Type::Raw:
            rect(2.3f, 2.3f, 15.7f, 15.7f, 1);
            line(9, 4.5f, 9, 13.5f); line(4.5f, 9, 13.5f, 9);
            break;
        case Type::Channel:
        case Type::ScalarField:
            rect(5.5f, 2.5f, 12.5f, 15.5f, 3.5f);
            line(9, 6, 9, 12);
            break;
        case Type::Spectrum:
        case Type::FrequencyResponse:
        case Type::SpectrumMagnitude:
        case Type::SpectrumPhase:
            polygon({{5, 2.5f}, {13, 2.5f}, {16, 9}, {13, 15.5f}, {5, 15.5f}, {2, 9}});
            line(4, 9, 6, 9); line(6, 9, 7, 6); line(7, 6, 9, 12); line(9, 12, 11, 9); line(11, 9, 14, 9);
            break;
        case Type::Vector2:
        case Type::Vector3:
        case Type::Vector4:
        case Type::Coordinate:
            polygon({{9, 2}, {16, 15}, {2, 15}});
            line(9, 7, 9, 12);
            break;
        case Type::Matrix3:
        case Type::Matrix4:
            rect(2.5f, 2.5f, 15.5f, 15.5f, 2);
            line(7, 5, 7, 13); line(11, 5, 11, 13); line(5, 7, 13, 7); line(5, 11, 13, 11);
            break;
        case Type::Curve:
            rect(2.5f, 3, 15.5f, 15, 3);
            line(5, 12, 8, 10); line(8, 10, 10, 6); line(10, 6, 13, 5);
            break;
        case Type::Histogram:
        case Type::Statistics:
        case Type::Metadata:
        case Type::Handle:
        case Type::Analysis:
            polygon({{5, 3}, {13, 3}, {16, 9}, {13, 15}, {5, 15}, {2, 9}});
            draw->AddCircleFilled(point(9, 9), 1.3f * scale, ink);
            break;
        case Type::Boolean:
        case Type::Integer:
        case Type::Scalar:
        case Type::Value:
            draw->AddCircleFilled(pin, 5.0f * scale, connected ? outline : interior, 20);
            draw->AddCircle(pin, 5.0f * scale, outline, 20, stroke);
            draw->AddCircleFilled(pin, 1.25f * scale, ink, 10);
            break;
    }

}

} // namespace Stack::Editor::NodeGraphUIVisuals
