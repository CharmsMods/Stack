#include "Notifications/AnimatedIndicatorLabel.h"
#include <imgui_internal.h>
#include <imstb_truetype.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace Stack::Notifications {
namespace {
constexpr double Duration = .22;
constexpr float EmptyDistance = -4.f;
float Mix(float a, float b, float t) { return a + (b - a) * t; }
std::vector<unsigned int> Characters(const std::string& text) {
    std::vector<unsigned int> result;
    const char* at = text.data();
    const char* end = at + text.size();
    while (at < end) {
        unsigned int character = 0;
        const int length = ImTextCharFromUtf8(&character, at, end);
        if (length <= 0) break;
        result.push_back(character);
        at += length;
    }
    return result;
}
bool HasInk(const std::vector<float>& field) {
    return std::any_of(field.begin(),field.end(),[](float distance) { return distance > 0; });
}
bool IsSpace(unsigned int cp) { return cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r'; }
}

float AnimatedIndicatorLabel::Amount(double now) const {
    const float t = static_cast<float>(std::clamp((now - m_Start) / Duration, 0., 1.));
    return t * t * (3.f - 2.f * t);
}

void AnimatedIndicatorLabel::UpdateContours() {
    for (auto& track : m_Tracks) {
        track.contourBias = 0;
        if (track.unchanged || m_Amount <= 0 || m_Amount >= 1) continue;
        // A fixed zero contour erodes dissimilar thin letters into empty space.
        // Preserve the interpolated ink area while the silhouette changes.
        std::array<int,1024> histogram{};
        int fromArea = 0, toArea = 0;
        constexpr float binWidth = 8.f / 1024.f;
        for (std::size_t i = 0; i < track.from.size(); ++i) {
            fromArea += track.from[i] > 0;
            toArea += track.to[i] > 0;
            // Saturated exterior is padding, not a candidate contour. In a
            // nearly empty glyph it otherwise dominates the lowest histogram bin.
            if (track.from[i] <= EmptyDistance && track.to[i] <= EmptyDistance) continue;
            const float distance = Mix(track.from[i],track.to[i],m_Amount);
            const int bin = std::clamp(static_cast<int>((distance + 4.f) / binWidth),0,1023);
            ++histogram[bin];
        }
        const float area = Mix(static_cast<float>(fromArea),static_cast<float>(toArea),m_Amount);
        if (area <= 0) continue; // Real spaces must remain spaces.
        int above = 0;
        for (int bin = 1023; bin >= 0; --bin) {
            if (above + histogram[bin] >= area) {
                const float fraction = (area - above) / std::max(1,histogram[bin]);
                track.contourBias = std::min(4.f - m_Cell,
                    4.f - (bin + 1.f - fraction) * binWidth);
                break;
            }
            above += histogram[bin];
        }
    }
}

const AnimatedIndicatorLabel::Glyph& AnimatedIndicatorLabel::GetGlyph(unsigned int codepoint) {
    if (const auto found = m_Glyphs.find(codepoint); found != m_Glyphs.end()) return found->second;
    Glyph value;
    value.field.assign(m_Columns * m_Rows, EmptyDistance);
    if (codepoint) {
        const auto* baked = m_Font->GetFontBaked(m_Size);
        const auto* glyph = const_cast<ImFontBaked*>(baked)->FindGlyph(static_cast<ImWchar>(codepoint));
        value.advance = glyph->AdvanceX;
        if (glyph->Visible && glyph->SourceIdx < m_Font->Sources.Size) {
            const auto* source = m_Font->Sources[glyph->SourceIdx];
            stbtt_fontinfo info{};
            const int offset = source->FontData ? stbtt_GetFontOffsetForIndex(
                static_cast<const unsigned char*>(source->FontData), source->FontNo) : -1;
            if (offset >= 0 && stbtt_InitFont(&info, static_cast<const unsigned char*>(source->FontData), offset)) {
                const float scale = stbtt_ScaleForPixelHeight(&info, m_Size / m_Cell * source->ExtraSizeScale);
                int w = 0, h = 0, x = 0, y = 0;
                unsigned char* sdf = stbtt_GetCodepointSDF(&info, scale, glyph->Codepoint,
                    static_cast<int>(std::ceil(4.f / m_Cell)), 128, 16.f * m_Cell, &w, &h, &x, &y);
                if (sdf) {
                    // Align the source outline to ImGui's glyph box, including font offsets.
                    int bx0, by0, bx1, by1;
                    stbtt_GetCodepointBitmapBox(&info, glyph->Codepoint, scale, scale, &bx0, &by0, &bx1, &by1);
                    const int startX = static_cast<int>(std::round((glyph->X0 - m_Origin) / m_Cell)) + x - bx0;
                    const int startY = static_cast<int>(std::round((glyph->Y0 - m_Origin) / m_Cell)) + y - by0;
                    for (int row = 0; row < h; ++row) for (int col = 0; col < w; ++col) {
                        const int dx = col + startX, dy = row + startY;
                        if (dx >= 0 && dx < m_Columns && dy >= 0 && dy < m_Rows)
                            value.field[dy * m_Columns + dx] = std::clamp((sdf[row * w + col] - 128.f) / 16.f,
                                EmptyDistance, -EmptyDistance);
                    }
                    stbtt_FreeSDF(sdf, info.userdata);
                }
            }
        }
    }
    // Bound session cache growth for arbitrary producer text.
    if (m_Glyphs.size() >= 256) m_Glyphs.clear();
    return m_Glyphs.emplace(codepoint, std::move(value)).first->second;
}

void AnimatedIndicatorLabel::Update(const std::string& text, double now, bool reducedMotion) {
    auto* font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    const float density = std::clamp(std::max(ImGui::GetIO().DisplayFramebufferScale.x,
        ImGui::GetIO().DisplayFramebufferScale.y), 1.f, 3.f);
    const bool changedFont = font != m_Font || size != m_Size || density != m_Density;
    if (changedFont) {
        m_Font = font; m_Size = size; m_Density = density;
        m_Cell = 1.f / (2.f * density);
        m_Origin = -4.f;
        m_Columns = static_cast<int>(std::ceil((size * 2.f + 8.f) / m_Cell));
        m_Rows = static_cast<int>(std::ceil((size * 1.6f + 8.f) / m_Cell));
        m_Glyphs.clear(); m_Tracks.clear(); m_Text.clear();
    }
    m_Amount = Amount(now);
    if (m_Amount >= 1.f) m_Tracks.clear();
    UpdateContours();
    if (text == m_Text && !changedFont) {
        if (reducedMotion) { m_Tracks.clear(); m_Amount = 1.f; }
        return;
    }
    const auto next = Characters(text);
    // Very long labels are clipped by the header. Avoid quadratic matching of arbitrary text.
    if (reducedMotion || changedFont || size > 64.f || next.size() > 128 || Characters(m_Text).size() > 128 || m_Tracks.size() > 256) {
        m_Text = text; m_Tracks.clear(); m_Amount = 1.f; return;
    }
    std::vector<Track> previous;
    if (!m_Tracks.empty()) {
        previous = std::move(m_Tracks);
        for (auto& track : previous) {
            for (std::size_t i = 0; i < track.from.size(); ++i)
                track.from[i] = track.from[i] <= EmptyDistance && track.to[i] <= EmptyDistance ? EmptyDistance :
                    std::clamp(Mix(track.from[i], track.to[i], m_Amount) + track.contourBias,-4.f,4.f);
            track.fromAdvance = Mix(track.fromAdvance, track.toAdvance, m_Amount);
            track.fromPresence = Mix(track.fromPresence, track.toPresence, m_Amount);
        }
        m_FromWidth = Mix(m_FromWidth, m_ToWidth, m_Amount);
    } else {
        float x = 0;
        for (auto cp : Characters(m_Text)) {
            const auto& glyph = GetGlyph(cp);
            Track track;
            track.codepoint = cp;
            track.from = glyph.field;
            track.fromAdvance = glyph.advance;
            previous.push_back(std::move(track));
            x += glyph.advance;
        }
        m_FromWidth = x;
    }
    std::vector<float> positions(next.size() + 1, 0.f);
    for (std::size_t j = 0; j < next.size(); ++j)
        positions[j + 1] = positions[j] + GetGlyph(next[j]).advance;
    m_ToWidth = positions.back();
    const std::size_t n = previous.size(), m = next.size();
    // Prefer direct replacements over removing and adding letters around a
    // distant match. Spaces retain their own layout rather than receiving ink.
    std::vector<int> costs((n + 1) * (m + 1), 0);
    const auto index = [m](std::size_t i, std::size_t j) { return i * (m + 1) + j; };
    const auto replacementCost = [&](std::size_t i, std::size_t j) {
        if (previous[i].codepoint == next[j]) return 0;
        return IsSpace(previous[i].codepoint) != IsSpace(next[j]) ? 3 : 1;
    };
    for (std::size_t i = 0; i <= n; ++i) costs[index(i,m)] = static_cast<int>(n-i);
    for (std::size_t j = 0; j <= m; ++j) costs[index(n,j)] = static_cast<int>(m-j);
    for (std::size_t i = n; i-- > 0;) for (std::size_t j = m; j-- > 0;)
        costs[index(i,j)] = std::min({replacementCost(i,j) + costs[index(i+1,j+1)],
            1 + costs[index(i+1,j)],1 + costs[index(i,j+1)]});
    const auto add = [&](std::size_t i, std::size_t j, bool old, bool target) {
        Track track;
        track.codepoint = target ? next[j] : 0;
        track.from = old ? previous[i].from : GetGlyph(track.codepoint).field;
        track.to = target ? GetGlyph(track.codepoint).field : track.from;
        track.fromAdvance = old ? previous[i].fromAdvance : 0;
        track.toAdvance = target ? GetGlyph(track.codepoint).advance : 0;
        track.fromPresence = old ? previous[i].fromPresence : 0;
        track.toPresence = target ? 1.f : 0.f;
        track.unchanged = target && track.from == track.to;
        const bool fromInk = HasInk(track.from), toInk = HasInk(track.to);
        // Keep actual spaces empty. Collapse the visible shape instead of
        // pushing a zero contour into a uniform empty field.
        if (fromInk && !toInk) { track.to = track.from; track.toPresence = 0; }
        else if (!fromInk && toInk) { track.from = track.to; track.fromPresence = 0; }
        m_Tracks.push_back(std::move(track));
    };
    for (std::size_t i = 0, j = 0; i < n || j < m;) {
        if (i < n && j < m && costs[index(i,j)] == replacementCost(i,j) + costs[index(i+1,j+1)]) {
            add(i,j,true,true); ++i; ++j;
        } else if (i < n && (j == m || costs[index(i,j)] == 1 + costs[index(i+1,j)])) {
            add(i,j,true,false); ++i;
        } else {
            add(i,j,false,true); ++j;
        }
    }
    m_Text = text; m_Start = now; m_Amount = 0;
}

void AnimatedIndicatorLabel::Draw(ImDrawList& draw, ImVec2 left, float right, ImU32 color) const {
    if (m_Tracks.empty()) {
        const auto size = m_Font->CalcTextSizeA(m_Size, FLT_MAX, 0, m_Text.c_str());
        draw.AddText(m_Font, m_Size, ImVec2(std::max(left.x,right-size.x),left.y),color,m_Text.c_str());
        return;
    }
    const float width = Mix(m_FromWidth,m_ToWidth,m_Amount);
    const float start = std::max(left.x,right-width);
    const unsigned int alpha = (color >> IM_COL32_A_SHIFT) & 255;
    float cursor = start;
    for (const auto& track : m_Tracks) {
        const float presence = Mix(track.fromPresence,track.toPresence,m_Amount);
        const float advance = Mix(track.fromAdvance,track.toAdvance,m_Amount);
        const float pen = cursor;
        cursor += advance;
        const float x = pen + m_Origin * presence;
        if (presence <= .001f || x + m_Columns * m_Cell * presence < left.x || x > right) continue;
        if (track.unchanged && presence == 1.f) {
            // RenderChar rounds positions to pixels. Use the same atlas quad with
            // fractional coordinates so retained letters move continuously.
            const auto* glyph = m_Font->GetFontBaked(m_Size)->FindGlyph(static_cast<ImWchar>(track.codepoint));
            if (glyph && glyph->Visible)
                draw.AddImage(m_Font->OwnerAtlas->TexRef,
                    ImVec2(x-m_Origin+glyph->X0,left.y+glyph->Y0),
                    ImVec2(x-m_Origin+glyph->X1,left.y+glyph->Y1),
                    ImVec2(glyph->U0,glyph->V0),ImVec2(glyph->U1,glyph->V1),color);
            continue;
        }
        for (int row = 0; row < m_Rows; ++row) {
            int run = 0;
            unsigned int last = 0;
            for (int col = 0; col <= m_Columns; ++col) {
                unsigned int coverage = 0;
                if (col < m_Columns) {
                    const int i = row * m_Columns + col;
                    if (track.from[i] > EmptyDistance || track.to[i] > EmptyDistance) {
                        const float distance = Mix(track.from[i],track.to[i],m_Amount) + track.contourBias;
                        coverage = static_cast<unsigned int>(std::round(std::clamp(distance / m_Cell + .5f,0.f,1.f) * alpha * presence));
                    }
                }
                if (coverage != last || col == m_Columns) {
                    if (last) draw.AddRectFilled(ImVec2(x+run*m_Cell*presence,left.y+m_Origin+row*m_Cell),
                        ImVec2(x+col*m_Cell*presence,left.y+m_Origin+(row+1)*m_Cell),
                        (color & ~IM_COL32_A_MASK) | (last << IM_COL32_A_SHIFT));
                    run = col; last = coverage;
                }
            }
        }
    }
}
} // namespace Stack::Notifications
