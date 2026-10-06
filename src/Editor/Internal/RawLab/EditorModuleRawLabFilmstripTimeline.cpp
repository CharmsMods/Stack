#include "Editor/EditorModule.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <limits>
#include <string>
#include <utility>

#include <imgui.h>

namespace {

bool ToLocalTime(std::int64_t timestamp, std::tm& result) {
    if (timestamp <= 0) return false;
    const std::time_t calendarTime = static_cast<std::time_t>(timestamp);
    return localtime_s(&result, &calendarTime) == 0;
}

std::string ClockLabel(const std::tm& localTime) {
    char value[16] {};
    const int hour = localTime.tm_hour % 12 == 0
        ? 12 : localTime.tm_hour % 12;
    std::snprintf(value, sizeof(value), "%d:%02d %s",
        hour, localTime.tm_min, localTime.tm_hour < 12 ? "AM" : "PM");
    return value;
}

std::string DateTimeLabel(std::int64_t timestamp, bool compact = false) {
    std::tm localTime {};
    if (!ToLocalTime(timestamp, localTime)) return "Date unknown";
    char date[40] {};
    std::strftime(date, sizeof(date),
        compact ? "%b %d" : "%a, %b %d, %Y", &localTime);
    return std::string(date) + "  " + ClockLabel(localTime);
}

std::string ShortDateLabel(const std::tm& localTime) {
    char date[32] {};
    std::strftime(date, sizeof(date), "%a, %b %d", &localTime);
    return date;
}

std::pair<std::string, std::string> HoverDateTimeLines(
    std::int64_t firstTimestamp,
    std::int64_t lastTimestamp,
    std::size_t imageCount) {
    std::tm firstTime {};
    if (!ToLocalTime(firstTimestamp, firstTime)) {
        return { "Date unknown", {} };
    }
    std::string date = ShortDateLabel(firstTime);
    if (imageCount > 1u) {
        date = std::to_string(imageCount) + " images | " + date;
    }
    std::string time = ClockLabel(firstTime);
    std::tm lastTime {};
    if (lastTimestamp > firstTimestamp &&
        ToLocalTime(lastTimestamp, lastTime)) {
        if (firstTime.tm_year != lastTime.tm_year ||
            firstTime.tm_yday != lastTime.tm_yday) {
            char firstDate[16] {};
            char lastDate[16] {};
            std::strftime(firstDate, sizeof(firstDate), "%b %d", &firstTime);
            std::strftime(lastDate, sizeof(lastDate),
                firstTime.tm_year == lastTime.tm_year &&
                    firstTime.tm_mon == lastTime.tm_mon
                    ? "%d" : "%b %d", &lastTime);
            date = (imageCount > 1u
                ? std::to_string(imageCount) + " images | "
                : std::string()) + firstDate + "-" + lastDate;
        }
        time += " to " + ClockLabel(lastTime);
    }
    return { std::move(date), std::move(time) };
}

std::string EntryDateTimeLabel(
    std::int64_t firstTimestamp,
    std::int64_t lastTimestamp,
    std::size_t imageCount) {
    std::string label;
    if (imageCount > 1u) {
        label = std::to_string(imageCount) + " images  |  ";
    }
    label += DateTimeLabel(firstTimestamp);
    if (lastTimestamp > firstTimestamp) {
        std::tm firstTime {};
        std::tm lastTime {};
        const bool sameDay = ToLocalTime(firstTimestamp, firstTime) &&
            ToLocalTime(lastTimestamp, lastTime) &&
            firstTime.tm_year == lastTime.tm_year &&
            firstTime.tm_yday == lastTime.tm_yday;
        label += " to ";
        label += sameDay
            ? ClockLabel(lastTime)
            : DateTimeLabel(lastTimestamp);
    }
    return label;
}

} // namespace

void EditorModule::RenderRawWorkspaceLabFilmstripTimeline(
    float scrollX, float scrollMaxX, float viewportWidth) {
    constexpr float height = 54.0f;
    constexpr float tileWidth = 124.0f;
    constexpr float gap = 10.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = std::max(1.0f, ImGui::GetContentRegionAvail().x);
    ImGui::Dummy(ImVec2(width, 32.0f));
    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + 32.0f));
    ImGui::InvisibleButton("##RawLabFilmstripTimeline", ImVec2(width, height - 32.0f));
    if (m_RawWorkspaceLabFilmstripTimelineEntries.empty()) return;

    const bool trackHovered = ImGui::IsItemHovered();
    const bool dragging = ImGui::IsItemActive();
    if ((trackHovered || dragging) && scrollMaxX > 0.0f) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    }
    const float left = origin.x + 10.0f;
    const float right = std::max(left + 1.0f, origin.x + width - 10.0f);
    const float span = std::max(1.0f, right - left);
    const float rulerY = origin.y + 27.0f;
    const float trackTop = origin.y + 40.0f;
    const float trackBottom = origin.y + 45.0f;
    const float trackCenterY = (trackTop + trackBottom) * 0.5f;
    const float scaledTileWidth =
        tileWidth * m_RawWorkspaceLabUi.galleryThumbnailScale;
    const float tileStep = scaledTileWidth + gap;
    const float contentOffset =
        m_RawWorkspaceLabFilmstripTimelineContentOriginX - origin.x;
    const float itemContentWidth = contentOffset +
        static_cast<float>(m_RawWorkspaceLabFilmstripTimelineEntries.size() - 1u) *
            tileStep + scaledTileWidth;
    const float contentWidth = std::max({
        1.0f, viewportWidth, viewportWidth + scrollMaxX, itemContentWidth
    });
    const auto imageCenterX = [&](std::size_t index) {
        return origin.x + contentOffset +
            static_cast<float>(index) * tileStep +
            scaledTileWidth * 0.5f - scrollX;
    };

    ImDrawList* draw = ImGui::GetWindowDrawList();
    const ImU32 trackFill = ImGui::GetColorU32(ImGuiCol_Text, 0.08f);
    const ImU32 thumbFill = ImGui::GetColorU32(ImGuiCol_Text, dragging ? 0.50f : trackHovered ? 0.40f : 0.28f);
    const ImU32 pointColor = ImGui::GetColorU32(ImGuiCol_Text, 0.55f);
    const ImU32 selectedPointColor =
        ImGui::GetColorU32(ImGuiCol_Text, 0.95f);
    draw->AddLine(
        ImVec2(left, rulerY), ImVec2(right, rulerY),
        ImGui::GetColorU32(ImGuiCol_Border), 1.0f);
    draw->AddRectFilled(
        ImVec2(left, trackTop), ImVec2(right, trackBottom),
        trackFill, 3.0f);

    const float visibleFraction = std::clamp(
        viewportWidth / contentWidth, 0.0f, 1.0f);
    const float thumbWidth = scrollMaxX > 0.0f
        ? std::min(span, std::max(32.0f, span * visibleFraction))
        : span;
    const float thumbTravel = std::max(0.0f, span - thumbWidth);
    const float thumbX = left + (scrollMaxX > 0.0f
        ? std::clamp(scrollX / scrollMaxX, 0.0f, 1.0f) * thumbTravel
        : 0.0f);
    draw->AddRectFilled(
        ImVec2(thumbX, trackCenterY - 3.5f),
        ImVec2(thumbX + thumbWidth, trackCenterY + 3.5f),
        thumbFill, 3.5f);
    const ImGuiID dragOffsetId = ImGui::GetID("RawFilmstripScrollGrabOffset");
    if (ImGui::IsItemActivated()) {
        const float mouseX = ImGui::GetIO().MousePos.x;
        ImGui::GetStateStorage()->SetFloat(dragOffsetId,
            mouseX >= thumbX && mouseX <= thumbX + thumbWidth ? mouseX - thumbX : thumbWidth * 0.5f);
    }
    if (dragging && scrollMaxX > 0.0f) {
        const float targetFraction = std::clamp(
            (ImGui::GetIO().MousePos.x - left - ImGui::GetStateStorage()->GetFloat(dragOffsetId)) /
                std::max(1.0f, thumbTravel),
            0.0f, 1.0f);
        m_RawWorkspaceLabFilmstripScrollTargetX =
            targetFraction * scrollMaxX;
    }

    std::size_t hoveredIndex =
        m_RawWorkspaceLabFilmstripTimelineHoveredIndex;
    if (hoveredIndex >= m_RawWorkspaceLabFilmstripTimelineEntries.size()) {
        hoveredIndex = std::numeric_limits<std::size_t>::max();
    }
    bool hoveredPoint = false;
    if (ImGui::IsWindowHovered() && !dragging &&
        std::abs(ImGui::GetIO().MousePos.y - rulerY) <= 5.0f) {
        float nearestDistance = 7.0f;
        for (std::size_t index = 0;
             index < m_RawWorkspaceLabFilmstripTimelineEntries.size();
             ++index) {
            const float distance = std::abs(
                ImGui::GetIO().MousePos.x - imageCenterX(index));
            if (distance < nearestDistance) {
                nearestDistance = distance;
                hoveredIndex = index;
                hoveredPoint = true;
            }
        }
    }

    for (std::size_t index = 0;
         index < m_RawWorkspaceLabFilmstripTimelineEntries.size();
         ++index) {
        const bool highlighted = index == hoveredIndex;
        const float alignedX = imageCenterX(index);
        if (alignedX >= left && alignedX <= right) {
            draw->AddCircleFilled(
                ImVec2(alignedX, rulerY),
                highlighted ? 3.0f : 2.0f,
                highlighted ? selectedPointColor : pointColor,
                12);
        }
    }

    const bool labelHovered = hoveredIndex < m_RawWorkspaceLabFilmstripTimelineEntries.size();
    const float labelStep = std::clamp(ImGui::GetIO().DeltaTime / 0.18f, 0.0f, 1.0f);
    if (m_FilmstripDateLastFrame + 1 != ImGui::GetFrameCount()) m_FilmstripDateLabels.clear();
    m_FilmstripDateLastFrame = ImGui::GetFrameCount();
    if (labelHovered) {

        const auto& entry =
            m_RawWorkspaceLabFilmstripTimelineEntries[hoveredIndex];
        std::int64_t firstTimestamp = entry.firstTimestamp;
        std::int64_t lastTimestamp = entry.lastTimestamp;
        std::size_t imageCount = entry.imageCount;
        if (!hoveredPoint &&
            m_RawWorkspaceLabFilmstripTimelineHoveredStackMember) {
            const auto* source = FindRawWorkspaceSourceByKey(
                m_RawWorkspaceLabFilmstripTimelineHoveredSourceKey);
            if (source != nullptr) {
                firstTimestamp = source->captureTimestamp > 0
                    ? source->captureTimestamp
                    : source->modifiedUnixSeconds;
                lastTimestamp = firstTimestamp;
                imageCount = 1u;
            }
        }
        const auto [dateLine, timeLine] = HoverDateTimeLines(
            firstTimestamp, lastTimestamp, imageCount);
        const float padding = std::min(scaledTileWidth * 0.5f, span * 0.5f);
        const float centerX = hoveredPoint
            ? imageCenterX(hoveredIndex)
            : std::clamp(m_RawWorkspaceLabFilmstripTimelineHoveredCenterX,
                left + padding, right - padding);
        if (m_FilmstripDateLabels.empty()) m_FilmstripDateCenterX = centerX;
        m_FilmstripDateCenterX += (centerX - m_FilmstripDateCenterX) *
            (1.0f - std::exp(-18.0f * std::max(0.0f, ImGui::GetIO().DeltaTime)));
        auto match = std::find_if(m_FilmstripDateLabels.begin(), m_FilmstripDateLabels.end(),
            [&](const auto& label) { return label.date == dateLine && label.time == timeLine; });
        if (match == m_FilmstripDateLabels.end()) {
            m_FilmstripDateLabels.push_back({dateLine, timeLine, 0.0f});
        }
        for (auto& label : m_FilmstripDateLabels) {
            const bool current = label.date == dateLine && label.time == timeLine;
            label.opacity = std::clamp(label.opacity + (current ? labelStep : -labelStep), 0.0f, 1.0f);
        }
        if (hoveredPoint) {
            ImGui::BeginTooltip();
            ImGui::TextUnformatted(entry.label.c_str());
            const std::string fullLabel = EntryDateTimeLabel(
                entry.firstTimestamp, entry.lastTimestamp,
                entry.imageCount);
            ImGui::TextUnformatted(fullLabel.c_str());
            ImGui::EndTooltip();
        }
    } else {
        for (auto& label : m_FilmstripDateLabels) label.opacity = std::max(0.0f, label.opacity - labelStep);
    }
    m_FilmstripDateLabels.erase(std::remove_if(m_FilmstripDateLabels.begin(), m_FilmstripDateLabels.end(),
        [](const auto& label) { return label.opacity <= 0.0f; }), m_FilmstripDateLabels.end());
    ImFont* font = ImGui::GetFont();
    const float fontSize = std::clamp(ImGui::GetFontSize() * 0.78f, 10.0f, 11.0f);
    float hoverOpacity = 0.0f;
    for (const auto& label : m_FilmstripDateLabels) {
        hoverOpacity += label.opacity;
        auto color = ImGui::GetStyleColorVec4(ImGuiCol_Text);
        color.w *= label.opacity;
        const auto drawLine = [&](const std::string& line, float y) {
            const float textWidth = font->CalcTextSizeA(fontSize,
                std::numeric_limits<float>::max(), 0.0f, line.c_str()).x;
            const float x = std::clamp(m_FilmstripDateCenterX - textWidth * 0.5f,
                left, std::max(left, right - textWidth));
            draw->AddText(font, fontSize, ImVec2(x, y), ImGui::GetColorU32(color), line.c_str());
        };
        drawLine(label.date, origin.y + 1.0f);
        drawLine(label.time, origin.y + fontSize + 2.0f);
    }
    if (!labelHovered) {
        ImVec4 idleColor = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
        idleColor.w *= 1.0f - std::min(1.0f, hoverOpacity);
        float lastLabelRight = left - 1000.0f;
        int previousDay = std::numeric_limits<int>::min();
        for (std::size_t index = 0;
             index < m_RawWorkspaceLabFilmstripTimelineEntries.size();
             ++index) {
            const auto& entry =
                m_RawWorkspaceLabFilmstripTimelineEntries[index];
            const float itemX = imageCenterX(index);
            if (itemX < left || itemX > right) continue;
            std::tm localTime {};
            if (!ToLocalTime(entry.firstTimestamp, localTime)) continue;
            const int day = localTime.tm_year * 366 + localTime.tm_yday;
            if (day == previousDay) continue;
            previousDay = day;
            const std::string label =
                DateTimeLabel(entry.firstTimestamp, true);
            const float labelWidth = ImGui::CalcTextSize(label.c_str()).x;
            const float x = std::clamp(
                itemX - labelWidth * 0.5f,
                left, std::max(left, right - labelWidth));
            if (x <= lastLabelRight + 8.0f) continue;
            draw->AddText(
                ImVec2(x, origin.y + 3.0f),
                ImGui::GetColorU32(idleColor),
                label.c_str());
            lastLabelRight = x + labelWidth;
        }
    }
}
