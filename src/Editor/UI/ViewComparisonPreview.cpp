#include "ViewComparisonPreview.h"
#include "Async/TaskSystem.h"
#include "BurstEngine/Client.h"
#include "Editor/RawRenderService.h"
#include "Raw/RawGraphOperation.h"
#include "Raw/Bracketing/InspectionImage.h"
#include "Raw/MultiFrameDenoise/MemoryPolicy.h"
#include "Renderer/GLHelpers.h"
#include "Renderer/RenderPipeline.h"
#include <imgui.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <stdexcept>

namespace Stack::Editor {
struct ViewComparisonPreview::Job {
    std::atomic<bool> canceled{false}, done{false};
    std::vector<unsigned char> pixels;
    int width = 0, height = 0;
    std::string error;
};

namespace {
RawRecipe::RawDevelopmentRecipe Baseline(const RawRecipe::RawDevelopmentRecipe& current) {
    auto recipe = RawRecipe::BuildUneditedSourceRecipe(current);
    // Restore only the standard display encoding, never the authored View curve.
    recipe.viewTransform.layerJson = RawRecipe::DefaultViewTransformJson();
    recipe.technical.encodeSrgbOutput = true;
    // Geometry must agree with the processed image being compared.
    recipe.cropRotation = current.cropRotation;
    return recipe;
}

std::shared_ptr<const Raw::RawImageData> CaptureRaw(
    const Raw::Bracketing::CapturePreview& image, const Raw::RawMetadata& metadata,
    const std::string& identity, bool matched) {
    auto raw = std::make_shared<Raw::RawImageData>();
    raw->metadata = metadata;
    auto& m = raw->metadata;
    m.rawWidth = m.visibleWidth = image.width;
    m.rawHeight = m.visibleHeight = image.height;
    m.leftMargin = m.topMargin = 0;
    m.pixelLayout = Raw::RawPixelLayout::LinearRgb;
    m.mosaiced = false; m.cfaPattern = Raw::CfaPattern::Unknown;
    m.linearChannels = 3; m.linearSampleFormat = Raw::RawSampleFormat::Float32;
    m.dngActiveArea = {0, 0, int(image.height), int(image.width)};
    m.dngGainMaps.clear(); m.dngGainMapCount = 0;
    m.dngLinearizationTable.clear(); m.dngBlackLevelValues.clear();
    m.dngBlackLevelDeltaH.clear(); m.dngBlackLevelDeltaV.clear();
    m.blackLevel = 0; m.perChannelBlack = {0, 0, 0, 0}; m.whiteLevel = 1;
    raw->reconstructedCameraRgb = true;
    raw->linearFloatBuffer = image.rgb;
    if (matched) for (auto& value : raw->linearFloatBuffer) value *= float(image.exposureScale);
    if (!image.coverage.empty()) raw->outputCoverage =
        std::make_shared<const std::vector<float>>(image.coverage);
    raw->contentIdentity = identity;
    raw->contentIdentityHash = std::hash<std::string>{}(identity);
    m.sourceContentSha256 = identity;
    m.sourceByteSize = raw->linearFloatBuffer.size() * sizeof(float);
    return raw;
}
}

ViewComparisonPreview::~ViewComparisonPreview() { Reset(); }
void ViewComparisonPreview::RetireTexture() {
    if (m_Texture) m_RetiredTextures.push_back({m_Texture, ImGui::GetFrameCount()});
    m_Texture = 0;
}
void ViewComparisonPreview::CollectRetiredTextures() {
    const int frame = ImGui::GetFrameCount();
    for (auto it = m_RetiredTextures.begin(); it != m_RetiredTextures.end();) {
        if (it->frame < frame) { glDeleteTextures(1, &it->texture); it = m_RetiredTextures.erase(it); }
        else ++it;
    }
}
void ViewComparisonPreview::Suspend() {
    if (m_Job) { m_Job->canceled = true; m_Dirty = true; }
}
void ViewComparisonPreview::Invalidate() {
    Suspend();
    RetireTexture();
    m_Texture = 0; m_Width = m_Height = 0;
    m_Dirty = true; m_Status.clear();
}
void ViewComparisonPreview::Reset() {
    Suspend();
    if (m_Texture) glDeleteTextures(1, &m_Texture);
    for (const auto& retired : m_RetiredTextures) glDeleteTextures(1, &retired.texture);
    m_RetiredTextures.clear(); m_Texture = 0; m_Width = m_Height = 0;
    m_Dirty = true; m_Status.clear();
    m_Job.reset(); m_Source.reset(); m_Bracket.reset();
    m_Captures.clear(); m_Selected.clear(); m_RecipeKey.clear();
    m_Revision = ~std::uint64_t(0); m_Edge = 0; m_Matched = false;
    m_ControlChanged = false; m_SelectedLabel.clear();
}
void ViewComparisonPreview::SetUnavailable(const char* reason) {
    if (m_Texture || m_Job) Invalidate();
    m_Bracket.reset(); m_Source.reset(); m_Captures.clear();
    m_RecipeKey.clear(); m_Revision = ~std::uint64_t(0);
    m_Status = reason;
}
bool ViewComparisonPreview::NeedsBracketCatalog(const Raw::Bracketing::BracketingResult* result) const {
    return m_Bracket.get() != result;
}
void ViewComparisonPreview::Poll() {
    CollectRetiredTextures();
    if (!m_Job || !m_Job->done.load()) return;
    auto job = std::move(m_Job);
    if (job->canceled) return;
    m_Status = job->error;
    if (!job->pixels.empty() && job->width > 0 && job->height > 0) {
        m_Texture = GLHelpers::CreateTextureFromPixels(job->pixels.data(), job->width, job->height, 4);
        m_Width = job->width; m_Height = job->height;
        if (!m_Texture) m_Status = "Could not upload the before image.";
    } else if (m_Status.empty()) m_Status = "The before image is unavailable.";
}
void ViewComparisonPreview::UpdateRaw(const std::shared_ptr<const Raw::RawImageData>& source,
    const RawRecipe::RawDevelopmentRecipe& recipe, std::uint64_t revision, int edge) {
    if (m_Bracket || m_Source != source || m_Revision != revision || m_Edge != edge) {
        const auto baseline = Baseline(recipe);
        const auto key = RawRecipe::SerializeRecipe(baseline).dump();
        if (m_Bracket || m_Source != source || m_RecipeKey != key || m_Edge != edge) {
            Invalidate(); m_Recipe = baseline; m_RecipeKey = key;
        }
        m_Bracket.reset(); m_Captures.clear(); m_Source = source;
        m_Revision = revision; m_Edge = edge;
    }
    Poll();
    if (m_Dirty && !m_Job) Start(m_Source, m_Recipe);
}
void ViewComparisonPreview::UpdateBracket(
    const std::shared_ptr<const Raw::Bracketing::BracketingResult>& result,
    const Raw::Bracketing::BracketingRecipe& recipe, const std::vector<Capture>& captures,
    const RawRecipe::RawDevelopmentRecipe& development, std::uint64_t revision, int edge) {
    if (m_ControlChanged) { Invalidate(); m_ControlChanged = false; }
    if (m_Bracket != result || m_Revision != revision) {
        auto baseline = Baseline(development);
        const auto key = RawRecipe::SerializeRecipe(baseline).dump();
        if (m_RecipeKey != key) { Invalidate(); m_Recipe = std::move(baseline); m_RecipeKey = key; }
        m_Revision = revision;
    }
    if (m_Bracket != result) {
        Invalidate(); m_Source.reset(); m_Bracket = result;
        m_BracketRecipe = recipe; m_Captures = captures;
        const auto available = [&](const Capture& capture) { return capture.unavailable.empty(); };
        auto selected = std::find_if(m_Captures.begin(), m_Captures.end(), [&](const Capture& capture) {
            return capture.id == m_Selected && available(capture);
        });
        if (selected == m_Captures.end()) selected = std::find_if(m_Captures.begin(), m_Captures.end(), [&](const Capture& capture) {
            return capture.id == recipe.originFrameId && available(capture);
        });
        if (selected == m_Captures.end()) selected = std::find_if(m_Captures.begin(), m_Captures.end(), available);
        m_Selected = selected == m_Captures.end() ? std::string() : selected->id;
        m_SelectedLabel = selected == m_Captures.end() ? "Unavailable" : selected->label;
    }
    if (m_Edge != edge) { Invalidate(); m_Edge = edge; }
    Poll();
    if (m_Selected.empty()) { m_Status = "No prepared originals are available for this bracket."; return; }
    if (m_Dirty && !m_Job) Start({}, m_Recipe, result);
}
void ViewComparisonPreview::DrawControls() {
    if (!m_Bracket) return;
    ImGui::SetNextItemWidth(260.f);
    if (ImGui::BeginCombo("Before", m_SelectedLabel.c_str())) {
        for (const auto& capture : m_Captures) {
            ImGui::PushID(capture.id.c_str());
            ImGui::BeginDisabled(!capture.unavailable.empty());
            if (ImGui::Selectable(capture.label.c_str(), capture.id == m_Selected)) {
                m_Selected = capture.id; m_SelectedLabel = capture.label; m_ControlChanged = true;
            }
            ImGui::EndDisabled();
            if (!capture.unavailable.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("%s", capture.unavailable.c_str());
            ImGui::PopID();
        }
        ImGui::Separator();
        if (ImGui::Checkbox("Exposure match", &m_Matched)) m_ControlChanged = true;
        ImGui::EndCombo();
    }
}
void ViewComparisonPreview::Start(std::shared_ptr<const Raw::RawImageData> raw,
    RawRecipe::RawDevelopmentRecipe recipe,
    std::shared_ptr<const Raw::Bracketing::BracketingResult> bracket) {
    auto job = std::make_shared<Job>(); m_Job = job;
    m_Dirty = false; m_Status = "Loading before image...";
    const auto capture = m_Selected;
    const auto bracketRecipe = m_BracketRecipe;
    const bool matched = m_Matched;
    const int edge = m_Edge;
    if (!Async::TaskSystem::Get().SubmitHighPriority("Preparing comparison", [job, raw, recipe, bracket, capture, bracketRecipe, matched, edge]() mutable {
        try {
            if (bracket && !job->canceled) {
                Raw::Bracketing::ProcessingRequest request;
                request.recipe = bracketRecipe; request.analysis = bracket->analysis;
                const auto budget = Raw::Mfd::ResolveMfdProcessingMemoryBudget(0, Raw::Mfd::QueryPhysicalMemorySnapshot());
                request.memoryBudgetBytes = budget.valid ? budget.budgetBytes : 0;
                request.shouldCancel = [job] { return job->canceled.load(); };
                auto image = BurstEngine::RenderInspectionImage(request, *bracket,
                    Raw::Bracketing::InspectionImageKind::Capture, capture, 0, unsigned(edge));
                if (!image || !bracket->raw) throw std::runtime_error("This original is not available in the prepared bracket.");
                raw = CaptureRaw(*image, bracket->raw->metadata,
                    "view-before/" + bracket->identity + "/" + capture + (matched ? "/matched" : "/captured"), matched);
            }
            if (!job->canceled) {
                EditorRendering::RawRenderService::Get().ExecuteOpenGlTaskBlocking(
                    [job, raw, recipe, edge](std::string& error) {
                        if (job->canceled) return false;
                        RenderPipeline pipeline;
                        pipeline.Initialize();
                        pipeline.SetPreviewMaxDimension(edge);
                        pipeline.SetRawDevelopmentAnalysisEnabled(false);
                        pipeline.SetRawDevelopmentInteractivePreview(true);
                        pipeline.SetRenderCancellationContext(1, [job] { return job->canceled.load(); });
                        RenderGraphSnapshot graph;
                        RenderGraphNode source; source.nodeId = 1;
                        source.kind = RenderGraphNodeKind::RawDevelopment;
                        source.rawDevelopment.recipe = recipe;
                        source.rawDevelopment.embeddedRawData = raw;
                        graph.nodes.push_back(std::move(source));
                        RenderGraphNode output; output.nodeId = 2; output.kind = RenderGraphNodeKind::Output;
                        graph.nodes.push_back(std::move(output));
                        graph.links.push_back({1, "imageOut", 2, "imageIn"});
                        graph.outputNodeId = 2; graph.outputSocketId = "imageOut";
                        pipeline.ExecuteGraph(graph);
                        if (!job->canceled) job->pixels = pipeline.GetOutputPixels(job->width, job->height);
                        if (job->pixels.empty()) error = "Could not render the before image.";
                        return !job->pixels.empty();
                    }, job->error, Raw::OpenGlTaskPriority::Background);
            }
        } catch (const std::exception& error) { job->error = error.what(); }
        catch (...) { job->error = "Could not prepare the before image."; }
        job->done = true;
    })) { job->error = "Could not schedule the before image."; job->done = true; }
}
}
