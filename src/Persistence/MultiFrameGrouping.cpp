#include "Persistence/MultiFrameGraph.h"
#include "Persistence/RawProjectModel.h"
#include "Raw/MultiFrameHdr/Contracts.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Stack::Project {
bool BuildExposureGroupedMultiFrameGraph(const RawProjectSnapshot& snapshot,
    const MultiFrameSourceSet& set, MultiFrameGraphDocument& output, std::string* error) {
    const auto fail = [&](const char* message) { if (error) *error = message; return false; };
    struct Capture { std::string frameId, nodeId; double ev; RawCaptureCompatibilitySummary metadata; };
    auto graph = BuildManualMultiFrameGraph(snapshot, set);
    std::vector<Capture> captures;
    for (const auto& frame : set.frames) {
        if (!frame.enabled) continue;
        const auto* asset = FindEmbeddedAsset(snapshot, frame.assetId);
        RawCaptureCompatibilitySummary metadata;
        if (!asset || !DeserializeRawCaptureCompatibilitySummary(asset->captureMetadataSummary, metadata, nullptr) ||
            !std::isfinite(metadata.exposureTimeSeconds) || metadata.exposureTimeSeconds <= 0)
            return fail("Exposure grouping needs shutter metadata for each enabled capture.");
        std::string nodeId;
        for (const auto& node : graph.nodes) if (node.frameIds == std::vector<std::string>{frame.frameId}) nodeId = node.nodeId;
        const double aperture = metadata.apertureFNumber > 0 ? metadata.apertureFNumber : 1.0;
        const double iso = metadata.isoSpeed > 0 ? metadata.isoSpeed : 100.0;
        const double exposure = metadata.exposureTimeSeconds * iso / (aperture * aperture);
        captures.push_back({frame.frameId, nodeId, std::log2(exposure), metadata});
    }
    if (captures.size() < 2 || captures.size() > 30) return fail("Group between 2 and 30 enabled RAW captures.");
    const auto& first = captures.front().metadata;
    for (const auto& c : captures) {
        const auto& m = c.metadata;
        if (m.cameraMake != first.cameraMake || m.cameraModel != first.cameraModel ||
            m.cfaPattern != first.cfaPattern || m.visibleWidth != first.visibleWidth || m.visibleHeight != first.visibleHeight ||
            m.lensModel != first.lensModel ||
            (m.isoSpeed > 0 && first.isoSpeed > 0 && std::abs(std::log2(m.isoSpeed / first.isoSpeed)) > 0.05))
            return fail("Automatic grouping requires the same camera, CFA, dimensions, lens, and ISO.");
    }
    std::sort(captures.begin(), captures.end(), [](const auto& a, const auto& b) { return a.ev < b.ev; });
    std::vector<std::vector<Capture>> groups;
    for (const auto& c : captures) {
        if (groups.empty() || c.ev - groups.back().front().ev > 0.15) groups.emplace_back();
        groups.back().push_back(c);
    }
    if (groups.size() > Raw::Hdr::kMaximumFrameCount) return fail("HDR Fusion supports at most 20 exposure groups.");
    const double originEv = groups[groups.size() / 2].front().ev;
    const auto link = [&](const std::string& from, const std::string& to, bool capture, std::uint32_t order) {
        graph.links.push_back({GenerateStableUuid(), from, capture ? "measurements" : "estimate", to,
            "measurements", capture ? MultiFrameGraphResourceType::RawMeasurementSet : MultiFrameGraphResourceType::RawMeasurement, order});
    };
    std::vector<std::pair<std::string, bool>> outputs;
    double y = 54;
    for (const auto& group : groups) {
        const double midpoint = y + (group.size() - 1) * 74;
        for (const auto& capture : group) {
            if (auto* node = FindMultiFrameGraphNode(graph, capture.nodeId)) { node->positionX = 48; node->positionY = y; }
            y += 148;
        }
        if (group.size() == 1) outputs.emplace_back(group.front().nodeId, true);
        else {
            MultiFrameGraphNode burst;
            burst.nodeId = GenerateStableUuid(); burst.kind = MultiFrameGraphNodeKind::BurstDenoise;
            char title[80]; std::snprintf(title, sizeof(title), "Burst Denoise %+.2f EV", group.front().ev - originEv);
            burst.title = title; burst.positionX = 380; burst.positionY = midpoint;
            burst.settings = {{"alignmentMode", "identity"}, {"evidencePolicy", "disjoint-original-evidence-v1"}};
            const auto id = burst.nodeId; graph.nodes.push_back(std::move(burst));
            for (std::size_t i = 0; i < group.size(); ++i) link(group[i].nodeId, id, true, static_cast<std::uint32_t>(i));
            outputs.emplace_back(id, false);
        }
        y += 36;
    }
    std::string terminal = outputs.front().first;
    if (outputs.size() > 1) {
        MultiFrameGraphNode hdr;
        hdr.nodeId = GenerateStableUuid(); hdr.kind = MultiFrameGraphNodeKind::HdrMerge; hdr.title = "HDR Fusion";
        hdr.positionX = 720; hdr.positionY = (y - 148) / 2;
        Raw::Hdr::Parameters parameters; parameters.alignmentMode = Raw::Hdr::AlignmentMode::Identity;
        hdr.settings = {{"parameters", Raw::Hdr::SerializeParameters(parameters)}, {"evidencePolicy", "disjoint-original-evidence-v1"}};
        terminal = hdr.nodeId; graph.nodes.push_back(std::move(hdr));
        for (std::size_t i = 0; i < outputs.size(); ++i) link(outputs[i].first, terminal, outputs[i].second, static_cast<std::uint32_t>(i));
    }
    link(terminal, graph.outputNodeId, false, 0);
    if (auto* node = FindMultiFrameGraphNode(graph, graph.outputNodeId)) { node->positionX = outputs.size() > 1 ? 1040 : 720; node->positionY = (y - 148) / 2; }
    graph.userEdited = true;
    graph.automaticDecisionSnapshot = {{"source", "capture-exposure-grouping-v2"}, {"toleranceEv", 0.15}, {"groups", groups.size()}};
    const auto validation = ValidateMultiFrameGraph(graph, snapshot, true);
    if (!validation.valid) { if (error) *error = validation.errors.front(); return false; }
    output = std::move(graph); if (error) error->clear(); return true;
}
} // namespace Stack::Project
