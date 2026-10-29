#include "RawLabGalleryFolderCards.h"
#include "App/AppHeaderStyle.h"
#include "Library/LibraryControls.h"

#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Stack::Editor::RawLabInternal {
namespace {
using Texture = GalleryFolderCards::Texture;

// Filesystem paths are literal identities; ### in a legal name is not an
// ImGui label-ID override. Retain the current folder/window scope as the seed.
void PushPathID(const std::string& path) {
    ImGui::PushOverrideID(ImHashData(path.data(), path.size(),
        ImGui::GetCurrentWindow()->IDStack.back()));
}


ImU32 Ink(ImVec4 color, float alpha = 1.f) {
    color.w *= alpha * ImGui::GetStyle().Alpha;
    return ImGui::ColorConvertFloat4ToU32(color);
}

// Both shapes use the same rounded corners and a gently curved folder tab.
// Concave triangulation is small, bounded vector geometry, not an image copy.
void FolderPath(ImDrawList* draw, ImVec2 origin, float width, float height, bool front) {
    const auto p = [&](float x, float y) { return ImVec2(origin.x + x * width, origin.y + y * height); };
    const float top = front ? .18f : .13f;
    draw->PathLineTo(p(.07f, top));
    if (front) {
        draw->PathLineTo(p(.34f, top));
        draw->PathBezierCubicCurveTo(p(.43f, top), p(.43f, .34f), p(.53f, .34f));
        draw->PathLineTo(p(.93f, .34f));
        draw->PathBezierCubicCurveTo(p(.98f, .34f), p(1.f, .37f), p(1.f, .43f));
    } else {
        draw->PathLineTo(p(.43f, top));
        draw->PathBezierCubicCurveTo(p(.54f, top), p(.50f, 0.f), p(.62f, 0.f));
        draw->PathLineTo(p(.93f, 0.f));
        draw->PathBezierCubicCurveTo(p(.98f, 0.f), p(1.f, .03f), p(1.f, .09f));
    }
    draw->PathLineTo(p(1.f, .91f));
    draw->PathBezierCubicCurveTo(p(1.f, .97f), p(.98f, 1.f), p(.93f, 1.f));
    draw->PathLineTo(p(.07f, 1.f));
    draw->PathBezierCubicCurveTo(p(.02f, 1.f), p(0.f, .97f), p(0.f, .91f));
    draw->PathLineTo(p(0.f, top + .09f));
    draw->PathBezierCubicCurveTo(p(0.f, top + .03f), p(.02f, top), p(.07f, top));
}

void GradientFolder(ImDrawList* draw, ImVec2 origin, float width, float height,
    bool front, ImVec4 top, ImVec4 bottom) {
    const int start = draw->VtxBuffer.Size;
    FolderPath(draw, origin, width, height, front);
    draw->PathFillConcave(Ink(top));
    const float y0 = origin.y + (front ? .18f : 0.f) * height;
    for (int index = start; index < draw->VtxBuffer.Size; ++index) {
        auto& vertex = draw->VtxBuffer[index];
        const float amount = std::clamp((vertex.pos.y - y0) / std::max(1.f, origin.y + height - y0), 0.f, 1.f);
        const float originalAlpha = static_cast<float>((vertex.col >> IM_COL32_A_SHIFT) & 0xff) / 255.f;
        const float fringe = std::clamp(originalAlpha / std::max(.001f, top.w * ImGui::GetStyle().Alpha), 0.f, 1.f);
        vertex.col = Ink(Header::Blend(top, bottom, amount), fringe);
    }
}

void CropUvs(const Texture& texture, float aspect, ImVec2& low, ImVec2& high) {
    low = ImVec2(0, 1); high = ImVec2(1, 0);
    if (texture.width <= 0 || texture.height <= 0) return;
    const float imageAspect = static_cast<float>(texture.width) / texture.height;
    if (imageAspect > aspect) {
        const float half = .5f * aspect / imageAspect;
        low.x = .5f - half; high.x = .5f + half;
    } else {
        const float half = .5f * imageAspect / aspect;
        low.y = .5f + half; high.y = .5f - half;
    }
}

void PhotoFolder(ImDrawList* draw, ImVec2 origin, float width, float height, const Texture& texture, bool front = false) {
    ImVec2 low, high;
    CropUvs(texture, width / height, low, high);
    draw->PushTextureID(static_cast<ImTextureID>(static_cast<intptr_t>(texture.handle)));
    const int start = draw->VtxBuffer.Size;
    FolderPath(draw, origin, width, height, front);
    draw->PathFillConcave(Ink(ImVec4(1, 1, 1, 1)));
    for (int index = start; index < draw->VtxBuffer.Size; ++index) {
        auto& vertex = draw->VtxBuffer[index];
        vertex.uv = ImVec2(low.x + (high.x - low.x) * (vertex.pos.x - origin.x) / width,
            low.y + (high.y - low.y) * (vertex.pos.y - origin.y) / height);
    }
    draw->PopTextureID();
}

void RotateVertices(ImDrawList* draw, int start, ImVec2 pivot, float angle) {
    const float sine = std::sin(angle), cosine = std::cos(angle);
    for (int index = start; index < draw->VtxBuffer.Size; ++index) {
        auto& point = draw->VtxBuffer[index].pos;
        const float x = point.x - pivot.x, y = point.y - pivot.y;
        point = ImVec2(pivot.x + x * cosine - y * sine, pivot.y + x * sine + y * cosine);
    }
}

void Shadow(ImDrawList* draw, ImVec2 minimum, ImVec2 maximum, float scale, float reveal) {
    // A few transparent rings approximate the reference's broad ambient shadow
    // without offscreen targets, readbacks or a per-frame blur pass.
    for (int ring = 8; ring > 0; --ring) {
        const float spread = ring * 1.8f * scale;
        draw->AddRectFilled(ImVec2(minimum.x - spread, minimum.y + 10 * scale - spread),
            ImVec2(maximum.x + spread, maximum.y + 10 * scale + spread),
            Ink(ImVec4(0, 0, 0, .012f + .003f * reveal)), (12.f + ring * 1.5f) * scale);
    }
}

void Ellipsis(ImDrawList* draw, const std::string& text, ImVec2 minimum, ImVec2 maximum, ImVec4 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::RenderTextEllipsis(draw, minimum, maximum, maximum.x, text.c_str(), nullptr, nullptr);
    ImGui::PopStyleColor();
}

struct PagePose { ImRect rect; float angle = 0; };
PagePose Page(ImVec2 origin, float width, std::size_t index, std::size_t count, float reveal, float lift) {
    const float scale = width / 220.f, rank = static_cast<float>(index);
    const float x = origin.x + (12.f + rank * 5.f + (rank - 1.5f) * 12.f * reveal) * scale;
    const float y = origin.y + (30.f + rank * 3.f - (98.f - rank * 19.f) * reveal - 12.f * lift) * scale;
    return {ImRect(ImVec2(x, y), ImVec2(x + width - 38 * scale, y + 126 * scale)),
        (rank - (count - 1.f) * .5f) * .09f * reveal};
}
struct FolderTilt {
    ImVec2 pivot;
    float bottom, scale, reveal, yawSin, yawCos, pitchSin, pitchCos;

    FolderTilt(ImVec2 origin, float width, float amount, float leanX, float leanY)
        : scale(width / 220.f), reveal(amount) {
        const float height = 168.f * scale;
        pivot = ImVec2(origin.x + width * .5f, origin.y + height * .8f);
        bottom = origin.y + height;
        const float yaw = leanX * .16f, pitch = .025f + leanY * .12f;
        yawSin = std::sin(yaw); yawCos = std::cos(yaw);
        pitchSin = std::sin(pitch); pitchCos = std::cos(pitch);
    }
    ImVec2 Apply(ImVec2 point) const {
        const float x = point.x - pivot.x, y = point.y - pivot.y;
        const float z = (bottom - point.y) * .10f;
        const float rx = x * yawCos + z * yawSin;
        const float rz = -x * yawSin + z * yawCos;
        const float ry = y * pitchCos - rz * pitchSin;
        const float depth = y * pitchSin + rz * pitchCos;
        const float perspective = 700.f * scale / std::max(100.f * scale, 700.f * scale - depth);
        return ImVec2(pivot.x + rx * perspective, pivot.y + ry * perspective - reveal * 2.f * scale);
    }
};
ImVec2 Tilt(ImVec2 point, ImVec2 origin, float width, float reveal, float leanX, float leanY) {
    return FolderTilt(origin, width, reveal, leanX, leanY).Apply(point);
}
ImVec2 FoldFront(ImVec2 point, ImVec2 origin, float width, float reveal) {
    const float height = width * 168.f / 220.f;
    const float distance = std::clamp((origin.y + height - point.y) / height, 0.f, 1.f);
    point.x = origin.x + width * .5f + (point.x - origin.x - width * .5f) *
        (1.f + distance * (.075f + .075f * reveal));
    point.y = origin.y + height - (origin.y + height - point.y) * (.86f - .16f * reveal);
    return point;
}
ImRect OpenButton(ImVec2 origin, float width, float reveal, float leanX, float leanY) {
    const float scale = width / 220.f;
    const ImVec2 a = Tilt(FoldFront(ImVec2(origin.x + width - 57 * scale, origin.y + 114 * scale), origin, width, reveal), origin, width, reveal, leanX, leanY);
    const ImVec2 b = Tilt(FoldFront(ImVec2(origin.x + width - 13 * scale, origin.y + 158 * scale), origin, width, reveal), origin, width, reveal, leanX, leanY);
    return ImRect(a, b);
}
ImRect PageHit(ImVec2 origin, float width, std::size_t index, std::size_t count, float reveal, float leanX, float leanY) {
    const auto pose = Page(origin, width, index, count, reveal, 0);
    const float scale = width / 220.f;
    // Exposed top strips do not overlap the next page or the front pocket.
    // Include the small lifted extent so a stationary pointer cannot flicker.
    const float strip = index + 1 == count ? 40.f : 19.f;
    ImRect result(Tilt(ImVec2(pose.rect.Min.x, pose.rect.Min.y - 13 * scale), origin, width, reveal, leanX, leanY),
        Tilt(ImVec2(pose.rect.Max.x, pose.rect.Min.y + strip * scale), origin, width, reveal, leanX, leanY));
    result.Expand(2 * scale);
    return result;
}
void DrawFolder(ImDrawList* draw, ImVec2 origin, float width, float reveal,
    float leanX, float leanY, const std::array<float, 4>& lifts,
    const std::array<Texture, 4>& images, const RawWorkspace::SourceFolderCard& folder,
    const std::string& title, const std::string& subtitle, ImVec4 surface,
    Renderer::FolderPocketMaterial& material) {
    const float scale = width / 220.f, height = 168.f * scale;
    const auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const bool light = surface.x + surface.y + surface.z > 1.5f;
    const ImVec4 paper = light ? ImVec4(.965f, .958f, .97f, 1) : Header::Blend(surface, text, .095f);
    const ImVec4 muted = Header::Blend(paper, text, .64f);
    const ImVec2 bottom(origin.x + width, origin.y + height);
    Shadow(draw, ImVec2(origin.x + 3 * scale, origin.y + 58 * scale), bottom, scale, reveal);
    const int folderStart = draw->VtxBuffer.Size;
    const std::size_t imageCount = std::min(images.size(), folder.previews.size());
    if (imageCount && images[0].handle) PhotoFolder(draw, origin, width, height, images[0]);
    else GradientFolder(draw, origin, width, height, false,
        Header::Blend(paper, text, .035f), Header::Blend(paper, text, .10f));
    // The side gussets keep a visible gap between the back and hinged front.
    const ImVec4 side = Header::Blend(paper, text, light ? .15f : .045f);
    const float lipY = height * (.30f + reveal * .13f);
    draw->AddTriangleFilled(ImVec2(origin.x + 3 * scale, origin.y + 35 * scale),
        ImVec2(origin.x - 7 * scale, origin.y + lipY), ImVec2(origin.x + 8 * scale, bottom.y - 5 * scale), Ink(side));
    draw->AddTriangleFilled(ImVec2(bottom.x - 3 * scale, origin.y + 43 * scale),
        ImVec2(bottom.x + 7 * scale, origin.y + lipY + 20 * scale), ImVec2(bottom.x - 8 * scale, bottom.y - 5 * scale), Ink(side));
    const std::size_t count = imageCount ? imageCount : folder.countKnown && folder.sourceCount == 0 ? 0u : 2u;
    for (std::size_t index = 0; index < count; ++index) {
        const auto pose = Page(origin, width, index, count, reveal, lifts[index]);
        const auto minimum = pose.rect.Min, maximum = pose.rect.Max;
        const int first = draw->VtxBuffer.Size;
        draw->AddRectFilled(ImVec2(minimum.x + 2 * scale, minimum.y + 4 * scale),
            ImVec2(maximum.x + 2 * scale, maximum.y + 4 * scale), Ink(ImVec4(0, 0, 0, .18f)), 8 * scale);
        draw->AddRectFilled(ImVec2(minimum.x - scale, minimum.y - scale), ImVec2(maximum.x + scale, maximum.y + scale),
            Ink(Header::Blend(paper, text, .18f)), 8 * scale);
        if (imageCount && images[index].handle) {
            ImVec2 low, high;
            CropUvs(images[index], pose.rect.GetWidth() / pose.rect.GetHeight(), low, high);
            draw->AddImageRounded(static_cast<ImTextureID>(static_cast<intptr_t>(images[index].handle)),
                minimum, maximum, low, high, Ink(ImVec4(1, 1, 1, 1)), 7 * scale);
            // The header belongs to the page throughout its travel. Later
            // pages and the front pocket occlude it naturally while tucked.
            draw->AddRectFilled(minimum, ImVec2(maximum.x, minimum.y + 19 * scale),
                Ink(ImVec4(0, 0, 0, .42f)), 7 * scale, ImDrawFlags_RoundCornersTop);
            ImGui::PushFont(nullptr, 10 * scale);
            const auto& source = folder.previews[index];
            const auto name = source.fileName.empty() ? source.absolutePath.filename().u8string() : source.fileName;
            Ellipsis(draw, name, ImVec2(minimum.x + 7 * scale, minimum.y + 3 * scale),
                ImVec2(maximum.x - 7 * scale, minimum.y + 18 * scale), ImVec4(1, 1, 1, 1));
            ImGui::PopFont();
        } else draw->AddRectFilled(minimum, maximum, Ink(Header::Blend(paper, text, .035f + index * .025f)), 7 * scale);
        RotateVertices(draw, first, ImVec2(pose.rect.GetCenter().x, maximum.y - 8 * scale), pose.angle);
    }
    const int frontStart = draw->VtxBuffer.Size;
    // A narrow rim underneath the face provides thickness, not a second opaque
    // panel. Both the face and its typography hinge about the same lower edge.
    GradientFolder(draw, ImVec2(origin.x, origin.y + 3 * scale), width, height, true,
        Header::Blend(paper, text, .12f), Header::Blend(paper, text, .055f));
    const ImVec4 pocketTop = Header::Blend(paper, text, light ? .025f : .055f);
    bool frost = false;
    if (imageCount && images[0].handle) {
        ImVec2 low, high;
        CropUvs(images[0], width / height, low, high);
        frost = material.Begin(draw, ImVec2(std::abs(high.x - low.x) * 6.f / width,
            std::abs(high.y - low.y) * 6.f / height), pocketTop, paper, .14f,
            ImVec2(low.y + (high.y - low.y) * .18f, high.y));
        if (frost) { PhotoFolder(draw, origin, width, height, images[0], true); material.End(draw); }
    }
    if (!frost) GradientFolder(draw, origin, width, height, true, pocketTop, paper);
    FolderPath(draw, origin, width, height, true);
    draw->PathStroke(Ink(light ? ImVec4(1, 1, 1, .62f) : ImVec4(1, 1, 1, .18f)), ImDrawFlags_Closed, scale);
    const ImVec2 buttonMin(bottom.x - 53 * scale, bottom.y - 51 * scale);
    draw->AddRectFilled(buttonMin, ImVec2(buttonMin.x + 36 * scale, buttonMin.y + 34 * scale),
        Ink(Header::Blend(paper, text, .07f)), 10 * scale);
    const ImVec2 center(buttonMin.x + 18 * scale, buttonMin.y + 17 * scale);
    draw->AddLine(ImVec2(center.x - 6 * scale, center.y), ImVec2(center.x + 6 * scale, center.y), Ink(text), 1.4f * scale);
    draw->AddLine(ImVec2(center.x + scale, center.y - 5 * scale), ImVec2(center.x + 6 * scale, center.y), Ink(text), 1.4f * scale);
    draw->AddLine(ImVec2(center.x + scale, center.y + 5 * scale), ImVec2(center.x + 6 * scale, center.y), Ink(text), 1.4f * scale);
    ImGui::PushFont(nullptr, 14 * scale);
    Ellipsis(draw, title, ImVec2(origin.x + 18 * scale, bottom.y - 55 * scale), ImVec2(buttonMin.x - 8 * scale, bottom.y - 30 * scale), text);
    ImGui::PopFont();
    ImGui::PushFont(nullptr, 11 * scale);
    Ellipsis(draw, subtitle, ImVec2(origin.x + 18 * scale, bottom.y - 31 * scale), ImVec2(buttonMin.x - 8 * scale, bottom.y - 12 * scale), muted);
    ImGui::PopFont();
    for (int index = frontStart; index < draw->VtxBuffer.Size; ++index)
        draw->VtxBuffer[index].pos = FoldFront(draw->VtxBuffer[index].pos, origin, width, reveal);
    // The pose is common to every vertex. Keep trigonometry outside the
    // mesh loop without caching pixels or changing the material/geometry.
    const FolderTilt tilt(origin, width, reveal, leanX, leanY);
    for (int index = folderStart; index < draw->VtxBuffer.Size; ++index)
        draw->VtxBuffer[index].pos = tilt.Apply(draw->VtxBuffer[index].pos);
}
void DrawAdd(ImDrawList* draw, ImRect tile, ImVec4 surface, bool hovered, float unit) {
    const auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const auto muted = Header::Blend(surface, text, .58f);
    draw->AddRectFilled(tile.Min, tile.Max, Ink(Header::Blend(surface, text, hovered ? .065f : .025f)), 16 * unit);
    draw->AddRect(tile.Min, tile.Max, Ink(Header::Blend(surface, text, hovered ? .32f : .14f)), 16 * unit, 0, unit);
    const ImVec2 center(tile.GetCenter().x, tile.Min.y + 60 * unit);
    draw->AddLine(ImVec2(center.x - 13 * unit, center.y), ImVec2(center.x + 13 * unit, center.y), Ink(text), 2 * unit);
    draw->AddLine(ImVec2(center.x, center.y - 13 * unit), ImVec2(center.x, center.y + 13 * unit), Ink(text), 2 * unit);
    ImGui::PushFont(nullptr, 14 * unit);
    const auto label = ImGui::CalcTextSize("Add source folder");
    draw->AddText(ImVec2(center.x - label.x * .5f, tile.Min.y + 104 * unit), Ink(text), "Add source folder");
    ImGui::PopFont();
    ImGui::PushFont(nullptr, 11 * unit);
    const auto detail = ImGui::CalcTextSize("Choose from your computer");
    draw->AddText(ImVec2(center.x - detail.x * .5f, tile.Min.y + 129 * unit), Ink(muted), "Choose from your computer");
    ImGui::PopFont();
}
bool HitButton(const char* id, const ImRect& rect, bool overlap = false, bool navigable = true) {
    ImGui::SetCursorScreenPos(rect.Min);
    return ImGui::InvisibleButton(id, rect.GetSize(), (navigable ? ImGuiButtonFlags_EnableNav : ImGuiButtonFlags_None) |
        (overlap ? ImGuiButtonFlags_AllowOverlap : ImGuiButtonFlags_None));
}
} // namespace

GalleryFolderCards::Action GalleryFolderCards::Render(
    const std::vector<RawWorkspace::SourceFolderCard>& folders, const ImVec2& size,
    ImVec4 surface, const ResolveTexture& texture,Navigation::GalleryNavigationTransition* navigation) {
    Action action;
    const float unit = ImGui::GetFontSize() / 16.f;
    const bool reduced = LibraryUi::ReducedMotion();
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) for (auto& entry : m_Motion) entry.second.expanded = false;
    const auto text = ImGui::GetStyleColorVec4(ImGuiCol_Text);
    const auto muted = Header::Blend(surface, text, .60f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(30 * unit, 24 * unit));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, surface);
    if (!ImGui::BeginChild("SourceFolders", size, ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopStyleVar(4);
        return action;
    }
    ImGui::PushFont(nullptr, 24 * unit); ImGui::TextUnformatted("Source folders"); ImGui::PopFont();
    if (folders.size() > 3u) {
        if (size.x >= 380 * unit) { ImGui::SameLine(); ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - 112 * unit)); }
        else ImGui::Dummy(ImVec2(0, 6 * unit));
        ImGui::PushFont(nullptr, 14 * unit);
        if (ImGui::Button("+ Add folder", ImVec2(112 * unit, 28 * unit))) action.addFolder = true;
        ImGui::PopFont();
    }
    ImGui::Dummy(ImVec2(0, 8 * unit));
    ImGui::PushStyleColor(ImGuiCol_Text, muted); ImGui::TextWrapped("Click a folder to explore its images. Use its arrow to browse the full gallery."); ImGui::PopStyleColor();
    ImGui::Dummy(ImVec2(0, 20 * unit));
    if (!ImGui::BeginChild("FolderCards", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground)) {
        ImGui::EndChild(); ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopStyleVar(4);
        return action;
    }
    const float available = std::max(1.f, ImGui::GetContentRegionAvail().x), strideTarget = 292 * unit;
    const int columns = std::max(1, static_cast<int>((available + 30 * unit) / strideTarget));
    const float width = std::min(220 * unit, std::max(96 * unit, available - 24 * unit));
    const float stride = std::min(strideTarget, available / columns), rowHeight = 274 * unit;
    const float left = ImGui::GetCursorScreenPos().x + std::max(0.f, (available - columns * stride) * .5f);
    const std::size_t total = folders.size() + 1;
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>((total + columns - 1) / columns), rowHeight);
    while (clipper.Step()) for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        const float rowY = ImGui::GetCursorScreenPos().y;
        for (int column = 0; column < columns; ++column) {
            const auto index = static_cast<std::size_t>(row * columns + column);
            if (index >= total) break;
            const ImVec2 origin(left + column * stride + (stride - width) * .5f, rowY + 88 * unit);
            if (index == folders.size()) {
                const ImRect tile(origin, ImVec2(origin.x + width, origin.y + 168 * width / 220.f));
                if (HitButton("Add source folder", tile)) action.addFolder = true;
                const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
                auto* draw=ImGui::GetWindowDrawList();
                const int first=draw->VtxBuffer.Size;
                DrawAdd(draw, tile, surface, hovered, width / 220.f);
                if(navigation && navigation->FoldersMoving()) {
                    const auto pose=navigation->FolderPose("add-folder",tile,index);
                    for(int vertex=first;vertex<draw->VtxBuffer.Size;++vertex) {
                        auto& point=draw->VtxBuffer[vertex].pos;
                        point={pose.Min.x+(point.x-tile.Min.x)*pose.GetWidth()/tile.GetWidth(),
                            pose.Min.y+(point.y-tile.Min.y)*pose.GetHeight()/tile.GetHeight()};
                    }
                }
                if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                continue;
            }
            const auto& folder = folders[index];
            const auto key = folder.root.lexically_normal().generic_u8string();
            PushPathID(key);
            auto& motion = m_Motion[key];
            const int frame = ImGui::GetFrameCount();
            if (motion.frame != frame - 1) {
                const bool expanded = motion.expanded; motion = {}; motion.expanded = expanded; motion.reveal = expanded ? 1.f : .16f;
            }
            motion.frame = frame;
            const auto open = OpenButton(origin, width, motion.reveal, motion.leanX, motion.leanY);
            const ImRect body(ImVec2(origin.x - 8 * unit, origin.y + (motion.expanded ? 72.f : 0.f) * unit),
                ImVec2(open.Min.x - 4 * unit, origin.y + 174 * unit));
            bool bodyClicked = HitButton("Expand or collapse folder", body);
            bool bodyHovered = ImGui::IsItemHovered();
            const bool bodyFocused = ImGui::IsItemFocused();
            // Cover the rest of the face without putting a second keyboard
            // stop over the independent Open arrow or the exposed image pages.
            const ImRect bodyRight(ImVec2(body.Max.x, body.Min.y), ImVec2(origin.x + width + 8 * unit, open.Min.y - 3 * unit));
            if (bodyRight.GetHeight() > 0) {
                bodyClicked |= HitButton("Expand folder face", bodyRight, false, false);
                bodyHovered |= ImGui::IsItemHovered();
            }
            if (bodyClicked) motion.expanded = !motion.expanded;
            if (HitButton("Open folder gallery", open)) {
                action.openFolder=folder.root;
                action.folderBounds={origin,{origin.x+width,origin.y+168*unit}};
            }
            const bool openHovered = ImGui::IsItemHovered();
            if (openHovered && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Open all images in %s", folder.root.u8string().c_str());
            const auto mouse = ImGui::GetMousePos();
            const bool aroundFolder = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImRect(ImVec2(origin.x - 20 * unit, rowY), ImVec2(origin.x + width + 25 * unit, origin.y + 176 * unit)).Contains(mouse);
            const float target = motion.expanded ? 1.f : bodyHovered || bodyFocused ? .30f : .16f;
            const float leanX = aroundFolder ? std::clamp((mouse.x - origin.x) / width * 2.f - 1.f, -1.f, 1.f) : 0.f;
            const float leanY = aroundFolder ? std::clamp((mouse.y - origin.y) / (168 * unit) * 2.f - 1.f, -1.f, 1.f) : 0.f;
            if (reduced) { motion.reveal = target; motion.leanX = motion.leanY = 0; }
            else { Header::ApproachMotion(motion.reveal, target, .46f); Header::Approach(motion.leanX, leanX, 14.f, .001f); Header::Approach(motion.leanY, leanY, 14.f, .001f); }
            std::array<Texture, 4> images{};
            const std::size_t count = std::min(images.size(), folder.previews.size());
            for (std::size_t image = 0; image < count; ++image) images[image] = texture(folder.root, folder.previews[image]);
            int hoveredPage = -1;
            if (motion.expanded && motion.reveal > .65f && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                for (std::size_t image = count; image > 0; --image)
                    if (images[image - 1].handle && PageHit(origin, width, image - 1, count,
                            motion.reveal, motion.leanX, motion.leanY).Contains(mouse)) {
                        hoveredPage = static_cast<int>(image - 1);
                        break;
                    }
            }
            // Frontmost page gets the first native hit target. Later rear
            // targets cannot consume its click in an overlapping lifted strip.
            for (std::size_t reverse = count; reverse > 0; --reverse) {
                const std::size_t image = reverse - 1;
                bool raised = false;
                if (motion.expanded && motion.reveal > .65f && images[image].handle) {
                    // A catalog refresh can replace/reorder preview slots while
                    // a button is held. Identity follows the original, not slot.
                    const auto previewKey = folder.previews[image].absolutePath.lexically_normal().generic_u8string();
                    PushPathID(previewKey);
                    const bool clicked = HitButton("Open image", PageHit(origin, width, image, count, motion.reveal, motion.leanX, motion.leanY));
                    const bool keyboardFocused = ImGui::IsItemFocused() && ImGui::GetIO().NavVisible;
                    if (clicked && (hoveredPage == static_cast<int>(image) || keyboardFocused))
                        {
                        const auto page=Page(origin,width,image,count,motion.reveal,motion.pageLift[image]);
                        Action::Image selected{folder.previews[image].absolutePath,folder.root,
                            folder.previews[image].relativePathKey,page.rect};
                        CropUvs(images[image],page.rect.GetWidth()/page.rect.GetHeight(),selected.uvMinimum,selected.uvMaximum);
                        action.openImage=std::move(selected);
                    }
                    raised = hoveredPage == static_cast<int>(image) || keyboardFocused;
                    if (raised) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::PopID();
                }
                if (reduced) motion.pageLift[image] = raised ? 1.f : 0.f;
                else Header::ApproachMotion(motion.pageLift[image], raised ? 1.f : 0.f, .18f);
            }
            if(action.openFolder && *action.openFolder==folder.root) {
                for(std::size_t image=0;image<count;++image) {
                    const auto page=Page(origin,width,image,count,motion.reveal,motion.pageLift[image]);
                    Action::Image selected{folder.previews[image].absolutePath,folder.root,
                        folder.previews[image].relativePathKey,page.rect};
                    CropUvs(images[image],page.rect.GetWidth()/page.rect.GetHeight(),selected.uvMinimum,selected.uvMaximum);
                    action.pages.push_back(std::move(selected));
                }
            }
            // The clipper may retain a fully offscreen navigation row. Keep
            // its native input targets above, but not its meshes/material draws.
            // Generous bounds include the lifted fan, lean and ambient shadow.
            if (ImGui::IsRectVisible(ImVec2(origin.x - 64 * unit, rowY - 64 * unit),
                    ImVec2(origin.x + width + 64 * unit, origin.y + 232 * unit))) {
                std::string title = folder.root.filename().u8string(); if (title.empty()) title = key;
                const std::string subtitle = folder.countKnown ? std::to_string(folder.sourceCount) + (folder.sourceCount == 1 ? " image" : " images") : "Open to browse";
                auto* draw=ImGui::GetWindowDrawList();
                const int firstVertex=draw->VtxBuffer.Size;
                const bool moving=navigation && navigation->FoldersMoving();
                DrawFolder(draw, origin, width, moving && navigation->ChosenFolder(key) ?
                    motion.reveal+(1.f-motion.reveal)*(1.f-navigation->FolderAmount()) : motion.reveal, motion.leanX, motion.leanY,
                    motion.pageLift, images, folder, title, subtitle, surface, m_Material);
                if(moving) {
                    const ImRect target(origin,{origin.x+width,origin.y+168*width/220.f});
                    const auto pose=navigation->FolderPose(key,target,static_cast<std::size_t>(row*columns+column));
                    for(int vertex=firstVertex;vertex<draw->VtxBuffer.Size;++vertex) {
                        auto& point=draw->VtxBuffer[vertex].pos;
                        point={pose.Min.x+(point.x-target.Min.x)*pose.GetWidth()/target.GetWidth(),
                            pose.Min.y+(point.y-target.Min.y)*pose.GetHeight()/target.GetHeight()};
                    }
                }
            }
            if (bodyHovered || openHovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::PopID();
        }
        ImGui::SetCursorScreenPos(ImVec2(left, rowY + rowHeight));
    }
    ImGui::Dummy(ImVec2(0, 8 * unit));
    ImGui::PushStyleColor(ImGuiCol_Text, muted); ImGui::TextWrapped("Click to unfold  ·  Hover an image to lift it  ·  Click an image to open"); ImGui::PopStyleColor();
    ImGui::EndChild(); ImGui::EndChild(); ImGui::PopStyleColor(); ImGui::PopStyleVar(4);
    return action;
}
} // namespace Stack::Editor::RawLabInternal
