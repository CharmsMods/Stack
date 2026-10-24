#!/usr/bin/env python3
"""Generate bounded CPU checks of the production denoise async-launch branch.

The launch branch and failure-memo guard are extracted from
RenderRawDevelopmentRgbDenoise, not reimplemented. Only std::async is replaced
with a deterministic launch boundary; GPU/model/adapter operations are fakes.
The worker retention/currentness methods also check preservation of deferred
main-image demand through auxiliary work. Completion consumption and synchronous
publication check invalidation and same-model recovery. This does not test
inference or GL; cache invalidation itself is a fake boundary here.
"""
import argparse
from pathlib import Path


def block(source, start):
    brace = source.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        if source[end] == '{':
            depth += 1
        elif source[end] == '}':
            depth -= 1
        end += 1
    return source[start:end]


parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
source = (root / 'src/Renderer/Internal/RenderPipelineRawRgbDenoise.cpp').read_text()
method = block(source, source.index('unsigned int RenderPipeline::RenderRawDevelopmentRgbDenoise('))
marker = 'if (m_RawRgbDenoiseAsyncEnabled) {'
first_branch = method.index(marker)
launch_branch = block(method, method.index(marker, first_branch + len(marker)))
assert launch_branch.count('std::async(') == 1
launch_branch = launch_branch.replace('std::async(', 'TestAsync(')
failure_guard = block(method, method.index('if (m_RawRgbDenoiseState->lastCompletedModelFingerprint =='))
sync_start = method.index('        const unsigned int outputTexture =\n            UploadFloatRgbaTexture(')
sync_return = '        return outputTexture;'
sync_publication = method[sync_start:method.index(sync_return, sync_start) + len(sync_return)]
set_string = block(source, source.index('void SetStringNoThrow('))
provisional_output = block(source, source.index('unsigned int RenderPipeline::RenderProvisionalRawRgbDenoiseOutput('))
consume_completion = block(source, source.index('bool RenderPipeline::ConsumeRawRgbDenoiseAsyncCompletion('))
completion_ready = block(source, source.index('bool RenderPipeline::IsRawRgbDenoiseAsyncCompletionReady('))
sessions = (root / 'src/Editor/Internal/EditorRenderWorkerSessions.cpp').read_text()
retention = block(sessions, sessions.index('void EditorRenderWorker::RetainDenoiseContinuationLocked('))
currentness = block(sessions, sessions.index('bool EditorRenderWorker::IsOwnerSnapshotInvalidLocked('))

pre = r'''
#include "Renderer/RawRgbDenoiseState.h"
#include "Editor/RawRenderPurpose.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <future>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

namespace Raw { enum class RawWorkingSpace { LinearSrgbD65 }; }
namespace Stack::RawRecipe { struct RawRgbDenoiseRecipe {}; }
namespace Stack::Restormer {
enum class Quality { InteractivePreview, Settled };
struct DenoiseResult {
    bool ok = false, cancelled = false;
    std::string provider, error;
    double inferenceMilliseconds = 0.0;
    int completedTiles = 0, totalTiles = 0;
    std::vector<float> outputSrgbProxy;
};
struct Client {
    static Client& Instance() { static Client instance; return instance; }
    template<class... Args> DenoiseResult Denoise(Args&&...) {
        throw std::logic_error("Model work is outside this launch test.");
    }
};
}
namespace Stack::RawRestormer {
struct AdapterResult { bool ok = true; std::string error; float inputExposureGain = 1.0f; };
template<class... Args> AdapterResult ApplyOutput(Args&&...) {
    throw std::logic_error("Adapter work is outside this launch test.");
}
}
double MeanAbsoluteRgbDifference(const std::vector<float>&,
    const std::vector<float>&, int, int) { return 0.0; }

enum class LaunchMode { StandardException, UnknownException, Success };
LaunchMode launchMode = LaunchMode::Success;
int launches = 0;
template<class Callback>
std::future<RawRgbDenoiseAsyncResult> TestAsync(std::launch, Callback&&) {
    ++launches;
    if (launchMode == LaunchMode::StandardException)
        throw std::runtime_error("Synthetic thread launch failure");
    if (launchMode == LaunchMode::UnknownException) throw 7;
    // A valid, ready future exercises successful admission without running
    // any model code or creating a thread.
    std::promise<RawRgbDenoiseAsyncResult> promise;
    auto future = promise.get_future();
    promise.set_value({});
    return future;
}
'''

fixture = r'''
class RenderPipeline {
public:
    std::shared_ptr<RawRgbDenoiseState> m_RawRgbDenoiseState =
        std::make_shared<RawRgbDenoiseState>();
    bool m_RawRgbDenoiseAsyncEnabled = true;
    bool m_HasProvisionalRawRgbDenoiseOutput = false;
    bool m_LastRawRgbDenoiseOutputPending = false;
    int m_Width = 2, m_Height = 2, m_PreviewMaxDimension = 0;
    std::uint64_t m_RenderGeneration = 41;
    std::shared_ptr<const std::vector<float>> m_RestormerNeutralCacheOutputProxy;
    std::shared_ptr<const std::vector<float>> m_RestormerAppliedCacheRgba;
    std::size_t m_RestormerNeutralCacheFingerprint = 0, m_RestormerAppliedCacheFingerprint = 0;
    int m_RestormerNeutralCacheWidth = 0, m_RestormerNeutralCacheHeight = 0;
    int m_RestormerAppliedCacheWidth = 0, m_RestormerAppliedCacheHeight = 0;
    bool hasProvisionalGraphCache = true, hasProvisionalStageCache = true;
    int invalidations = 0;
    void InvalidateGraphCaches() {
        ++invalidations;
        hasProvisionalGraphCache = hasProvisionalStageCache = false;
    }
    bool IsRawRgbDenoiseAsyncCompletionReady() const;
    bool ConsumeRawRgbDenoiseAsyncCompletion();
    int exposureCalls = 0;
    unsigned int uploadedTexture = 73;
    unsigned int UploadFloatRgbaTexture(int, int, const std::vector<float>&) {
        return uploadedTexture;
    }
    unsigned int SyncPublish(std::size_t modelFingerprint);
    unsigned int RenderProvisionalRawRgbDenoiseOutput(unsigned int inputTexture);
    unsigned int RenderRawDevelopmentExposure(unsigned int, float) {
        ++exposureCalls;
        return 73;
    }
    unsigned int Launch() {
        const unsigned int inputTexture = 19;
        const Stack::RawRecipe::RawRgbDenoiseRecipe settings;
        const auto workingSpace = Raw::RawWorkingSpace::LinearSrgbD65;
        constexpr std::size_t modelFingerprint = 101, applicationFingerprint = 202;
        std::vector<float> sourceRgba(16, 0.25f), inputProxy(12, 0.25f);
        const Stack::RawRestormer::AdapterResult proxyResult;
        const bool modelCacheHit = false;
'''

tail = r'''
        return 0;
    }
    unsigned int RetryGuard(std::size_t modelFingerprint) {
'''

tests = r'''
        return 1;
    }
};

class EditorRenderWorker {
public:
    struct Snapshot {
        std::uint64_t ownerId = 7, generation = 20, schedulingSerial = 30;
        RawRenderPurpose rawRenderPurpose = RawRenderPurpose::InteractivePresentation;
        struct { int viewportDependencyEdge = -1; } rawWorkspace;
    };
    struct OwnerRenderState {
        std::shared_ptr<RawRgbDenoiseState> denoise = std::make_shared<RawRgbDenoiseState>();
        std::optional<Snapshot> continuation;
        bool continuationReady = false, resumeOriginalPurpose = false, denoiseInFlight = false;
        bool released = false, resetDenoiseRequested = false;
        RawRenderPurpose denoisePurpose = RawRenderPurpose::InteractivePresentation;
        std::uint64_t invalidBeforeGeneration = 0, invalidBeforeSerial = 0, latestSerial = 30;
        std::unordered_map<RawRenderPurpose, std::uint64_t> canceledPurposeBeforeSerial;
    };
    std::unordered_map<std::uint64_t, OwnerRenderState> m_OwnerStates;
    std::uint64_t m_InvalidBeforeGeneration = 0, m_InvalidBeforeSchedulingSerial = 0;
    bool IsOwnerSnapshotInvalidLocked(std::uint64_t, std::uint64_t,
        std::uint64_t, RawRenderPurpose) const;
    void RetainDenoiseContinuationLocked(Snapshot&, bool mainDenoisePending = false);
};
'''

checks = r'''
void CheckCompletionInvalidation() {
    for (int outcome = 0; outcome < 4; ++outcome) {
        RenderPipeline pipeline;
        auto& state = *pipeline.m_RawRgbDenoiseState;
        state.pending = true;
        state.modelFingerprint = 101;
        state.applicationFingerprint = 202;
        state.cancel = std::make_shared<std::atomic<bool>>(false);
        if (outcome != 3) {
            std::promise<RawRgbDenoiseAsyncResult> promise;
            state.future = promise.get_future();
            if (outcome == 2) {
                promise.set_exception(std::make_exception_ptr(std::runtime_error("Inference exception")));
            } else {
                RawRgbDenoiseAsyncResult result;
                result.ok = outcome == 0;
                result.error = "Inference failure";
                result.modelFingerprint = 101;
                result.applicationFingerprint = 202;
                result.width = result.height = 2;
                if (result.ok) result.outputRgba = std::make_shared<const std::vector<float>>(16, 0.25f);
                promise.set_value(std::move(result));
            }
        }
        Require(pipeline.ConsumeRawRgbDenoiseAsyncCompletion(),
            "Ready or missing-future completion must be consumed.");
        Require(!state.pending && !state.future.valid() && !state.cancel &&
            state.modelFingerprint == 0 && state.applicationFingerprint == 0,
            "Completion must release active model state before follow-up work.");
        Require(pipeline.invalidations == 1 && !pipeline.hasProvisionalGraphCache &&
            !pipeline.hasProvisionalStageCache,
            "Success and terminal failure must both invalidate provisional graph and RAW-stage pixels.");
        if (outcome == 0) {
            Require(pipeline.m_RestormerAppliedCacheRgba &&
                pipeline.m_RestormerAppliedCacheFingerprint == 202 && state.lastCompletedError.empty(),
                "Verified completed pixels must remain available for a settled rerender.");
        } else {
            Require(!pipeline.m_RestormerAppliedCacheRgba && !state.lastCompletedError.empty() &&
                pipeline.RetryGuard(101) == 0,
                "Failed inference must stay terminal rather than silently reusing provisional pixels.");
        }
        Require(!pipeline.ConsumeRawRgbDenoiseAsyncCompletion() && pipeline.invalidations == 1,
            "A consumed completion must not invalidate or publish a second time.");
    }
}

void CheckSynchronousRecovery() {
    for (int scenario = 0; scenario < 3; ++scenario) {
        RenderPipeline pipeline;
        auto& state = *pipeline.m_RawRgbDenoiseState;
        state.lastCompletedModelFingerprint = 101;
        state.lastCompletedError = "Previous async failure";
        pipeline.uploadedTexture = scenario == 0 ? 0 : 73;
        const std::size_t modelFingerprint = scenario == 2 ? 102 : 101;
        Require(pipeline.SyncPublish(modelFingerprint) == pipeline.uploadedTexture,
            "Synchronous publication must retain its real upload result.");
        if (scenario == 1) {
            Require(state.lastCompletedModelFingerprint == 0 && state.lastCompletedError.empty(),
                "A verified successful retry must supersede the same model's old async failure.");
            Require(pipeline.RetryGuard(101) == 1,
                "Later interactive work must not resurrect a recovered failure.");
        } else {
            Require(state.lastCompletedModelFingerprint == 101 &&
                state.lastCompletedError == "Previous async failure",
                "Failed publication or a different model must not erase the old failure.");
        }
        Require(scenario != 0 || !state.error.empty(),
            "A failed synchronous upload must retain a current diagnostic.");
    }
}

void CheckDeferredRetention() {
    {
        EditorRenderWorker worker;
        EditorRenderWorker::Snapshot snapshot;
        auto& owner = worker.m_OwnerStates[snapshot.ownerId];
        // The main image needed async work; an auxiliary graph then cleared
        // deferred before the request's continuation was retained.
        worker.RetainDenoiseContinuationLocked(snapshot, true);
        Require(owner.denoise->deferred && owner.continuation &&
            owner.continuation->generation == 20 && !owner.continuationReady,
            "Main-image demand must survive an auxiliary clearing deferred.");
        Require(!owner.denoise->pending && !owner.denoise->future.valid() && !owner.denoiseInFlight,
            "Restoring deferred demand must not invent an in-flight model.");
        Require(!owner.resumeOriginalPurpose,
            "Restored demand must remain a denoise completion, not ordinary replay.");
    }
    {
        EditorRenderWorker worker;
        EditorRenderWorker::Snapshot snapshot;
        auto& owner = worker.m_OwnerStates[snapshot.ownerId];
        worker.RetainDenoiseContinuationLocked(snapshot);
        Require(!owner.denoise->deferred && !owner.continuation,
            "A request with no observed unmet demand must not gain a continuation.");
    }
    for (int invalid = 0; invalid < 4; ++invalid) {
        EditorRenderWorker worker;
        EditorRenderWorker::Snapshot snapshot;
        auto& owner = worker.m_OwnerStates[snapshot.ownerId];
        if (invalid == 0) owner.canceledPurposeBeforeSerial[snapshot.rawRenderPurpose] = 31;
        if (invalid == 1) owner.latestSerial = 31;
        if (invalid == 2) owner.released = true;
        if (invalid == 3) owner.invalidBeforeGeneration = 21;
        worker.RetainDenoiseContinuationLocked(snapshot, true);
        Require(!owner.denoise->deferred && !owner.continuation,
            "Canceled, replaced, released or obsolete work must not be revived.");
    }
    for (auto purpose : {RawRenderPurpose::ExplicitExport,
             RawRenderPurpose::ExplicitInspection, RawRenderPurpose::ViewportCalibration,
             RawRenderPurpose::ViewportOverview}) {
        EditorRenderWorker worker;
        EditorRenderWorker::Snapshot snapshot;
        snapshot.rawRenderPurpose = purpose;
        snapshot.rawWorkspace.viewportDependencyEdge = 0;
        auto& owner = worker.m_OwnerStates[snapshot.ownerId];
        worker.RetainDenoiseContinuationLocked(snapshot, true);
        Require(!owner.denoise->deferred && !owner.continuation,
            "Authoritative and isolated requests must not acquire deferred continuations.");
    }
    {
        EditorRenderWorker worker;
        EditorRenderWorker::Snapshot snapshot;
        auto& owner = worker.m_OwnerStates[snapshot.ownerId];
        std::promise<RawRgbDenoiseAsyncResult> promise;
        owner.denoise->future = promise.get_future();
        promise.set_value({});
        owner.denoise->pending = true;
        worker.RetainDenoiseContinuationLocked(snapshot, true);
        Require(owner.continuation && owner.denoise->pending && owner.denoise->future.valid() &&
            owner.denoiseInFlight && !owner.denoise->deferred,
            "A real pending model must remain pending, without fabricated deferred work.");
    }
}

void CheckFailure(LaunchMode mode) {
    launchMode = mode;
    RenderPipeline pipeline;
    auto& state = *pipeline.m_RawRgbDenoiseState;
    state.status = "Old status";
    state.lastCompletedError = "Old error";
    state.lastCompletedModelFingerprint = 99;
    const int beforeLaunches = launches;
    Require(pipeline.Launch() == 0, "Launch failure must not publish pass-through pixels.");
    Require(launches == beforeLaunches + 1, "The selected launch path must execute exactly once.");
    Require(!state.pending && !state.deferred && !state.future.valid(),
        "Failed launch must not pretend a continuation or future exists.");
    Require(state.modelFingerprint == 0 && state.applicationFingerprint == 0 && !state.cancel,
        "Failed launch must release active identity and cancellation state.");
    Require(state.lastCompletedModelFingerprint == 101 &&
        state.lastCompletedError == "Could not start asynchronous AI denoise.",
        "Failed launch must preserve the failed model identity and diagnosis.");
    Require(state.error == state.lastCompletedError && state.status.empty(),
        "The current render must carry its launch failure, not background status.");
    Require(!pipeline.m_HasProvisionalRawRgbDenoiseOutput && pipeline.exposureCalls == 0,
        "Failed launch must not create provisional output.");
    state.error.clear();
    Require(pipeline.RetryGuard(101) == 0 && state.error == state.lastCompletedError,
        "The same failed model must report its memoized error rather than relaunch.");
    Require(pipeline.RetryGuard(102) == 1,
        "A different model identity must remain eligible for normal processing.");
}

int main() {
    try {
        CheckCompletionInvalidation();
        CheckSynchronousRecovery();
        CheckDeferredRetention();
        CheckFailure(LaunchMode::StandardException);
        CheckFailure(LaunchMode::UnknownException);
        launchMode = LaunchMode::Success;
        RenderPipeline pipeline;
        auto& state = *pipeline.m_RawRgbDenoiseState;
        state.lastCompletedError = "Old error";
        state.lastCompletedModelFingerprint = 99;
        Require(pipeline.Launch() == 73 && pipeline.exposureCalls == 1 &&
            pipeline.m_HasProvisionalRawRgbDenoiseOutput,
            "Successful launch must retain the responsive provisional output.");
        Require(state.pending && state.future.valid() && state.cancel &&
            state.modelFingerprint == 101 && state.applicationFingerprint == 202,
            "Successful launch must retain real continuation state.");
        Require(state.error.empty() && state.lastCompletedError.empty() &&
            state.lastCompletedModelFingerprint == 0,
            "Successful launch must clear obsolete failure identity.");
        std::cout << "Denoise launch failure checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
'''

args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(pre + '\n' + set_string + '\n' + fixture + launch_branch +
                       '\n' + tail + failure_guard + '\n' + tests +
                       '\n' + provisional_output + '\n' +
                       '\n' + completion_ready + '\n' + consume_completion + '\n' +
                       '\nunsigned int RenderPipeline::SyncPublish(std::size_t modelFingerprint) {\n'
                       'std::vector<float> outputRgba;\n' + sync_publication + '\n}\n' +
                       '\n' + currentness + '\n' + retention + '\n' + checks)
