#pragma once
#include "Project/BracketingState.h"
#include "ProcessingPresentation.h"
#include "Notifications/AsyncActivity.h"
#include <imgui.h>
#include <array>
#include <limits>
#include <unordered_map>

namespace Stack::Editor {
using Project::BracketingDraftSource;
using Project::BracketingMetadataJob;
using Project::BracketingJob;
using Project::BracketingInputsChanged;
using Project::MakeBracketingInteractiveRaw;

// The editor adds presentation to one project's processing state. The base
// remains usable without constructing any editor or UI.
struct BracketingSession : Project::BracketingState {
    // Presentation preparation is invalidated by accepted recipe changes, not
    // camera movement, a floating-tool drag, or an unchanged visible frame.
    std::uint64_t controlsRevision = 0;
    std::uint64_t metadataCheckedRevision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t observedDocumentRevision = std::numeric_limits<std::uint64_t>::max();
    std::uint64_t observedInputRevision = std::numeric_limits<std::uint64_t>::max();
    std::string observedStoredRecipe;
    struct ControlsCache {
        std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
        bool hasContributions = false;
        std::vector<ImVec4> colors;
        std::unordered_map<std::string, std::size_t> sourceIndices;
        std::vector<std::string> readingCaptions;
    } controls;
    struct CurvePlotCache {
        std::uint64_t revision = std::numeric_limits<std::uint64_t>::max();
        std::weak_ptr<const Raw::Bracketing::BracketingAnalysis> analysis;
        float minimum = 0, maximum = 0;
        bool automatic = false;
        std::vector<Raw::Bracketing::Knot> suggestion;
        std::array<std::vector<double>, 257> visible, suggested;
    } curvePlot;
    bool selectionRestored = false, synchronizing = false;
    bool gradedResult = true;
    bool orientationReviewRequested = false;
    bool processAfterOrientationReview = true;
    std::map<int, int> orientationChoices;
    std::shared_ptr<ProcessingPresentation> presentation;
    Notifications::Notifier activityNotifier;
    Notifications::ActivityHandle activity;
    std::shared_ptr<Notifications::AsyncActivityCompletion> activityLease;
    bool activityMeaningful = true;
    double inspectionChangedAt=0;
    bool detailMode=false,requestDetail=false,showGroups=true;
    int selectedGroup=0,selectedPoint=-1,selectedHandle=0,view=0;
    float viewMin=-12,viewMax=8,zoom=1,panX=0,panY=0;
    ImVec2 curveOrigin{},curveSize{};
    float probeX=.5f,probeY=.5f;
    unsigned texture=0;
    std::string textureIdentity;
    std::string selectedFrame,hoverFrame;
    std::map<std::string,std::string> frameLabels;
    float inspectionEv=0,compareSplit=.5f;
    bool showClipping=false,selectedMask=false;
    ~BracketingSession();
};
ImVec4 BracketColor(const Raw::Bracketing::BracketingRecipe&,std::size_t group);
void PrepareBracketingControls(BracketingSession&);
bool DrawBracketingContributionTool(BracketingSession&);
bool DrawBracketingCurves(BracketingSession&,const ImVec2&);
void DrawBracketingPreview(BracketingSession&,const ImVec2&);
std::shared_ptr<const Raw::Bracketing::CapturePreview> UpdateBracketingInspection(BracketingSession&,int view,const std::string& frame);
bool NeedsBracketingOrientationReview(const BracketingSession&);
void RegroupBracketingDraft(BracketingSession&);
} // namespace Stack::Editor
