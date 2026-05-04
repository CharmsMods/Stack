#pragma once

#include "BundlerJobQueue.h"
#include "BundlerGraph.h"
#include "BundlerToolAdapter.h"
#include "BundlerBuild.h"
#include <vector>
#include <string>
#include <memory>

class BundlerModule {
public:
    BundlerModule();
    ~BundlerModule();

    void Initialize();
    void RenderUI();
    void Shutdown();

private:
    void RenderSettingsPanel();
    void RenderFileTreePanel();
    void RenderDiagnosticsPanel();

    BundlerJobQueue m_JobQueue;
    uint32_t m_NextJobId = 1;

    // ── Project settings ─────────────────────────────────────────────────
    std::vector<std::string> m_InputPaths;
    std::string m_ComputedRoot;
    std::string m_OutputDir;
    int m_SelectedBuildMode = 0;        // 0=Single, 1=PerFile, 2=Bundle

    // ── Scan state ───────────────────────────────────────────────────────
    BundlerGraph m_Graph;
    bool m_HasScannedOnce = false;
    int  m_RefsFound = 0;
    int  m_RefsResolved = 0;
    int  m_RefsUnresolved = 0;

    // ── Tool detection ───────────────────────────────────────────────────
    BundlerToolAdapter m_Tools;
    bool m_ToolsDetected = false;

    // ── Build results ────────────────────────────────────────────────────
    std::shared_ptr<BuildResult> m_LastBuildResult;

    // ── Log messages ─────────────────────────────────────────────────────
    struct LogEntry {
        std::string message;
        bool isError;
    };
    std::vector<LogEntry> m_Logs;

    float m_CurrentProgress = 0.0f;
    bool m_IsBuilding = false;

    // ── File tree filter ─────────────────────────────────────────────────
    int m_FileTreeFilter = -1;
    bool m_ShowEdges = true;
    int m_DiagFilter = -1;
};
