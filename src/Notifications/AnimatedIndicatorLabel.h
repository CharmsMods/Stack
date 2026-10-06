#pragma once

#include <imgui.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::Notifications {

// Presentation-only state. Fields are cached at the current font and raster scale.
class AnimatedIndicatorLabel {
public:
    void Update(const std::string& text, double now, bool reducedMotion);
    void Draw(ImDrawList& draw, ImVec2 left, float right, ImU32 color) const;
    bool Visible() const { return !m_Text.empty() || !m_Tracks.empty(); }

private:
    struct Glyph { std::vector<float> field; float advance = 0; };
    struct Track {
        unsigned int codepoint = 0;
        std::vector<float> from, to;
        float fromAdvance = 0, toAdvance = 0;
        float fromPresence = 1, toPresence = 1;
        float contourBias = 0;
        bool unchanged = false;
    };
    const Glyph& GetGlyph(unsigned int codepoint);
    float Amount(double now) const;
    void UpdateContours();
    ImFont* m_Font = nullptr;
    float m_Size = 0, m_Density = 0, m_Cell = 1, m_Origin = 0;
    int m_Columns = 0, m_Rows = 0;
    std::string m_Text;
    std::unordered_map<unsigned int, Glyph> m_Glyphs;
    std::vector<Track> m_Tracks;
    float m_FromWidth = 0, m_ToWidth = 0, m_Amount = 1;
    double m_Start = 0;
};

} // namespace Stack::Notifications
