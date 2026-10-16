#include "RawLabGalleryDetails.h"

#include "App/AppHeaderStyle.h"
#include "Library/LibraryControls.h"
#include "Raw/RawImageData.h"
#include "Raw/RawWorkspace.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string_view>

namespace Stack::Editor::RawLabInternal {
namespace {

struct InspectorSpacing {
    float padding, labelGap, fieldGap, sectionGap, exposureRowGap, thumbnailHeight;
    float nameHeight, captionHeight, labelHeight, valueHeight, exposureHeight, locationHeight;
};
constexpr InspectorSpacing QuietSpacing{16, 2, 8, 16, 12, 96, 24, 18, 16, 20, 22, 32};
constexpr InspectorSpacing CondensedSpacing{12, 2, 6, 10, 8, 72, 22, 16, 14, 18, 20, 28};

void Space(float pixels) {
    if (pixels > 0) ImGui::Dummy(ImVec2(0, pixels));
}

void Line(const char* text, float points, float height, float unit, ImVec4 color) {
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * points / 16.f);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrapped("%s", text);
    const float used = ImGui::GetItemRectSize().y;
    ImGui::PopStyleColor();
    ImGui::PopFont();
    Space(std::max(0.f, height * unit - used));
}

bool SameColor(ImVec4 a, ImVec4 b) {
    return a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w;
}

float Luminance(ImVec4 c) {
    const auto linear = [](float v) {
        return v <= .04045f ? v / 12.92f : std::pow((v + .055f) / 1.055f, 2.4f);
    };
    return .2126f * linear(c.x) + .7152f * linear(c.y) + .0722f * linear(c.z);
}

float Contrast(ImVec4 a, ImVec4 b) {
    const float x = Luminance(a), y = Luminance(b);
    return (std::max(x, y) + .05f) / (std::min(x, y) + .05f);
}

ImVec4 Readable(ImVec4 ink, ImVec4 surface, float minimum) {
    ink.w = 1;
    if (Contrast(ink, surface) >= minimum) return ink;
    const ImVec4 black(0, 0, 0, 1), white(1, 1, 1, 1);
    const auto destination = Contrast(black, surface) > Contrast(white, surface) ? black : white;
    for (int step = 1; step <= 20; ++step) {
        const auto candidate = Header::Blend(ink, destination, step / 20.f);
        if (Contrast(candidate, surface) >= minimum) return candidate;
    }
    return destination;
}

void Glyph(ImDrawList* draw, int glyph, ImVec2 origin, float unit, ImU32 color) {
    const auto p = [&](float x, float y) { return ImVec2(origin.x + x * unit, origin.y + y * unit); };
    const auto line = [&](float x, float y, float x2, float y2) {
        draw->AddLine(p(x, y), p(x2, y2), color, 1.25f * unit);
    };
    if (glyph == 0) {
        draw->AddRect(p(5, 5), p(14, 14), color, 1.5f * unit, 0, 1.25f * unit);
        line(2, 11, 2, 2);
        line(2, 2, 11, 2);
    } else if (glyph == 1) {
        draw->AddRect(p(3, 3), p(13, 15), color, 1.5f * unit, 0, 1.25f * unit);
        draw->AddRectFilled(p(6, 1), p(10, 5), color, unit);
        line(6, 8, 10, 8);
        line(6, 11, 10, 11);
    } else {
        draw->PathLineTo(p(1, 5)); draw->PathLineTo(p(1, 3)); draw->PathLineTo(p(6, 3));
        draw->PathLineTo(p(8, 5)); draw->PathLineTo(p(14, 5));
        draw->PathStroke(color, 0, 1.25f * unit);
        draw->PathLineTo(p(1, 5)); draw->PathLineTo(p(1, 13)); draw->PathLineTo(p(13, 13));
        draw->PathLineTo(p(15, 7)); draw->PathLineTo(p(4, 7)); draw->PathLineTo(p(2, 13));
        draw->PathStroke(color, 0, 1.25f * unit);
    }
}

} // namespace

void GalleryDetails::Prepare(const RawWorkspace::SourceRecord* source,
    const std::shared_ptr<const Raw::RawMetadata>& metadata, const std::filesystem::path& project) {
    const std::filesystem::path empty;
    const auto& path = source ? source->absolutePath : empty;
    const std::string_view name = source ? std::string_view(source->fileName) : std::string_view();
    const std::string_view extension = source ? std::string_view(source->extension) : std::string_view();
    const auto bytes = source ? source->fileSizeBytes : 0;
    const auto modified = source ? source->modifiedTimeTicks : 0;
    if (m_Prepared && m_SourcePath == path && m_ProjectPath == project && m_SourceName == name &&
        m_Extension == extension && m_FileBytes == bytes && m_Modified == modified && m_Metadata == metadata) return;
    m_Prepared = true;
    m_LayoutDirty = true;
    m_SourcePath = path;
    m_ProjectPath = project;
    m_SourceName = name;
    m_Extension = extension;
    m_FileBytes = bytes;
    m_Modified = modified;
    m_Metadata = metadata;
    m_Name = project.empty() ? m_SourceName : project.filename().u8string();
    m_Dimensions.clear();
    m_Camera = "Unavailable";
    m_Lens = "Unavailable";
    m_Exposure.fill("\xe2\x80\x94");
    m_Format = m_Extension;
    if (!m_Format.empty() && m_Format.front() == '.') m_Format.erase(m_Format.begin());
    if (m_Format.empty()) m_Format = source ? "RAW" : "Stack project";
    char value[128]{};
    std::snprintf(value, sizeof(value), "%.2f MB", static_cast<double>(bytes) / (1024. * 1024.));
    m_Size = source ? value : "Unavailable";
    if (!metadata) return;
    if (!metadata->cameraModel.empty() || !metadata->cameraMake.empty()) {
        m_Camera = metadata->cameraMake.empty() ? metadata->cameraModel :
            metadata->cameraModel.empty() ? metadata->cameraMake :
            metadata->cameraModel.rfind(metadata->cameraMake, 0) == 0 ? metadata->cameraModel :
            metadata->cameraMake + " " + metadata->cameraModel;
    }
    if (!metadata->lensModel.empty()) m_Lens = metadata->lensModel;
    const int width = Raw::DisplayWidth(*metadata), height = Raw::DisplayHeight(*metadata);
    if (width > 0 && height > 0) {
        std::snprintf(value, sizeof(value), "%d \xc3\x97 %d \xc2\xb7 %.1f MP", width, height,
            static_cast<double>(width) * height / 1000000.);
        m_Dimensions = value;
    }
    if (metadata->hasIsoSpeed && std::isfinite(metadata->isoSpeed) && metadata->isoSpeed > 0) {
        std::snprintf(value, sizeof(value), "%.0f", metadata->isoSpeed);
        m_Exposure[0] = value;
    }
    const float seconds = metadata->exposureTimeSeconds;
    if (metadata->hasExposureTime && std::isfinite(seconds) && seconds > 0) {
        const double reciprocal = std::round(1.0 / seconds);
        if (seconds <= .5f && std::abs(reciprocal * seconds - 1.0) < .015)
            std::snprintf(value, sizeof(value), "1/%.0f s", reciprocal);
        else std::snprintf(value, sizeof(value), "%.3g s", static_cast<double>(seconds));
        m_Exposure[1] = value;
    }
    if (metadata->hasApertureFNumber && std::isfinite(metadata->apertureFNumber) && metadata->apertureFNumber > 0) {
        std::snprintf(value, sizeof(value), "f/%.1f", metadata->apertureFNumber);
        m_Exposure[2] = value;
    }
    if (metadata->hasFocalLength && std::isfinite(metadata->focalLengthMm) && metadata->focalLengthMm > 0) {
        std::snprintf(value, sizeof(value), "%.4g mm", static_cast<double>(metadata->focalLengthMm));
        m_Exposure[3] = value;
    }
}

void GalleryDetails::PreparePalette(ImVec4 workspace) {
    const std::array inputs{workspace, ImGui::GetStyleColorVec4(ImGuiCol_Text),
        ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled), ImGui::GetStyleColorVec4(ImGuiCol_NavCursor)};
    bool same = m_PaletteReady;
    for (std::size_t i = 0; i < inputs.size(); ++i) same = same && SameColor(inputs[i], m_PaletteInputs[i]);
    if (same) return;
    m_PaletteReady = true;
    m_PaletteInputs = inputs;
    m_Surface = workspace;
    m_Surface.w = 1.f;
    m_Text = Readable(inputs[1], m_Surface, 7.f);
    m_Secondary = Readable(Header::Blend(m_Surface, m_Text, .65f), m_Surface, 4.7f);
    m_Focus = Readable(inputs[3], m_Surface, 3.f);
    m_Scrollbar = {Header::Blend(m_Surface, m_Text, .30f),
        Header::Blend(m_Surface, m_Text, .45f), Header::Blend(m_Surface, m_Text, .60f)};
}

void GalleryDetails::PrepareLayout(float width, bool thumbnail) {
    if (!m_LayoutDirty && m_LayoutWidth == width && m_LayoutFont == ImGui::GetFont() &&
        m_LayoutFontSize == ImGui::GetFontSize() && m_LayoutFontBase == m_FontBase &&
        m_LayoutThumbnail == thumbnail) return;
    m_LayoutDirty = false;
    m_LayoutWidth = width;
    m_LayoutFont = ImGui::GetFont();
    m_LayoutFontSize = ImGui::GetFontSize();
    m_LayoutFontBase = m_FontBase;
    m_LayoutThumbnail = thumbnail;

    // Measure the comfortable layout with native font metrics only when its
    // wrapping inputs change. Height changes then need just one comparison;
    // scrollbar appearance is never fed back into the density decision.
    const auto& s = QuietSpacing;
    const float contentWidth = std::max(1.f, width - 2.f * s.padding * m_Unit);
    const auto measure = [&](const char* text, float points, float minimum, float wrapWidth) {
        ImGui::PushFont(nullptr, m_FontBase * points / 16.f);
        const float height = ImGui::CalcTextSize(text, nullptr, false, wrapWidth).y;
        ImGui::PopFont();
        return std::max(minimum * m_Unit, height);
    };
    ImGui::PushFont(nullptr, m_FontBase * 13.f / 16.f);
    m_EditRowWidth = 2.f * (std::max(ImGui::CalcTextSize("Copy edits").x,
        ImGui::CalcTextSize("Paste edits").x) + 40.f * m_Unit) + 8.f * m_Unit;
    ImGui::PopFont();
    float height = 2.f * s.padding * m_Unit;
    if (thumbnail) height += (s.thumbnailHeight + 8.f) * m_Unit;
    if (m_Name.empty()) {
        m_PreferredBodyHeight = height +
            measure("Hover or select an image to see its properties.", 14, 20, contentWidth);
        return;
    }
    height += measure(m_Name.c_str(), 16, s.nameHeight, contentWidth) + s.labelGap * m_Unit;
    const char* description = m_Dimensions.empty() ?
        (m_ProjectPath.empty() ? "RAW image" : "Stack project") : m_Dimensions.c_str();
    height += measure(description, 12, s.captionHeight, contentWidth) + s.sectionGap * m_Unit;
    const auto fieldHeight = [&](const char* label, const std::string& value) {
        return measure(label, 12, s.labelHeight, contentWidth) + s.labelGap * m_Unit +
            measure(value.c_str(), 14, s.valueHeight, contentWidth);
    };
    height += fieldHeight("Camera", m_Camera) + s.fieldGap * m_Unit;
    height += fieldHeight("Lens", m_Lens) + s.sectionGap * m_Unit;
    constexpr const char* labels[]{"ISO", "Shutter", "Aperture", "Focal length"};
    for (int row = 0; row < 2; ++row) {
        float rowHeight = 0;
        for (int column = 0; column < 2; ++column) {
            const int i = row * 2 + column;
            rowHeight = std::max(rowHeight, measure(labels[i], 12, s.labelHeight, contentWidth * .5f) +
                s.labelGap * m_Unit + measure(m_Exposure[i].c_str(), 16, s.exposureHeight, contentWidth * .5f));
        }
        height += rowHeight + (row ? s.exposureRowGap * m_Unit : 0.f);
    }
    const auto fileHeight = [&](const char* label, const std::string& value) {
        return measure(label, 12, s.labelHeight, contentWidth * .5f) + s.labelGap * m_Unit +
            measure(value.c_str(), 14, s.valueHeight, contentWidth * .5f);
    };
    height += s.sectionGap * m_Unit + std::max(fileHeight("Format", m_Format), fileHeight("Size", m_Size));
    m_PreferredBodyHeight = height + (4.f + s.locationHeight) * m_Unit;
}

bool GalleryDetails::Button(const char* id, const char* label, int glyph, bool enabled, ImVec2 size, bool quiet) {
    float& hover = m_Hover[static_cast<std::size_t>(glyph)];
    const ImVec4 face = Header::Blend(m_Surface, m_Text, quiet ? hover * .07f : .045f + hover * .065f);
    ImGui::PushFont(nullptr, m_FontBase * 13.f / 16.f);
    ImGui::PushID(id);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.f * m_Unit);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, quiet ? 0.f : 1.f * m_Unit);
    ImGui::PushStyleColor(ImGuiCol_Button, face);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, face);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, Header::Blend(m_Surface, m_Text, .14f));
    ImGui::PushStyleColor(ImGuiCol_Border, Header::Blend(m_Surface, m_Text, enabled ? .32f : .18f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0, 0, 0, 0));
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::Button("##Action", size);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
    const float target = hovered ? 1.f : 0.f;
    if (hover != target) {
        if (LibraryUi::ReducedMotion()) hover = target;
        else Header::ApproachMotion(hover, target, .11f);
    }
    const float textWidth = ImGui::CalcTextSize(label).x;
    const float x = quiet ? minimum.x : minimum.x + (size.x - textWidth - 24.f * m_Unit) * .5f;
    const ImU32 ink = ImGui::GetColorU32(m_Text);
    auto* draw = ImGui::GetWindowDrawList();
    Glyph(draw, glyph, ImVec2(x, minimum.y + (size.y - 16.f * m_Unit) * .5f), m_Unit, ink);
    draw->AddText(ImVec2(x + 24.f * m_Unit, minimum.y + (size.y - ImGui::GetFontSize()) * .5f), ink, label);
    if (quiet) {
        const ImVec2 a(maximum.x - 10.f * m_Unit, minimum.y + size.y * .5f);
        draw->AddLine(ImVec2(a.x - 3.f * m_Unit, a.y + 3.f * m_Unit),
            ImVec2(a.x + 3.f * m_Unit, a.y - 3.f * m_Unit), ink, m_Unit);
        draw->AddLine(ImVec2(a.x - 3.f * m_Unit, a.y - 3.f * m_Unit),
            ImVec2(a.x + 3.f * m_Unit, a.y - 3.f * m_Unit), ink, m_Unit);
        draw->AddLine(ImVec2(a.x + 3.f * m_Unit, a.y - 3.f * m_Unit),
            ImVec2(a.x + 3.f * m_Unit, a.y + 3.f * m_Unit), ink, m_Unit);
    }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(5);
    ImGui::PopStyleVar(2);
    ImGui::PopID();
    ImGui::PopFont();
    return pressed;
}

void GalleryDetails::EditsCopied() {
    m_CopiedUntil = ImGui::GetTime() + 1.6;
}

std::optional<RawGalleryActions::Action> GalleryDetails::Render(
    const RawWorkspace::SourceRecord* source, const std::shared_ptr<const Raw::RawMetadata>& metadata,
    const std::filesystem::path& displayedProject, const GalleryInspectorActions& actions,
    ImVec4 workspaceColor, const std::function<void(const ImVec2&)>& thumbnail) {
    Prepare(source, metadata, displayedProject);
    PreparePalette(workspaceColor);
    // Stack's embedded font is authored at 16 px. Explicit native line slots
    // and font roles scale together rather than inheriting HTML line metrics.
    m_Unit = ImGui::GetFontSize() / 16.f;
    m_FontBase = ImGui::GetStyle().FontSizeBase;
    const std::filesystem::path empty;
    const auto& focus = actions.focusedPath ? *actions.focusedPath : empty;
    if (focus != m_FocusedPath) {
        m_FocusedPath = focus;
        m_FocusedName = focus.filename().u8string();
        m_CopiedUntil = 0;
    }
    if (actions.selectionCount != m_SelectionCount) {
        m_SelectionCount = actions.selectionCount;
        m_SelectionScope = m_SelectionCount == 0 ? "No selection" : m_SelectionCount == 1 ?
            "Selected image" : std::to_string(m_SelectionCount) + " selected";
    }
    const auto& displayed = displayedProject.empty() ? m_SourcePath : displayedProject;
    const bool different = actions.selectionCount > 0 && !focus.empty() && focus != displayed;
    std::optional<RawGalleryActions::Action> command;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 0.f);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, m_Text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, m_Secondary);
    ImGui::PushStyleColor(ImGuiCol_NavCursor, m_Focus);
    if (!ImGui::BeginChild("QuietInspector", ImVec2(0, 0), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::EndChild();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(4);
        return {};
    }
    PrepareLayout(ImGui::GetContentRegionAvail().x, static_cast<bool>(thumbnail));
    const float contentWidth = std::max(1.f, ImGui::GetContentRegionAvail().x - 32.f * m_Unit);
    const bool stacked = actions.copyVisible && contentWidth < m_EditRowWidth;
    // Hovering another image changes the target label in this fixed header,
    // never the footer height or the metadata's available scrolling area.
    const float footerHeight = (84.f + (stacked ? 44.f : 0.f)) * m_Unit;
    const float bodyHeight = std::max(1.f, ImGui::GetContentRegionAvail().y - footerHeight);
    m_Condensed = m_PreferredBodyHeight > bodyHeight;
    const float padding = (m_Condensed ? CondensedSpacing.padding : QuietSpacing.padding) * m_Unit;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
    // Keep native wheel/drag input and its wider hit rail, with a quiet 4 px
    // rounded thumb. The transparent track creates no second dark gutter.
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 12.f * m_Unit);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarPadding, 4.f * m_Unit);
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarRounding, 6.f * m_Unit);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab, m_Scrollbar[0]);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, m_Scrollbar[1]);
    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive, m_Scrollbar[2]);
    if (ImGui::BeginChild("Metadata", ImVec2(0, bodyHeight),
            ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoBackground))
        command = RenderMetadata(actions, thumbnail);
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f * m_Unit, 12.f * m_Unit));
    if (ImGui::BeginChild("EditActions", ImVec2(0, footerHeight), ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        if (const auto edit = RenderEditActions(actions, stacked, different)) command = edit;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::EndChild();
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(4);
    return command;
}

std::optional<RawGalleryActions::Action> GalleryDetails::RenderMetadata(
    const GalleryInspectorActions& actions, const std::function<void(const ImVec2&)>& thumbnail) {
    std::optional<RawGalleryActions::Action> command;
    const auto& s = m_Condensed ? CondensedSpacing : QuietSpacing;
    if (thumbnail) {
        thumbnail(ImVec2(ImGui::GetContentRegionAvail().x, s.thumbnailHeight * m_Unit));
        Space(8.f * m_Unit);
    }
    if (m_Name.empty()) Line("Hover or select an image to see its properties.", 14, 20, m_Unit, m_Secondary);
    else {
        Line(m_Name.c_str(), 16, s.nameHeight, m_Unit, m_Text);
        Space(s.labelGap * m_Unit);
        const char* description = m_Dimensions.empty() ?
            (m_ProjectPath.empty() ? "RAW image" : "Stack project") : m_Dimensions.c_str();
        Line(description, 12, s.captionHeight, m_Unit, m_Secondary);
        Space(s.sectionGap * m_Unit);
        const auto field = [&](const char* label, const std::string& value) {
            Line(label, 12, s.labelHeight, m_Unit, m_Secondary);
            Space(s.labelGap * m_Unit);
            Line(value.c_str(), 14, s.valueHeight, m_Unit, m_Text);
        };
        field("Camera", m_Camera);
        Space(s.fieldGap * m_Unit);
        field("Lens", m_Lens);
        Space(s.sectionGap * m_Unit);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
        if (ImGui::BeginTable("Exposure", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("Left");
            ImGui::TableSetupColumn("Right");
            constexpr const char* labels[]{"ISO", "Shutter", "Aperture", "Focal length"};
            for (int i = 0; i < 4; ++i) {
                if (i % 2 == 0) ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(i % 2);
                if (i >= 2) Space(s.exposureRowGap * m_Unit);
                Line(labels[i], 12, s.labelHeight, m_Unit, m_Secondary);
                Space(s.labelGap * m_Unit);
                Line(m_Exposure[i].c_str(), 16, s.exposureHeight, m_Unit, m_Text);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        Space(s.sectionGap * m_Unit);
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0, 0));
        if (ImGui::BeginTable("File", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_NoSavedSettings)) {
            ImGui::TableSetupColumn("Format");
            ImGui::TableSetupColumn("Size");
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            field("Format", m_Format);
            ImGui::TableSetColumnIndex(1);
            field("Size", m_Size);
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        Space(4.f * m_Unit);
        if (Button("Location", "Open location", 2, actions.locationEnabled, ImVec2(-1, s.locationHeight * m_Unit), true))
            command = RawGalleryActions::Action::ShowInExplorer;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Reveal the displayed image or project in File Explorer");
    }
    return command;
}

std::optional<RawGalleryActions::Action> GalleryDetails::RenderEditActions(
    const GalleryInspectorActions& actions, bool stacked, bool different) {
    std::optional<RawGalleryActions::Action> command;
    const float headerY = ImGui::GetCursorPosY();
    ImGui::PushFont(nullptr, m_FontBase * 12.f / 16.f);
    const float scopeX = ImGui::GetWindowContentRegionMax().x - ImGui::CalcTextSize(m_SelectionScope.c_str()).x;
    ImGui::PopFont();
    ImGui::PushFont(nullptr, m_FontBase * (different ? 12.f : 13.f) / 16.f);
    if (different) {
        const auto at = ImGui::GetCursorScreenPos();
        const float width = std::max(1.f, scopeX - ImGui::GetCursorPosX() - 12.f * m_Unit);
        ImGui::Dummy(ImVec2(width, 18.f * m_Unit));
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), at,
            ImVec2(at.x + width, at.y + 18.f * m_Unit), at.x + width, m_FocusedName.c_str(), nullptr, nullptr);
    } else ImGui::TextUnformatted("Edits");
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::PushFont(nullptr, m_FontBase * 12.f / 16.f);
    ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), scopeX));
    ImGui::PushStyleColor(ImGuiCol_Text, m_Secondary);
    ImGui::TextUnformatted(m_SelectionScope.c_str());
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SetCursorPosY(headerY + 18.f * m_Unit);
    Space(6.f * m_Unit);
    const float buttonWidth = !actions.copyVisible || stacked ? ImGui::GetContentRegionAvail().x :
        (ImGui::GetContentRegionAvail().x - 8.f * m_Unit) * .5f;
    if (actions.copyVisible) {
        if (Button("Copy", ImGui::GetTime() < m_CopiedUntil ? "Copied" : "Copy edits", 0,
                actions.copyEnabled, ImVec2(buttonWidth, 36.f * m_Unit)))
            command = RawGalleryActions::Action::CopyEdits;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s", actions.copyEnabled ? m_FocusedName.c_str() : actions.copyReason);
        if (stacked) Space(8.f * m_Unit);
        else ImGui::SameLine(0, 8.f * m_Unit);
    }
    if (Button("Paste", "Paste edits", 1, actions.pasteEnabled, ImVec2(buttonWidth, 36.f * m_Unit)))
        command = RawGalleryActions::Action::PasteEdits;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", actions.pasteEnabled ? "Apply copied edits to the selection" : actions.pasteReason);
    return command;
}

} // namespace Stack::Editor::RawLabInternal
