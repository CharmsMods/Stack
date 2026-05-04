#include "BundlerModule.h"
#include "Utils/FileDialogs.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <filesystem>

namespace fs = std::filesystem;

static std::string ComputeCommonRoot(const std::vector<std::string>& paths) {
    if (paths.empty()) return "";
    if (paths.size() == 1) {
        std::error_code ec;
        fs::path p = fs::canonical(paths[0], ec);
        if (ec) return "";
        if (fs::is_directory(p, ec)) return p.string();
        return p.parent_path().string();
    }

    std::error_code ec;
    fs::path common = fs::canonical(paths[0], ec);
    if (ec) return "";
    if (!fs::is_directory(common, ec)) common = common.parent_path();

    for (size_t i = 1; i < paths.size(); ++i) {
        fs::path p = fs::canonical(paths[i], ec);
        if (ec) continue;
        if (!fs::is_directory(p, ec)) p = p.parent_path();

        auto it1 = common.begin();
        auto it2 = p.begin();
        fs::path newCommon;
        while (it1 != common.end() && it2 != p.end() && *it1 == *it2) {
            newCommon /= *it1;
            ++it1;
            ++it2;
        }
        common = newCommon;
    }
    return common.string();
}

BundlerModule::BundlerModule() {}

BundlerModule::~BundlerModule() {
    Shutdown();
}

void BundlerModule::Initialize() {
    m_JobQueue.Start();
    m_Logs.push_back({"Bundler ready.  Select a project folder to begin.", false});
}

void BundlerModule::Shutdown() {
    m_JobQueue.Stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main render
// ─────────────────────────────────────────────────────────────────────────────

void BundlerModule::RenderUI() {
    // Drain results from the worker
    JobResult result;
    while (m_JobQueue.TryPopResult(result)) {
        m_Logs.push_back({result.message, !result.success});
        m_CurrentProgress = result.progress;

        if (result.scanResult) {
            m_Graph          = std::move(result.scanResult->graph);
            m_RefsFound      = result.scanResult->refsFound;
            m_RefsResolved   = result.scanResult->refsResolved;
            m_RefsUnresolved = result.scanResult->refsUnresolved;
            m_HasScannedOnce = true;

            for (const auto& diag : m_Graph.GetDiagnostics()) {
                const GraphNode* node = m_Graph.GetNode(diag.nodeId);
                std::string prefix = node ? node->relativePath : "?";
                std::string sevStr;
                switch (diag.severity) {
                    case GraphDiagnostic::Severity::Info:    sevStr = "INFO"; break;
                    case GraphDiagnostic::Severity::Warning: sevStr = "WARN"; break;
                    case GraphDiagnostic::Severity::Error:   sevStr = "ERROR"; break;
                }
                m_Logs.push_back({
                    "[" + sevStr + "] " + prefix + ":" + std::to_string(diag.line)
                        + " - " + diag.message,
                    diag.severity == GraphDiagnostic::Severity::Error
                });
            }
        }

        if (result.buildResult) {
            m_LastBuildResult = result.buildResult;

            // Log per-file failures
            for (const auto& fr : result.buildResult->fileResults) {
                if (!fr.success) {
                    m_Logs.push_back({"[ERROR] " + fr.inputPath + ": " + fr.errorMessage, true});
                }
            }
        }

        if (result.isComplete) {
            m_IsBuilding = false;
        }
    }

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove
                           | ImGuiWindowFlags_NoResize  | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("StackBundlerWorkspace", ImVec2(0, 0), false, flags);
    ImGui::PopStyleVar();

    ImGuiID dockId = ImGui::GetID("BundlerDockSpace");
    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    static bool firstLayout = true;
    if (firstLayout) {
        firstLayout = false;
        ImGui::DockBuilderRemoveNode(dockId);
        ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockId, ImGui::GetWindowSize());

        ImGuiID leftId   = 0;
        ImGuiID bottomId = 0;
        ImGuiID mainId   = dockId;
        leftId   = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Left, 0.28f, nullptr, &mainId);
        bottomId = ImGui::DockBuilderSplitNode(mainId, ImGuiDir_Down, 0.30f, nullptr, &mainId);

        ImGui::DockBuilderDockWindow("Bundler Settings",    leftId);
        ImGui::DockBuilderDockWindow("Project Files",       mainId);
        ImGui::DockBuilderDockWindow("Bundler Diagnostics", bottomId);
        ImGui::DockBuilderFinish(dockId);
    }

    if (ImGui::Begin("Bundler Settings")) {
        RenderSettingsPanel();
    }
    ImGui::End();

    if (ImGui::Begin("Project Files")) {
        RenderFileTreePanel();
    }
    ImGui::End();

    if (ImGui::Begin("Bundler Diagnostics")) {
        RenderDiagnosticsPanel();
    }
    ImGui::End();

    ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static ImVec4 ColorForKind(FileKind kind) {
    switch (kind) {
        case FileKind::HTML:           return ImVec4(0.9f, 0.5f, 0.2f, 1.0f);
        case FileKind::CSS:            return ImVec4(0.3f, 0.7f, 1.0f, 1.0f);
        case FileKind::JavaScript:     return ImVec4(1.0f, 0.85f, 0.3f, 1.0f);
        case FileKind::StructuredText: return ImVec4(0.5f, 0.9f, 0.5f, 1.0f);
        case FileKind::OpaqueAsset:    return ImVec4(0.7f, 0.7f, 0.7f, 1.0f);
        case FileKind::Unknown:        return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    }
    return ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
}

static std::string FormatSize(uint64_t bytes) {
    char buf[32];
    if (bytes < 1024)
        snprintf(buf, sizeof(buf), "%llu B", (unsigned long long)bytes);
    else if (bytes < 1024 * 1024)
        snprintf(buf, sizeof(buf), "%.1f KB", bytes / 1024.0);
    else
        snprintf(buf, sizeof(buf), "%.2f MB", bytes / (1024.0 * 1024.0));
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings panel
// ─────────────────────────────────────────────────────────────────────────────

static void RenderToolStatus(const char* label, const ToolInfo& tool) {
    if (tool.available) {
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "  %s: %s",
                           label, tool.version.empty() ? "found" : tool.version.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  %s: not found", label);
    }
}

void BundlerModule::RenderSettingsPanel() {
    // ── Inputs
    ImGui::TextUnformatted("Bundler Inputs");
    
    ImGui::BeginChild("InputList", ImVec2(0, 100), true);
    if (m_InputPaths.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(no files/folders selected)");
    } else {
        for (size_t i = 0; i < m_InputPaths.size(); ++i) {
            ImGui::TextWrapped("%s", m_InputPaths[i].c_str());
        }
    }
    ImGui::EndChild();

    if (ImGui::Button("Add File(s)...", ImVec2(ImGui::GetContentRegionAvail().x * 0.5f - 4, 0))) {
        auto chosen = FileDialogs::OpenMultipleFilesDialog("Select Files");
        if (!chosen.empty()) {
            m_InputPaths.insert(m_InputPaths.end(), chosen.begin(), chosen.end());
            m_ComputedRoot = ComputeCommonRoot(m_InputPaths);
            m_Logs.push_back({"Added " + std::to_string(chosen.size()) + " files. Root: " + m_ComputedRoot, false});
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Add Folder...", ImVec2(ImGui::GetContentRegionAvail().x - 4, 0))) {
        std::string chosen = FileDialogs::OpenFolderDialog("Select Folder");
        if (!chosen.empty()) {
            m_InputPaths.push_back(chosen);
            m_ComputedRoot = ComputeCommonRoot(m_InputPaths);
            m_Logs.push_back({"Added folder: " + chosen + ". Root: " + m_ComputedRoot, false});
        }
    }
    
    if (ImGui::Button("Clear Inputs", ImVec2(-1, 0))) {
        m_InputPaths.clear();
        m_ComputedRoot = "";
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Output Directory
    ImGui::TextUnformatted("Output Directory");
    if (m_OutputDir.empty()) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "(defaults to project_root/dist)");
    } else {
        ImGui::TextWrapped("%s", m_OutputDir.c_str());
    }
    if (ImGui::Button("Browse Output Folder...", ImVec2(-1, 0))) {
        std::string chosen = FileDialogs::OpenFolderDialog("Select Output Directory");
        if (!chosen.empty()) {
            m_OutputDir = chosen;
            m_Logs.push_back({"Output directory set: " + m_OutputDir, false});
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Build mode
    ImGui::TextUnformatted("Build Mode");
    ImGui::RadioButton("Single HTML",     &m_SelectedBuildMode, 0);
    ImGui::RadioButton("Per-file Minify", &m_SelectedBuildMode, 1);
    ImGui::RadioButton("Bundle by Type",  &m_SelectedBuildMode, 2);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Tool detection
    ImGui::TextUnformatted("Minification Tools");
    if (!m_ToolsDetected) {
        if (ImGui::Button("Detect Tools on PATH", ImVec2(-1, 0))) {
            m_Tools.DetectTools();
            m_ToolsDetected = true;
            m_Logs.push_back({"Tool detection complete.", false});
            if (m_Tools.GetEsbuild().available)
                m_Logs.push_back({"  esbuild: " + m_Tools.GetEsbuild().version, false});
            if (m_Tools.GetLightningCSS().available)
                m_Logs.push_back({"  lightningcss: " + m_Tools.GetLightningCSS().version, false});
            if (m_Tools.GetTerser().available)
                m_Logs.push_back({"  terser: " + m_Tools.GetTerser().version, false});
            if (m_Tools.GetMinifyHTML().available)
                m_Logs.push_back({"  minify-html: " + m_Tools.GetMinifyHTML().version, false});
            if (!m_Tools.HasAnyTool())
                m_Logs.push_back({"  No tools found. Files will be copied without minification.", false});
        }
    } else {
        RenderToolStatus("esbuild",      m_Tools.GetEsbuild());
        RenderToolStatus("lightningcss", m_Tools.GetLightningCSS());
        RenderToolStatus("terser",       m_Tools.GetTerser());
        RenderToolStatus("minify-html",  m_Tools.GetMinifyHTML());

        if (ImGui::SmallButton("Re-detect")) {
            m_Tools.DetectTools();
            m_ToolsDetected = true;
        }
    }

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ── Action buttons
    bool canScan  = !m_InputPaths.empty() && !m_IsBuilding;
    bool canBuild = m_HasScannedOnce && !m_IsBuilding;

    if (!canScan) ImGui::BeginDisabled();
    if (ImGui::Button("Scan Inputs", ImVec2(-1, 28))) {
        m_IsBuilding = true;
        m_CurrentProgress = 0.0f;
        Job j;
        j.id      = m_NextJobId++;
        j.kind    = JobKind::Scan;
        j.inputPaths = m_InputPaths;
        j.computedRoot = m_ComputedRoot;
        m_JobQueue.PushJob(j);
    }
    if (!canScan) ImGui::EndDisabled();

    if (!canBuild) ImGui::BeginDisabled();
    if (ImGui::Button("Build", ImVec2(-1, 28))) {
        // Detect tools if not done yet
        if (!m_ToolsDetected) {
            m_Tools.DetectTools();
            m_ToolsDetected = true;
        }

        m_IsBuilding = true;
        m_CurrentProgress = 0.0f;
        m_LastBuildResult = nullptr;

        Job j;
        j.id            = m_NextJobId++;
        j.kind          = JobKind::Minify;
        j.graphSnapshot = std::make_shared<BundlerGraph>(m_Graph);
        j.buildConfig   = std::make_shared<BuildConfig>();
        j.buildConfig->mode       = static_cast<BuildMode>(m_SelectedBuildMode);
        j.buildConfig->projectRoot = m_ComputedRoot;
        j.buildConfig->outputDir   = m_OutputDir;
        j.toolAdapter  = std::make_shared<BundlerToolAdapter>(m_Tools);

        m_JobQueue.PushJob(j);
    }
    if (!canBuild) ImGui::EndDisabled();

    if (m_IsBuilding) {
        ImGui::ProgressBar(m_CurrentProgress, ImVec2(-1, 0), "");
    }

    // ── Ref summary
    if (m_HasScannedOnce && m_RefsFound > 0) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::Text("References: %d found", m_RefsFound);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "  Resolved: %d", m_RefsResolved);
        if (m_RefsUnresolved > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "  Unresolved: %d", m_RefsUnresolved);
        else
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "  Unresolved: 0");
    }

    // ── Build results summary
    if (m_LastBuildResult) {
        ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
        ImGui::TextUnformatted("Last Build");

        auto& br = *m_LastBuildResult;
        ImGui::Text("  Processed: %d files", br.filesProcessed);
        ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f),
            "  Minified:  %d", br.filesMinified);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
            "  Copied:    %d", br.filesCopied);
        if (br.filesFailed > 0)
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                "  Failed:    %d", br.filesFailed);

        ImGui::Text("  Input:  %s", FormatSize(br.totalInputSize).c_str());
        ImGui::Text("  Output: %s", FormatSize(br.totalOutputSize).c_str());

        if (br.totalInputSize > 0 && br.totalOutputSize < br.totalInputSize) {
            double pct = 100.0 * (1.0 - (double)br.totalOutputSize / (double)br.totalInputSize);
            ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f),
                "  Savings: %.1f%%", pct);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// File tree panel
// ─────────────────────────────────────────────────────────────────────────────

void BundlerModule::RenderFileTreePanel() {
    if (!m_HasScannedOnce) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f),
            "No project scanned yet.  Use \"Scan Project\" in settings.");
        return;
    }

    auto counts = m_Graph.CountByKind();

    ImGui::Text("Total: %d files", counts.Total());
    ImGui::SameLine();
    ImGui::TextColored(ColorForKind(FileKind::HTML),       " HTML:%d",  counts.html);
    ImGui::SameLine();
    ImGui::TextColored(ColorForKind(FileKind::CSS),        " CSS:%d",   counts.css);
    ImGui::SameLine();
    ImGui::TextColored(ColorForKind(FileKind::JavaScript), " JS:%d",    counts.js);
    ImGui::SameLine();
    ImGui::TextColored(ColorForKind(FileKind::StructuredText)," Text:%d",counts.text);
    ImGui::SameLine();
    ImGui::TextColored(ColorForKind(FileKind::OpaqueAsset)," Asset:%d", counts.asset);
    if (counts.unknown > 0) {
        ImGui::SameLine();
        ImGui::TextColored(ColorForKind(FileKind::Unknown), " Unknown:%d", counts.unknown);
    }

    ImGui::Separator();
    const char* filterLabels[] = {"All", "HTML", "CSS", "JavaScript", "Text/Data", "Asset", "Unknown"};
    ImGui::SetNextItemWidth(150);
    ImGui::Combo("Filter", &m_FileTreeFilter, filterLabels, 7);
    ImGui::SameLine();
    ImGui::Checkbox("Show References", &m_ShowEdges);

    ImGui::Separator();

    ImGui::BeginChild("FileList", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    const auto& nodes = m_Graph.GetNodes();
    for (const auto& node : nodes) {
        if (m_FileTreeFilter >= 0 && static_cast<int>(node.kind) != m_FileTreeFilter)
            continue;

        auto outEdges = m_Graph.GetOutgoingEdges(node.id);
        bool hasEdges = m_ShowEdges && !outEdges.empty();

        // Check if we have build info for this file
        const FileBuildResult* buildInfo = nullptr;
        if (m_LastBuildResult) {
            for (const auto& fr : m_LastBuildResult->fileResults) {
                if (fr.nodeId == node.id) { buildInfo = &fr; break; }
            }
        }

        if (hasEdges) {
            ImGui::PushStyleColor(ImGuiCol_Text, ColorForKind(node.kind));
            std::string label = "[" + std::string(FileKindToString(node.kind)) + "] ("
                + FormatSize(node.fileSize) + ") " + node.relativePath
                + "  [" + std::to_string((int)outEdges.size()) + " refs]";
            if (buildInfo && buildInfo->minified) {
                label += "  -> " + FormatSize(buildInfo->outputSize);
            }
            bool open = ImGui::TreeNode((void*)(intptr_t)node.id, "%s", label.c_str());
            ImGui::PopStyleColor();

            if (open) {
                for (const auto* edge : outEdges) {
                    if (edge->resolved) {
                        const GraphNode* target = m_Graph.GetNode(edge->targetNodeId);
                        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f),
                            "  -> [%s] %s  (%s, line %d)",
                            RefKindToString(edge->refKind),
                            target ? target->relativePath.c_str() : "?",
                            edge->originalSpecifier.c_str(),
                            edge->sourceLine);
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f),
                            "  -> [%s] UNRESOLVED: \"%s\"  (line %d)",
                            RefKindToString(edge->refKind),
                            edge->originalSpecifier.c_str(),
                            edge->sourceLine);
                    }
                }
                ImGui::TreePop();
            }
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ColorForKind(node.kind));
            ImGui::Text("[%s]", FileKindToString(node.kind));
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), "(%s)", FormatSize(node.fileSize).c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(node.relativePath.c_str());

            // Show build result inline
            if (buildInfo && buildInfo->minified) {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f),
                    "-> %s", FormatSize(buildInfo->outputSize).c_str());
            }
        }
    }
    ImGui::EndChild();
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics panel
// ─────────────────────────────────────────────────────────────────────────────

void BundlerModule::RenderDiagnosticsPanel() {
    const char* diagLabels[] = {"All", "Info", "Warning", "Error"};
    ImGui::SetNextItemWidth(120);
    ImGui::Combo("Severity", &m_DiagFilter, diagLabels, 4);
    ImGui::SameLine();
    if (ImGui::Button("Clear Logs")) {
        m_Logs.clear();
    }
    ImGui::Separator();

    ImGui::BeginChild("LogRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
    for (const auto& log : m_Logs) {
        if (m_DiagFilter == 1 && log.message.find("[INFO]")  == std::string::npos) continue;
        if (m_DiagFilter == 2 && log.message.find("[WARN]")  == std::string::npos) continue;
        if (m_DiagFilter == 3 && log.message.find("[ERROR]") == std::string::npos) continue;

        if (log.isError) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        } else if (log.message.find("[WARN]") != std::string::npos) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
        }
        ImGui::TextUnformatted(log.message.c_str());
        ImGui::PopStyleColor();
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}
