#include "Editor/EditorModule.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace {

std::string FormatActivityBytes(std::uint64_t bytes) {
    static constexpr const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < 5) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream text;
    text << std::fixed << std::setprecision(unit >= 3 ? 1 : 0)
         << value << ' ' << units[unit];
    return text.str();
}

} // namespace

void EditorModule::CollectActivity(Stack::UiActivity::Snapshot& snapshot) const {
    snapshot.SetAwaitingInteractionRelease(m_RawWorkspaceRootTabActive &&
        m_Project->rawPipelineActive && IsRawWorkspaceUiInteractionActive());
    snapshot.Add(IsExportBusy(), "Exporting");
    snapshot.Add(m_Project->saveCheckBusy, "Checking Save");
    snapshot.Add(!m_Project->saveCheckBusy &&
        (IsProjectFileSaveBusy() || IsRawWorkspaceProjectSaveBusy()), "Saving");
    snapshot.Add(IsGraphDropImportBusy(), "Importing images");
    snapshot.Add(IsSourceLoadBusy(), "Loading image");
    snapshot.Add(IsRawWorkspaceProjectLoadBusy() ||
        Async::IsBusy(GetProjectLoadTaskState()), "Loading project");
    snapshot.Add(IsMfdExperimentalProcessingBusy(), "Denoising");
    snapshot.Add(IsHdrProcessingBusy(), "Merging HDR");
    snapshot.Add(IsMultiFrameGraphProcessingBusy(), "Processing frames");
    snapshot.Add(m_GraphCaptureBusy, "Capturing graph");
    snapshot.Add(Async::IsBusy(m_RawEditAttributePasteTaskState), "Applying edits");
    snapshot.Add(IsEditorRenderBusy() && IsRawWorkspaceUiInteractionActive(), "Rendering preview");
    snapshot.Add(m_RawViewportMaintenanceGeneration != 0, m_RawViewportMaintenanceIsOverview
        ? "Refreshing navigation preview" : "Preparing viewport resolution");
    const std::string calibrationStatus = GetRawViewportCalibrationStatus();
    snapshot.AddOperation(IsRawViewportCalibrationBusy(), "viewport-calibration", calibrationStatus);
    snapshot.Add(IsEditorRenderBusy() && !IsRawViewportCalibrationBusy() && m_RawViewportMaintenanceGeneration == 0, m_RawWorkspaceRootTabActive &&
        m_Project->rawPipelineActive &&
        (IsRawWorkspaceUiInteractionActive() || m_ActivityRawRenderIsProxy)
            ? "Rendering proxy" : "Full Resolution");
    // Refinement can be queued behind the release debounce before a backend
    // accepts it. Keep that handoff from briefly claiming completion.
    snapshot.Add(m_RawWorkspaceRootTabActive && m_Project->rawPipelineActive &&
        !IsRawWorkspaceUiInteractionActive() &&
        m_RawWorkspaceFullResolutionPreviewPending &&
        !m_RawWorkspaceFullResolutionPreviewDeferredByBudget, "Full Resolution");
    snapshot.AddMaintenance(m_ProjectCoverEncodeInFlight, "Updating preview");
    const RawWorkspaceScanSnapshot scan = GetRawWorkspaceScanSnapshot();
    const RawWorkspaceThumbnailSnapshot thumbnails =
        GetRawWorkspaceThumbnailSnapshot();
    const bool scanBusy = Async::IsBusy(scan.state);
    const bool thumbnailBusy = Async::IsBusy(thumbnails.state);
    const bool groupingBusy = m_RawWorkspaceSimilarityWorkerActive ||
        m_RawWorkspaceSimilarityRebuildPending;
    if (scan.state == Async::TaskState::Failed && scan.sourcesPublished &&
        !scan.errorMessage.empty()) {
        snapshot.AddDetail(
            "Folder load incomplete: " + scan.errorMessage);
    }

    int quickAvailable = 0;
    int standardAvailable = 0;
    for (const auto& source : m_RawWorkspace.sources) {
        const bool standard = source.thumbnail.status ==
                Stack::RawWorkspace::ThumbnailStatus::Valid ||
            source.thumbnail.status ==
                Stack::RawWorkspace::ThumbnailStatus::Ready;
        const bool quick = standard || source.transientThumbnail.status ==
            Stack::RawWorkspace::ThumbnailStatus::Ready;
        quickAvailable += quick ? 1 : 0;
        standardAvailable += standard ? 1 : 0;
    }
    const int sourceCount = std::max(
        scan.progress.discoveredRawCount,
        static_cast<int>(m_RawWorkspace.sources.size()));

    snapshot.AddOperation(scanBusy || thumbnailBusy || groupingBusy, "raw-preparation", "Preparing RAW photos");
    std::vector<std::string> compactStages;
    if (scanBusy) {
        compactStages.push_back(scan.progress.verifiedRawBytes > 0
            ? "Hash " + FormatActivityBytes(
                scan.progress.verifiedRawBytes)
            : "Check " + std::to_string(scan.progress.filesVisited));
        std::ostringstream detail;
        detail << "Scanning folder: " << scan.progress.filesVisited
               << " files checked, "
               << FormatActivityBytes(scan.progress.verifiedRawBytes)
               << " verified, " << scan.progress.discoveredRawCount
               << " RAW files found, "
               << scan.progress.reusedSourceIdentityCount
               << " identities reused, "
               << scan.progress.hashedRawCount << " RAW files hashed";
        snapshot.AddOperationDetail("raw-preparation", detail.str());

        if (scan.progress.cachedPreviewsChecked > 0 || sourceCount > 0) {
            snapshot.AddOperationDetail("raw-preparation",
                "Checking saved previews: " +
                std::to_string(scan.progress.cachedPreviewsChecked) +
                " of " + std::to_string(sourceCount));
        }
        if (sourceCount > 0) {
            compactStages.push_back(
                "Quick " + std::to_string(quickAvailable) + "/" +
                std::to_string(sourceCount));
            snapshot.AddOperationDetail("raw-preparation",
                "Quick previews: " + std::to_string(quickAvailable) +
                " of " + std::to_string(sourceCount) + " available");
        }
        if (scan.progress.stage == Stack::RawWorkspace::ScanProgress::Stage::
                DiscoveringProjects) {
            snapshot.AddOperationDetail("raw-preparation", "Discovering saved projects");
        } else if (scan.progress.stage ==
                   Stack::RawWorkspace::ScanProgress::Stage::ApplyingCatalog) {
            snapshot.AddOperationDetail("raw-preparation", "Saving folder data");
        }
    }

    if (thumbnailBusy) {
        const int total = std::max(
            thumbnails.progress.total,
            static_cast<int>(m_RawWorkspace.sources.size()));
        const bool quickPass = thumbnails.statusText.find("quick") !=
            std::string::npos;
        const std::string compact = quickPass ? "Quick " : "Full ";
        const int available = quickPass ? quickAvailable : standardAvailable;
        compactStages.push_back(
            compact + std::to_string(available) + "/" +
            std::to_string(total));
        snapshot.AddOperationDetail("raw-preparation",
            std::string(quickPass ? "Quick previews: " : "Full previews: ") +
            std::to_string(available) + " of " + std::to_string(total) +
            (quickPass ? " available" : " saved"));
    }

    if (groupingBusy) {
        compactStages.push_back(
            "Group " +
            std::to_string(m_RawWorkspaceSimilarityActiveSourceCount));
        snapshot.AddOperationDetail("raw-preparation",
            "Grouping similar photos: " +
            std::to_string(m_RawWorkspaceSimilarityActiveSourceCount) +
            " photos, " +
            std::to_string(m_RawWorkspaceSimilarityLastStackCount) +
            " stacks");
    }

    if (!compactStages.empty()) {
        std::string primary = compactStages.front();
        if (compactStages.size() > 1) {
            primary += " \xC2\xB7 " + compactStages[1];
        }
        snapshot.AddOperation(true, "raw-preparation", primary);
        snapshot.SetPrimary(true, std::move(primary));
        snapshot.SuppressWorkerLabel("Scanning folder");
        snapshot.SuppressWorkerLabel("Building previews");
        snapshot.SuppressWorkerLabel("Preparing Graph previews");
        snapshot.SuppressWorkerLabel("Grouping similar RAW images");
    }

    snapshot.Add(
        !m_RawWorkspaceRootTabActive &&
        (m_NodeBrowserThumbnailPendingEntries > 0 ||
            m_NodeBrowserThumbnailWarmPendingEntries > 0),
        "Preparing Graph previews");
    snapshot.AddMaintenance(Async::IsBusy(m_RawWorkspaceCatalogPersistTaskState),
        "Saving folder data");
    snapshot.AddMaintenance(Async::IsBusy(m_RawWorkspaceAppStatePersistTaskState),
        "Saving settings");
}
