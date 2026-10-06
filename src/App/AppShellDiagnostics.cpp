#include "AppShell.h"

#include "Library/LibraryManager.h"
#include "Persistence/ProjectStore.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

void AppShell::ConfigureDiagnosticProjectOpen(
    std::filesystem::path projectPath,
    std::filesystem::path switchProjectPath) {
    m_DiagnosticProjectOpenPath = std::move(projectPath);
    m_DiagnosticProjectSwitchPath = std::move(switchProjectPath);
}

bool AppShell::WasDiagnosticProjectOpenSuccessful() const {
    return m_DiagnosticProjectOpenSuccessful;
}

void AppShell::ConfigureDiagnosticQueueExport(
    std::filesystem::path inputPath,
    std::filesystem::path destination,
    bool sourceImage) {
    m_DiagnosticQueueProjectPath = std::move(inputPath);
    m_DiagnosticQueueDestination = std::move(destination);
    m_DiagnosticQueueSourceImage = sourceImage;
    m_DiagnosticQueueLastItemState = -1;
    m_DiagnosticQueueLastItemStatus.clear();
}

bool AppShell::WasDiagnosticQueueExportSuccessful() const {
    return m_DiagnosticQueueSuccessful;
}

void AppShell::ConfigureDiagnosticGalleryInspection(
    std::filesystem::path firstSource,
    std::filesystem::path latestSource) {
    m_DiagnosticGalleryInspectionFirstSource = std::move(firstSource);
    m_DiagnosticGalleryInspectionLatestSource = std::move(latestSource);
    m_DiagnosticGalleryInspectionLatestRequested = false;
    m_DiagnosticGalleryInspectionStaleCompletion = false;
    m_DiagnosticGalleryInspectionSuccessful = false;
}

bool AppShell::WasDiagnosticGalleryInspectionSuccessful() const {
    return m_DiagnosticGalleryInspectionSuccessful;
}

void AppShell::TickDiagnosticProjectOpen() {
    if (m_DiagnosticProjectOpenPath.empty() ||
        !m_DiagnosticProjectOpenActive) {
        return;
    }

    const double diagnosticNow = glfwGetTime();
    if (m_DiagnosticProjectOpenLastTickAt > 0.0) {
        m_DiagnosticProjectOpenMaxTickGapMs = std::max(
            m_DiagnosticProjectOpenMaxTickGapMs,
            (diagnosticNow - m_DiagnosticProjectOpenLastTickAt) * 1000.0);
    }
    m_DiagnosticProjectOpenLastTickAt = diagnosticNow;

    const Async::TaskState loadState =
        m_Editor->GetProjectLoadTaskState();
    if (loadState == Async::TaskState::Failed ||
        m_Editor->HasDeferredLoadedProjectApplyFailed()) {
        std::cerr << "[ProjectOpenDiagnostic] Failed: "
                  << (m_Editor->GetDeferredLoadedProjectStatusText().empty()
                          ? m_Editor->GetProjectLoadStatusText()
                          : m_Editor->GetDeferredLoadedProjectStatusText())
                  << '\n';
        m_DiagnosticProjectOpenActive = false;
        m_IsRunning = false;
        return;
    }

    if (m_Editor->IsDeferredLoadedProjectReadyForReveal()) {
        auto absoluteProjectRoot = [](const std::filesystem::path& path) {
            std::error_code pathError;
            std::filesystem::path root =
                Stack::Project::ResolveProjectStoreRoot(path);
            std::filesystem::path absolute =
                std::filesystem::absolute(root, pathError);
            if (!pathError) root = std::move(absolute);
            return root.lexically_normal();
        };
        const std::filesystem::path activeProjectRoot =
            absoluteProjectRoot(m_Editor->GetCurrentProjectFileName());
        const std::filesystem::path requestedProjectRoot =
            absoluteProjectRoot(m_DiagnosticProjectOpenPath);
        if (activeProjectRoot != requestedProjectRoot) {
            // A switch diagnostic can briefly observe project A's completed
            // apply state while project B is still decoding. Only validate a
            // presentation after the requested document identity is active.
            if (glfwGetTime() - m_DiagnosticProjectOpenStartedAt > 120.0) {
                std::cerr
                    << "[ProjectOpenDiagnostic] Timed out waiting for target "
                    << requestedProjectRoot.string()
                    << " (active " << activeProjectRoot.string() << ")\n";
                m_DiagnosticProjectOpenActive = false;
                m_IsRunning = false;
            }
            return;
        }
        if (m_Editor->IsRawWorkspaceProjectActive() &&
            !m_Editor->IsMultiFrameRawProjectActive() &&
            m_Editor->GetRawWorkspaceStateForValidation()
                .selectedSourceKey.empty()) {
            std::cerr
                << "[ProjectOpenDiagnostic] Failed: the single-RAW project "
                   "was applied without selecting its editing source.\n";
            m_DiagnosticProjectOpenActive = false;
            m_IsRunning = false;
            return;
        }
        if (m_Editor->IsRawWorkspaceProjectActive() &&
            !m_Editor->IsMultiFrameRawProjectActive() &&
            !m_Editor->HasActiveRawWorkspacePresentationForValidation()) {
            m_DiagnosticProjectOpenReadyFrames = 0;
            if (glfwGetTime() - m_DiagnosticProjectOpenStartedAt > 120.0) {
                std::cerr
                    << "[ProjectOpenDiagnostic] Failed: the single-RAW project "
                       "opened without a displayable image ("
                    << m_Editor->GetActiveRawWorkspacePresentationDiagnosticForValidation()
                    << ").\n";
                m_DiagnosticProjectOpenActive = false;
                m_IsRunning = false;
            }
            return;
        }
        if (m_DiagnosticProjectOpenReadyFrames == 0) {
            const EditorModule::GraphPerformanceStats& firstRenderStats =
                m_Editor->GetGraphPerformanceStats();
            std::cout
                << "[ProjectOpenDiagnostic] First presentation ready; "
                << "max UI tick gap so far: "
                << m_DiagnosticProjectOpenMaxTickGapMs
                << " ms; RAW render: "
                << firstRenderStats.lastMainRenderMs
                << " ms at preview max dimension "
                << firstRenderStats.lastRawPreviewMaxDimension
                << "; snapshot build: "
                << firstRenderStats.lastSnapshotBuildMs
                << ".\n";
        }
        ++m_DiagnosticProjectOpenReadyFrames;
        if (!m_DiagnosticProjectSwitchRequested &&
            !m_DiagnosticProjectSwitchPath.empty() &&
            m_DiagnosticProjectOpenReadyFrames >= 30) {
            m_DiagnosticProjectSwitchRequested = true;
            m_DiagnosticProjectOpenPath =
                std::move(m_DiagnosticProjectSwitchPath);
            m_DiagnosticProjectOpenReadyFrames = 0;
            m_DiagnosticProjectOpenStartedAt = glfwGetTime();
            m_DiagnosticProjectOpenLastTickAt =
                m_DiagnosticProjectOpenStartedAt;
            m_DiagnosticProjectOpenMaxTickGapMs = 0.0;
            if (!m_Editor->RequestOpenRawWorkspaceProjectFromGallery(
                    m_DiagnosticProjectOpenPath)) {
                std::cerr
                    << "[ProjectOpenDiagnostic] Failed to queue switch to "
                    << m_DiagnosticProjectOpenPath.string() << '\n';
                m_DiagnosticProjectOpenActive = false;
                m_IsRunning = false;
            }
            return;
        }
        if (m_DiagnosticProjectOpenReadyFrames >= 300) {
            const EditorModule::GraphPerformanceStats& renderStats =
                m_Editor->GetGraphPerformanceStats();
            std::cout << "[ProjectOpenDiagnostic] "
                      << (m_DiagnosticProjectSwitchRequested
                              ? "Project switch opened successfully.\n"
                              : "Project opened successfully.\n");
            std::cout
                << "[ProjectOpenDiagnostic] Max UI tick gap: "
                << m_DiagnosticProjectOpenMaxTickGapMs
                << " ms; latest RAW render: "
                << renderStats.lastMainRenderMs
                << " ms at preview max dimension "
                << renderStats.lastRawPreviewMaxDimension
                << "; snapshot build: "
                << renderStats.lastSnapshotBuildMs
                << ".\n";
            m_DiagnosticProjectOpenSuccessful = true;
            m_DiagnosticProjectOpenActive = false;
            m_IsRunning = false;
        }
        return;
    }

    if (glfwGetTime() - m_DiagnosticProjectOpenStartedAt > 120.0) {
        std::cerr << "[ProjectOpenDiagnostic] Timed out in phase "
                  << m_Editor->GetDeferredLoadedProjectPhaseLabel() << ": "
                  << m_Editor->GetDeferredLoadedProjectStatusText() << '\n';
        m_DiagnosticProjectOpenActive = false;
        m_IsRunning = false;
    }
}

void AppShell::TickDiagnosticQueueExport() {
    if (!m_DiagnosticQueueActive) return;
    const std::vector<Stack::Queue::Item> items =
        m_Queue.Model().Snapshot();
    if (!items.empty()) {
        const Stack::Queue::Item& item = items.front();
        const int itemState = static_cast<int>(item.state);
        if (itemState != m_DiagnosticQueueLastItemState ||
            item.status != m_DiagnosticQueueLastItemStatus) {
            std::cout << "[QueueExportDiagnostic] "
                      << item.displayName << ": "
                      << (item.status.empty()
                              ? m_QueueRenderer.StatusText()
                              : item.status)
                      << " (" << static_cast<int>(item.progress * 100.0f)
                      << "%)\n";
            m_DiagnosticQueueLastItemState = itemState;
            m_DiagnosticQueueLastItemStatus = item.status;
        }
        if (item.state == Stack::Queue::ItemState::Complete) {
            std::cout << "[QueueExportDiagnostic] Export completed: "
                      << item.displayName << '\n';
            m_DiagnosticQueueSuccessful = true;
            m_DiagnosticQueueActive = false;
            m_IsRunning = false;
            return;
        }
        if (item.state == Stack::Queue::ItemState::Failed) {
            std::cerr << "[QueueExportDiagnostic] Export failed: "
                      << item.status << '\n';
            m_DiagnosticQueueActive = false;
            m_IsRunning = false;
            return;
        }
    }
    if (glfwGetTime() - m_DiagnosticQueueStartedAt > 1800.0) {
        std::cerr << "[QueueExportDiagnostic] Export timed out in phase: "
                  << m_QueueRenderer.StatusText() << '\n';
        m_DiagnosticQueueActive = false;
        m_IsRunning = false;
    }
}

void AppShell::CompleteDiagnosticGalleryInspection(
    Stack::RawGalleryInspection::Result result) {
    if (!m_DiagnosticGalleryInspectionActive) return;
    if (result.requestId != 2u) {
        m_DiagnosticGalleryInspectionStaleCompletion = true;
        std::cerr
            << "[GalleryInspectionDiagnostic] Stale request completed: "
            << result.requestId << '\n';
        return;
    }
    if (!result) {
        std::cerr
            << "[GalleryInspectionDiagnostic] Latest request failed: "
            << result.error << '\n';
    } else if (m_DiagnosticGalleryInspectionStaleCompletion) {
        std::cerr
            << "[GalleryInspectionDiagnostic] A superseded request was published.\n";
    } else {
        std::cout
            << "[GalleryInspectionDiagnostic] Latest request completed at "
            << result.width << 'x' << result.height
            << "; max UI tick gap "
            << m_DiagnosticGalleryInspectionMaxTickGapMs << " ms.\n";
        m_DiagnosticGalleryInspectionSuccessful = true;
    }
    m_DiagnosticGalleryInspectionActive = false;
    m_IsRunning = false;
}

void AppShell::TickDiagnosticGalleryInspection() {
    if (!m_DiagnosticGalleryInspectionActive) return;
    const double now = glfwGetTime();
    if (m_DiagnosticGalleryInspectionLastTickAt > 0.0) {
        m_DiagnosticGalleryInspectionMaxTickGapMs = std::max(
            m_DiagnosticGalleryInspectionMaxTickGapMs,
            (now - m_DiagnosticGalleryInspectionLastTickAt) * 1000.0);
    }
    m_DiagnosticGalleryInspectionLastTickAt = now;

    if (!m_DiagnosticGalleryInspectionLatestRequested) {
        Stack::RawGalleryInspection::Request request;
        request.requestId = 2;
        request.sourcePath = m_DiagnosticGalleryInspectionLatestSource;
        request.displayName = request.sourcePath.filename().string();
        request.version = Stack::RawGalleryInspection::Version::After;
        std::string inspectionError;
        if (!m_QueueRenderer.RequestInspection(
                std::move(request),
                [this](Stack::RawGalleryInspection::Result result) {
                    CompleteDiagnosticGalleryInspection(std::move(result));
                },
                &inspectionError)) {
            std::cerr
                << "[GalleryInspectionDiagnostic] Latest request failed to start: "
                << inspectionError << '\n';
            m_DiagnosticGalleryInspectionActive = false;
            m_IsRunning = false;
            return;
        }
        m_DiagnosticGalleryInspectionLatestRequested = true;
        std::cout
            << "[GalleryInspectionDiagnostic] Superseded the first request with "
            << m_DiagnosticGalleryInspectionLatestSource.filename().string()
            << ".\n";
    }

    if (now - m_DiagnosticGalleryInspectionStartedAt > 300.0) {
        std::cerr
            << "[GalleryInspectionDiagnostic] Timed out waiting for the latest request.\n";
        m_DiagnosticGalleryInspectionActive = false;
        m_IsRunning = false;
    }
}
