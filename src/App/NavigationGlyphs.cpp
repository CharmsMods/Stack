#include "NavigationRail.h"
#include <initializer_list>

namespace Stack::Navigation {

void DrawGlyph(ImDrawList* draw, Glyph glyph, ImVec2 minimum, float size, ImU32 color) {
    const float scale = size / 24.0f;
    const float stroke = 1.75f * scale;
    const auto p = [&](float x, float y) { return ImVec2(minimum.x + x * scale, minimum.y + y * scale); };
    const auto line = [&](float x, float y, float x2, float y2) {
        draw->AddLine(p(x, y), p(x2, y2), color, stroke);
        draw->AddCircleFilled(p(x, y), stroke * .5f, color, 8);
        draw->AddCircleFilled(p(x2, y2), stroke * .5f, color, 8);
    };
    switch (glyph) {
    case Glyph::Panel:
        draw->AddRect(p(3, 4), p(21, 20), color, 3 * scale, 0, stroke);
        line(9, 5, 9, 19);
        break;
    case Glyph::Mask:
        draw->AddCircle(p(12, 12), 8.f * scale, color, 32, stroke);
        // A filled semicircle reads as coverage without a tool-specific shape.
        draw->PathLineTo(p(12, 4));
        draw->PathArcTo(p(12, 12), 8.f * scale, -1.57079633f, 1.57079633f, 16);
        draw->PathFillConvex(color);
        break;
    case Glyph::Raw:
        line(5, 4, 5, 20); line(12, 4, 12, 20); line(19, 4, 19, 20);
        for (const ImVec2 center : {p(5, 9), p(12, 15), p(19, 7)}) {
            draw->AddRectFilled(ImVec2(center.x - 3 * scale, center.y - 2 * scale),
                ImVec2(center.x + 3 * scale, center.y + 2 * scale), color, 1.3f * scale);
        }
        break;
    case Glyph::Graph:
        line(7, 7, 17, 11); line(7, 18, 17, 13);
        draw->AddCircle(p(5, 6), 2.5f * scale, color, 20, stroke);
        draw->AddCircle(p(19, 12), 2.5f * scale, color, 20, stroke);
        draw->AddCircle(p(5, 19), 2.5f * scale, color, 20, stroke);
        break;
    case Glyph::Library:
        draw->AddRect(p(3, 4), p(8, 20), color, 1.5f * scale, 0, stroke);
        line(12, 5, 12, 20); line(17, 4, 21, 19); line(16, 5, 20, 4); line(20, 20, 23, 19);
        break;
    case Glyph::Queue:
        for (float y : {6.f, 12.f, 18.f}) {
            draw->AddCircleFilled(p(4, y), 1.2f * scale, color, 12);
            line(9, y, y == 18.f ? 17.f : 21.f, y);
        }
        break;
    case Glyph::Stack: {
        // Transparent vector tracing of the outlined S in Assets/Icons/Stack.png.
        // The brand's broad bowls and sloped terminals stay distinct from a font S.
        const auto b = [&](float x, float y) {
            return p(2.f + (x - 196.f) * 20.f / 630.f, 1.f + (y - 156.f) * 22.f / 712.f);
        };
        draw->PathLineTo(b(796, 374));
        draw->PathBezierCubicCurveTo(b(781, 232), b(693, 163), b(519, 163));
        draw->PathBezierCubicCurveTo(b(334, 154), b(232, 234), b(228, 358));
        draw->PathBezierCubicCurveTo(b(219, 493), b(321, 546), b(495, 585));
        draw->PathBezierCubicCurveTo(b(568, 602), b(607, 619), b(604, 659));
        draw->PathBezierCubicCurveTo(b(600, 697), b(564, 718), b(517, 716));
        draw->PathBezierCubicCurveTo(b(451, 714), b(420, 681), b(411, 611));
        draw->PathBezierCubicCurveTo(b(410, 605), b(407, 603), b(401, 604));
        draw->PathLineTo(b(211, 616));
        draw->PathBezierCubicCurveTo(b(205, 616), b(202, 620), b(204, 628));
        draw->PathBezierCubicCurveTo(b(216, 789), b(319, 860), b(512, 860));
        draw->PathBezierCubicCurveTo(b(709, 868), b(816, 773), b(817, 639));
        draw->PathBezierCubicCurveTo(b(821, 486), b(710, 434), b(505, 392));
        draw->PathBezierCubicCurveTo(b(454, 381), b(430, 366), b(432, 341));
        draw->PathBezierCubicCurveTo(b(434, 312), b(463, 298), b(497, 301));
        draw->PathBezierCubicCurveTo(b(550, 301), b(579, 324), b(589, 381));
        draw->PathBezierCubicCurveTo(b(589, 386), b(594, 388), b(600, 387));
        draw->PathLineTo(b(789, 375));
        draw->PathStroke(color, ImDrawFlags_Closed, 1.25f * scale);
        break;
    }
    }
}

} // namespace Stack::Navigation
