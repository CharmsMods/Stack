#include "AppShell.h"
#include "App/WorkspaceSwitcher.h"

#include "Library/LibraryManager.h"
#include "Persistence/ProjectStore.h"
#include "ThirdParty/stb_image.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
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

void AppShell::ConfigureDiagnosticQueueCurrent(
    std::filesystem::path projectPath,
    std::filesystem::path destination) {
    std::string extension = projectPath.extension().u8string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    std::error_code error;
    m_DiagnosticQueueSourceImage = extension != ".stack" &&
        extension != ".stackbundle" && !std::filesystem::is_directory(projectPath, error);
    ConfigureDiagnosticProjectOpen(std::move(projectPath));
    m_DiagnosticQueueDestination = std::move(destination);
    m_DiagnosticQueueCurrent = true;
}

void AppShell::StartDiagnosticQueueCurrent() {
    const auto fail = [&](const std::string& error) {
        std::cerr << "[QueueCurrentDiagnostic] Failed: " << error << '\n';
        m_DiagnosticProjectOpenActive = false;
        m_IsRunning = false;
    };
    m_Editor->SetDocumentPersistenceEnabled(false);
    if (m_Editor->IsRawWorkspaceProjectActive()) {
        fail("Use a saved raster Graph project with an Image connected directly to an Output.");
        return;
    }
    if (!m_Editor->SetWorkspaceMode(Stack::Project::WorkspaceMode::Graph)) {
        fail("The loaded project could not enter Graph.");
        return;
    }
    auto& graph = m_Editor->GetNodeGraph();
    int imageId = 0, outputId = 0;
    for (const int candidate : graph.GetConnectedOutputNodeIds()) {
        const auto* input = graph.FindInputLink(candidate, EditorNodeGraph::kImageInputSocketId);
        const auto* image = input ? graph.FindNode(input->fromNodeId) : nullptr;
        if (image && image->kind == EditorNodeGraph::NodeKind::Image &&
            image->image.width != image->image.height) {
            imageId = image->id;
            outputId = candidate;
            break;
        }
    }
    if (imageId == 0 || !m_Editor->RotateImageNode(imageId, 1)) {
        fail("The fixture needs a non-square, decoded Image connected directly to an Output.");
        return;
    }
    const auto* capturedImage = graph.FindNode(imageId);
    m_DiagnosticQueueExpectedWidth = capturedImage->image.width;
    m_DiagnosticQueueExpectedHeight = capturedImage->image.height;
    const auto capturedRevision = m_Editor->GetProjectEditRevision();
    if (!m_Editor->AddProjectOutputToQueue(outputId)) {
        fail("The current edited Output was rejected by Queue.");
        return;
    }
    if (!m_Editor->RotateImageNode(imageId, 1)) {
        fail("The foreground image could not receive its later edit.");
        return;
    }
    const auto* laterImage = graph.FindNode(imageId);
    std::cout << "[QueueCurrentDiagnostic] Captured revision " << capturedRevision
              << " at " << m_DiagnosticQueueExpectedWidth << 'x' << m_DiagnosticQueueExpectedHeight
              << "; later foreground revision " << m_Editor->GetProjectEditRevision()
              << " at " << laterImage->image.width << 'x' << laterImage->image.height << ".\n";
    if (!m_Editor->CloseCurrentProject(true)) {
        fail("The originating project could not be closed after capture.");
        return;
    }
    m_DiagnosticProjectOpenActive = false;
    m_CurrentTabId = static_cast<int>(Stack::Workspace::Id::Queue);
    m_RequestedTab = -1;
    std::string error;
    m_DiagnosticQueueActive = m_QueueRenderer.Start(m_DiagnosticQueueDestination,
        m_Queue.Model().Snapshot(), &error);
    m_DiagnosticQueueStartedAt = glfwGetTime();
    if (!m_DiagnosticQueueActive) fail(error);
    else std::cout << "[QueueCurrentDiagnostic] Originating project closed; rendering captured edits.\n";
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

    if (m_DiagnosticQueueCurrent && m_DiagnosticQueueSourceImage) {
        const auto failSource = [&](const std::string& error) {
            std::cerr << "[QueueCurrentDiagnostic] Failed: " << error << '\n';
            m_DiagnosticProjectOpenActive = false;
            m_IsRunning = false;
        };
        if (m_Editor->GetSourceLoadTaskState() == Async::TaskState::Failed) {
            failSource(m_Editor->GetSourceLoadStatusText());
            return;
        }
        if (diagnosticNow - m_DiagnosticProjectOpenStartedAt > 120.0) {
            failSource("Timed out preparing the real raster source for current-edit capture.");
            return;
        }
        if (m_Editor->IsSourceLoadBusy()) return;
        auto& graph = m_Editor->GetNodeGraph();
        const bool importing = std::any_of(graph.GetNodes().begin(), graph.GetNodes().end(),
            [](const auto& node) { return node.kind == EditorNodeGraph::NodeKind::Image &&
                (node.image.isLoading || node.image.isEmbedding); });
        if (importing) return;
        if (graph.GetActiveImageNodeId() <= 0) {
            // Normal source loading supplies this pair. Retain the supported
            // fixture path if a source loader leaves only its pipeline image.
            if (!m_Editor->AddImageNodeFromFile(m_DiagnosticProjectOpenPath.u8string(), {20, 30})) {
                failSource("The real raster could not be added to the diagnostic Graph.");
                return;
            }
            const int imageId = graph.GetSelectedNodeId();
            m_Editor->AddOutputNodeAt({320, 30});
            const int outputId = graph.GetSelectedNodeId();
            std::string error;
            if (!m_Editor->ConnectGraphSockets(imageId, EditorNodeGraph::kImageOutputSocketId,
                    outputId, EditorNodeGraph::kImageInputSocketId, &error)) {
                failSource(error);
                return;
            }
        }
        m_Editor->SetCurrentProjectName(m_DiagnosticProjectOpenPath.stem().u8string());
        StartDiagnosticQueueCurrent();
        return;
    }

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
        if (m_DiagnosticQueueCurrent) {
            StartDiagnosticQueueCurrent();
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
            if (m_DiagnosticQueueCurrent) {
                int width = 0, height = 0, channels = 0;
                const auto outputPath = item.outputPath.u8string();
                if (!stbi_info(outputPath.c_str(), &width, &height, &channels) ||
                    width != m_DiagnosticQueueExpectedWidth || height != m_DiagnosticQueueExpectedHeight) {
                    std::cerr << "[QueueCurrentDiagnostic] Captured size mismatch: expected "
                              << m_DiagnosticQueueExpectedWidth << 'x' << m_DiagnosticQueueExpectedHeight
                              << ", exported " << width << 'x' << height << '\n';
                    m_DiagnosticQueueActive = false;
                    m_IsRunning = false;
                    return;
                }
                std::cout << "[QueueCurrentDiagnostic] Verified captured orientation after later edit and project close: "
                          << width << 'x' << height << ".\n";
            }
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
