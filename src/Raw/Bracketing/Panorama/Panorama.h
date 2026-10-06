#pragma once
#include "../Recipe.h"
#include <array>
#include <memory>

namespace Raw::Bracketing {
struct ProcessingRequest;
struct BracketingResult;
struct Preview;
struct CapturePreview;
namespace Panorama {
inline constexpr const char* Revision="panorama-v1";
struct Camera {
    std::string frameId,name;
    unsigned width=0,height=0;
    double focal=1,cx=0,cy=0,k1=0,k2=0,gain=1;
    std::array<double,9> rotation{1,0,0,0,1,0,0,0,1};
};
struct Layout {
    PanoramaProjection projection=PanoramaProjection::Perspective;
    unsigned width=0,height=0;
    double scale=1,offsetX=0,offsetY=0;
    std::vector<Camera> cameras;
    std::shared_ptr<const std::vector<std::uint16_t>> ownership;
};
struct Prepared;
nlohmann::json SerializeLayout(const Layout&);
std::shared_ptr<const Layout> DeserializeLayout(const nlohmann::json&);
std::string PreparationIdentity(const ProcessingRequest&);
BracketingResult Process(const ProcessingRequest&);
Preview NativeDetail(const BracketingResult&,unsigned,unsigned,unsigned);
std::shared_ptr<const CapturePreview> Inspect(const ProcessingRequest&,const BracketingResult&,const std::string&,unsigned);
}
}
