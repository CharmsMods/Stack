#pragma once
#include "Raw/RawAssetCapture.h"

namespace Stack::MaskAuthoring {
using Point = std::array<float,2>;
enum class Tool { Values, Brush, Erase, Radial, Linear, Rectangle, Circle, Edge };
enum class Role { Include, Protect, Transition };
struct Mark {
    Tool tool = Tool::Brush;
    Role role = Role::Protect;
    std::vector<Point> points;
    float radius = .035f, feather = .4f;
    std::vector<std::array<float,3>> samples;
};
struct Recipe {
    std::vector<Mark> marks;
    bool wholeImage = false;
};
nlohmann::json Serialize(const Recipe&);
Recipe Deserialize(const nlohmann::json&);
void CaptureEvidence(const RawAssetCapture::PreviewField&,Recipe&,const std::function<bool()>& canceled = {});
Assets::Record Evaluate(const RawAssetCapture::PreviewField&, const Recipe&, const std::function<bool()>& canceled = {});
}

