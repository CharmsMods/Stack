#pragma once
#include "ThirdParty/json.hpp"
#include <string>
#include <map>
#include <vector>

namespace Raw::Bracketing {
inline constexpr int RecipeVersion = 6;
enum class ReconstructionMode { Standard, SuperResolution1x, SuperResolution2x, Panorama };
enum class PanoramaProjection { Auto, Perspective, Spherical };
struct PanoramaSettings {
    PanoramaProjection projection=PanoramaProjection::Auto;
    double outputScale=1.0;
    bool overlapDenoise=true;
};
const char* ReconstructionModeName(ReconstructionMode);
bool ParseReconstructionMode(const std::string&,ReconstructionMode&);
enum class AlignmentMode {
    AutomaticLocal,
    AutomaticGlobal,
    TranslationOnly,
    FixedCoordinates
};
const char* AlignmentModeName(AlignmentMode);
bool ParseAlignmentMode(const std::string&, AlignmentMode&);
struct Frame {
    std::string id;
    bool enabled = true;
    bool manualExposure = false;
    double relativeEv = 0;
};
struct Group {
    std::string id, name;
    bool enabled = true;
    std::vector<Frame> frames;
    int colorSlot=-1;
};
// Each ordinate vector is a simplex. Shared x handles keep every curve on
// the same Bezier parameter, preserving the sum between control points too.
struct Knot {
    double ev = 0;
    double incoming = 0, outgoing = 0;
    std::vector<double> share, left, right;
};
struct BracketingRecipe {
    int version = RecipeVersion;
    std::string originFrameId;
    std::vector<Group> groups;
    std::vector<Knot> knots;
    std::map<int, int> orientationOverrides;
    bool automatic = true;
    AlignmentMode alignmentMode = AlignmentMode::AutomaticLocal;
    ReconstructionMode reconstruction = ReconstructionMode::Standard;
    PanoramaSettings panorama;
};
int ResolveOrientation(const BracketingRecipe&, int original);
int ResolveStoredOrientation(const nlohmann::json&, int original);
bool UsesStoredFrame(const nlohmann::json&, const std::string& frameId);
std::vector<int> ColorSlots(const BracketingRecipe&);
bool Validate(const BracketingRecipe&, std::string& error, bool allowInactiveDraft = false);
nlohmann::json Serialize(const BracketingRecipe&);
bool Deserialize(const nlohmann::json&, BracketingRecipe&, std::string& error, bool allowInactiveDraft = false);
std::vector<double> Evaluate(const std::vector<Knot>&, double ev);
void Redistribute(std::vector<double>& shares, std::size_t selected, double value,
    const std::vector<double>& automatic = {});
std::vector<Knot> EqualCurves(std::size_t groups, double minimum = -12, double maximum = 8);
void InsertKnot(std::vector<Knot>&, double ev);
void RemapCurves(BracketingRecipe&, const std::vector<Group>& previousGroups);
void ConstrainEnabledCurves(BracketingRecipe&);
void EditContribution(const BracketingRecipe&,std::vector<double>&,std::size_t,double,const std::vector<double>&);
std::string Identity(const BracketingRecipe&);
} // namespace Raw::Bracketing
