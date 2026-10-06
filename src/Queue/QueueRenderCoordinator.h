#pragma once

#include "Async/MainThreadRequestScope.h"
#include "Queue/RenderQueueModel.h"
#include "Raw/RawGalleryInspection.h"
#include "Notifications/Notifier.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct GLFWwindow;
class EditorModule;
namespace StackAppearance { class AppearanceManager; }

namespace Stack::Queue {

class QueueRenderCoordinator {
public:
    QueueRenderCoordinator();
    ~QueueRenderCoordinator();

    void Configure(
        GLFWwindow* sharedWindow,
        StackAppearance::AppearanceManager* appearance,
        RenderQueueModel* model);
    bool Start(
        const std::filesystem::path& destination,
        const std::vector<Item>& items,
        std::string* errorMessage = nullptr);
    using InspectionCompletion = std::function<void(
        Stack::RawGalleryInspection::Result result)>;
    bool RequestInspection(
        Stack::RawGalleryInspection::Request request,
        InspectionCompletion completion,
        std::string* errorMessage = nullptr,
        Notifications::Notifier notifier = {});
    void SetNotificationScope(Notifications::Notifier notifier);
    Notifications::Notifier& GetNotifier() { return m_Notifier; }
    const Notifications::Notifier& GetNotifier() const { return m_Notifier; }
    const Notifications::Notifier& GetInspectionNotifier() const { return m_InspectionNotifier; }
    Notifications::OperationId ExportOperationId() const { return m_ExportActivity.operationId; }
    Notifications::OperationId InspectionOperationId() const { return m_InspectionActivity.operationId; }
    void Tick();
    void Shutdown();
    bool IsBusy() const;
    bool IsInspectionBusy() const {
        return m_InspectionPhase != InspectionPhase::Idle || m_PendingInspection.has_value();
    }
    const std::string& StatusText() const { return m_StatusText; }

private:
    Notifications::Notifier m_Notifier;
    Notifications::Notifier m_InspectionNotifier;
    Notifications::ActivityHandle m_ExportActivity;
    Notifications::ActivityHandle m_ItemActivity;
    Notifications::ActivityHandle m_InspectionActivity;
    std::size_t m_ExportTotal = 0;
    std::size_t m_ExportSucceeded = 0;
    std::size_t m_ExportFailed = 0;
    enum class Phase {
        Idle,
        PrepareItem,
        LoadProject,
        LoadSource,
        ApplyProject,
        ProcessMultiFrame,
        WaitForRender,
        WriteOutput
    };

    enum class InspectionPhase {
        Idle,
        Prepare,
        LoadProject,
        LoadSource,
        ApplyProject,
        ProcessMultiFrame,
        WaitForRender
    };

    bool EnsureRenderEditor(std::string* errorMessage);
    void BeginNextItem();
    void BeginProjectLoad();
    void BeginSourceLoad();
    bool BeginFullQualityRender(
        std::string status,
        float progress);
    bool BeginInspectionFullQualityRender();
    void BeginOutputWrite();
    void CompleteCurrent();
    void FailCurrent(std::string message);
    std::filesystem::path BuildOutputPath(const Item& item) const;
    void SetPhase(Phase phase, std::string status);
    bool PhaseTimedOut(std::chrono::minutes timeout) const;
    void TickInspection();
    void BeginInspection();
    void BeginInspectionProjectLoad();
    void CompleteInspection();
    void FailInspection(std::string message);
    void CancelActiveInspection(bool preserveLatestRequest);

    GLFWwindow* m_SharedWindow = nullptr;
    StackAppearance::AppearanceManager* m_Appearance = nullptr;
    RenderQueueModel* m_Model = nullptr;
    std::unique_ptr<EditorModule> m_RenderEditor;
    std::filesystem::path m_Destination;
    std::deque<Item> m_Pending;
    Item m_Current;
    Phase m_Phase = Phase::Idle;
    std::chrono::steady_clock::time_point m_PhaseStartedAt {};
    std::string m_StatusText;
    Async::MainThreadRequestScope m_ProjectLoadRequest;
    bool m_Initialized = false;
    bool m_MultiFrameProcessingStarted = false;
    float m_FullQualityProgressBase = 0.42f;
    InspectionPhase m_InspectionPhase = InspectionPhase::Idle;
    std::chrono::steady_clock::time_point m_InspectionPhaseStartedAt {};
    std::optional<Stack::RawGalleryInspection::Request> m_PendingInspection;
    Stack::RawGalleryInspection::Request m_CurrentInspection;
    InspectionCompletion m_InspectionCompletion;
    Async::MainThreadRequestScope m_InspectionLoadRequest;
    bool m_InspectionMultiFrameProcessingStarted = false;
};

} // namespace Stack::Queue
