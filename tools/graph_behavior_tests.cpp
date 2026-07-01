#include "Color/LutCreator.h"
#include "Color/LutImporter.h"
#include "Editor/LayerRegistry.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/EditorNodeGraphDefinitions.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Library/LibraryManager.h"
#include "MFSR/MFSRTypes.h"
#include "Raw/RawAutoBase.h"
#include "Raw/RawDevelopmentRecipe.h"
#include "Raw/RawImageAnalysis.h"
#include "Raw/RawLoader.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawWorkspaceManagedGraph.h"
#include "Renderer/RawPreviewProxy.h"
#include "Renderer/RenderTiling.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"

#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

std::shared_ptr<LayerBase> NullLayerFactory() {
    return nullptr;
}

const std::vector<LayerDescriptor>& TestLayerDescriptors() {
    static const std::vector<LayerDescriptor> descriptors = {
        { LayerType::Brightness, "Brightness", "Brightness", "Brightness", "Color", "Adjust brightness.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::ChannelSafe },
        { LayerType::Contrast, "Contrast", "Contrast", "Contrast", "Color", "Adjust contrast.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::ChannelSafe },
        { LayerType::Saturation, "Saturation", "Saturation", "Saturation", "Color", "Adjust saturation.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::FullImagePreferred },
        { LayerType::ToneCurve, "ToneCurve", "Tone Curve", "Tone Curve", "Color / Tone", "Manual scene-referred finish curve.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::FullImagePreferred },
        { LayerType::ViewTransform, "ViewTransform", "View Transform", "View Transform", "Color / Tone", "Display/output transform.", {}, NullLayerFactory, LayerLifecycleStatus::Stable, LayerChannelPolicy::FullImagePreferred },
    };
    return descriptors;
}

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

} // namespace

void LibraryManager::FlipImageRowsInPlace(std::vector<unsigned char>& pixels, int width, int height, int channels) {
    if (width <= 0 || height <= 1 || channels <= 0) {
        return;
    }
    const int rowBytes = width * channels;
    if (static_cast<int>(pixels.size()) < rowBytes * height) {
        return;
    }
    std::vector<unsigned char> scratch(static_cast<std::size_t>(rowBytes));
    for (int y = 0; y < height / 2; ++y) {
        unsigned char* top = pixels.data() + static_cast<std::size_t>(y * rowBytes);
        unsigned char* bottom = pixels.data() + static_cast<std::size_t>((height - 1 - y) * rowBytes);
        std::copy(top, top + rowBytes, scratch.begin());
        std::copy(bottom, bottom + rowBytes, top);
        std::copy(scratch.begin(), scratch.end(), bottom);
    }
}

namespace {

int NodeId(const EditorNodeGraph::Node* node) {
    Require(node != nullptr, "node allocation failed");
    return node->id;
}

EditorNodeGraph::ImagePayload TestImagePayload() {
    EditorNodeGraph::ImagePayload payload;
    payload.label = "Test Image";
    payload.width = 4;
    payload.height = 4;
    payload.channels = 4;
    payload.originalChannels = 4;
    payload.pixels.assign(4 * 4 * 4, 255);
    return payload;
}

std::string WriteTempTextFile(const std::string& stem, const std::string& extension, const std::string& contents) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (stem + "_" + std::to_string(std::rand()) + extension);
    std::ofstream out(path, std::ios::binary);
    Require(out.good(), "failed to open temporary test file");
    out << contents;
    Require(out.good(), "failed to write temporary test file");
    out.close();
    return path.string();
}

std::filesystem::path MakeTempDirectory(const std::string& stem) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        (stem + "_" + std::to_string(std::rand()));
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
    ec.clear();
    std::filesystem::create_directories(path, ec);
    Require(!ec, "failed to create temporary test directory");
    return path;
}

void WriteRawWorkspaceTestFile(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    Require(!ec, "failed to create temporary RAW parent directory");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Require(out.good(), "failed to open temporary RAW test file");
    out << "raw-placeholder";
    Require(out.good(), "failed to write temporary RAW test file");
}

void WriteRawWorkspaceJsonFile(const std::filesystem::path& path, const nlohmann::json& json) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    Require(!ec, "failed to create temporary JSON parent directory");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Require(out.good(), "failed to open temporary JSON file");
    out << json.dump(2);
    Require(out.good(), "failed to write temporary JSON file");
}

nlohmann::json ReadRawWorkspaceJsonFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    Require(in.good(), "failed to open temporary JSON file for reading");
    nlohmann::json json;
    in >> json;
    Require(!in.fail(), "failed to parse temporary JSON file");
    return json;
}

void WriteRawWorkspaceBinaryFile(const std::filesystem::path& path, const std::string& contents) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    Require(!ec, "failed to create temporary binary parent directory");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    Require(out.good(), "failed to open temporary binary file");
    out << contents;
    Require(out.good(), "failed to write temporary binary file");
}

nlohmann::json BuildTestThumbnailSignatureJson(const Stack::RawWorkspace::ThumbnailSignature& signature) {
    nlohmann::json value = nlohmann::json::object();
    value["schema"] = "stack.rawWorkspace.thumbnailSignature";
    value["schemaVersion"] = signature.schemaVersion;
    value["sourceRelativePath"] = signature.sourceRelativePath;
    value["sourceFileSizeBytes"] = signature.sourceFileSizeBytes;
    value["sourceModifiedTimeTicks"] = signature.sourceModifiedTimeTicks;
    value["sourceFingerprint"] = signature.sourceFingerprint.empty() ? nlohmann::json() : nlohmann::json(signature.sourceFingerprint);
    value["rawLoaderAlgorithmVersion"] = signature.rawLoaderAlgorithmVersion;
    value["neutralPreviewSettingsVersion"] = signature.neutralPreviewSettingsVersion;
    value["thumbnailVersion"] = signature.thumbnailVersion;
    value["maxDimension"] = signature.maxDimension;
    value["thumbnailWidth"] = 8;
    value["thumbnailHeight"] = 6;
    return value;
}

void TestScalarMaskCanUseLayerMath() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 0.0f }));
    const int brightnessId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 200.0f, 0.0f }));
    const int contrastId = NodeId(graph.AddLayerNode(LayerType::Contrast, 1, { 400.0f, 0.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 600.0f, 0.0f }));

    Require(graph.CanConnectSockets(maskId, kMaskOutputSocketId, brightnessId, kImageInputSocketId),
        "mask scalar output should be allowed into a layer image input");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, brightnessId, kImageInputSocketId),
        "mask scalar output should connect into layer image input");
    Require(graph.IsScalarSocketStream(brightnessId, kImageOutputSocketId),
        "layer image output should keep scalar lineage when its image input is scalar");

    Require(graph.CanConnectSockets(brightnessId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "scalar layer output should feed another layer image input");
    Require(graph.TryConnectSockets(brightnessId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "scalar layer output should connect to another layer image input");
    Require(graph.IsScalarSocketStream(contrastId, kImageOutputSocketId),
        "chained layer image output should keep scalar lineage");

    Require(graph.CanConnectSockets(contrastId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "scalar image output should be allowed into scalar factor inputs");
    Require(graph.TryConnectSockets(contrastId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "scalar image output should connect to scalar factor inputs");
}

void TestFullImageStillCannotTargetScalarInput() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 220.0f, 0.0f }));

    std::string error;
    Require(!graph.CanConnectSockets(imageId, kImageOutputSocketId, mixId, kMixFactorSocketId, nullptr, &error),
        "full image output should not connect directly to scalar factor input");
    Require(!error.empty(), "rejected full-image-to-scalar connection should explain why");
    Require(graph.CanInsertImageToScalarExtractor(imageId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "full image output should be convertible into a scalar factor through an extractor");
    Require(graph.CanConnectSocketsOrInsertExtractor(imageId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "permissive compatibility should allow full image to scalar factor with extractor insertion");
}

void TestOutputChannelNormalization() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int splitId = NodeId(graph.AddChannelSplitNode({ 220.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 440.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, splitId, kImageInputSocketId),
        "image should connect to channel split input");

    std::string normalized;
    Require(graph.CanConnectSockets(splitId, "r", outputId, kImageInputSocketId, &normalized),
        "channel output should be accepted when dropped on full output input");
    Require(normalized == "r", "channel output dropped on output image input should normalize to matching RGBA socket");
    Require(graph.TryConnectSockets(splitId, "r", outputId, kImageInputSocketId),
        "channel output should connect to normalized RGBA output input");
    Require(graph.HasLink(splitId, "r", outputId, "r"),
        "normalized output channel link should be stored on the R socket");
}

void TestScalarCyclesAreRejected() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 0.0f }));
    const int brightnessId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 220.0f, 0.0f }));
    const int contrastId = NodeId(graph.AddLayerNode(LayerType::Contrast, 1, { 440.0f, 0.0f }));

    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, brightnessId, kImageInputSocketId),
        "mask should connect to layer image input");
    Require(graph.TryConnectSockets(brightnessId, kImageOutputSocketId, contrastId, kImageInputSocketId),
        "layer should connect downstream");
    Require(!graph.CanConnectSockets(contrastId, kImageOutputSocketId, brightnessId, kMaskInputSocketId),
        "scalar/image feedback into an upstream layer mask should be rejected as a cycle");
}

void TestCustomMaskConnections() {
    using namespace EditorNodeGraph;

    Graph graph;
    CustomMaskPayload payload;
    payload.width = 4;
    payload.height = 4;
    payload.rasterLayer.assign(16, 0.5f);

    const int maskId = NodeId(graph.AddCustomMaskNode(payload, { 0.0f, 0.0f }));
    const int previewId = NodeId(graph.AddPreviewNode({ 220.0f, 0.0f }));
    const int layerId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 440.0f, 0.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 660.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 880.0f, 0.0f }, true));

    Require(graph.IsScalarSocketStream(maskId, kMaskOutputSocketId),
        "custom mask output should be treated as a scalar stream");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, previewId, kPreviewInputSocketId),
        "custom mask should connect directly to preview nodes");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, layerId, kMaskInputSocketId),
        "custom mask should connect to layer mask inputs");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, mixId, kMixFactorSocketId),
        "custom mask should connect to mix factor inputs");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, outputId, "a"),
        "custom mask should connect to output RGBA channel pins");
    Require(!graph.CanConnectSockets(maskId, kMaskOutputSocketId, maskId, kMaskOutputSocketId),
        "custom mask should reject self-connections");
}

void TestCustomMaskThroughMaskCombineExclude() {
    using namespace EditorNodeGraph;

    Graph graph;
    CustomMaskPayload payload;
    payload.width = 2;
    payload.height = 2;
    payload.rasterLayer.assign(4, 1.0f);

    const int customA = NodeId(graph.AddCustomMaskNode(payload, { 0.0f, 0.0f }));
    const int customB = NodeId(graph.AddCustomMaskNode(payload, { 0.0f, 120.0f }));
    const int combineId = NodeId(graph.AddMaskCombineNode(MaskCombineMode::Exclude, { 220.0f, 0.0f }));
    const int previewId = NodeId(graph.AddPreviewNode({ 440.0f, 0.0f }));

    Require(graph.TryConnectSockets(customA, kMaskOutputSocketId, combineId, kMaskCombineInputASocketId),
        "custom mask should connect to exclude combine A");
    Require(graph.TryConnectSockets(customB, kMaskOutputSocketId, combineId, kMaskCombineInputBSocketId),
        "custom mask should connect to exclude combine B");
    Require(graph.IsScalarSocketStream(combineId, kMaskOutputSocketId),
        "exclude mask combine should preserve scalar stream classification");
    Require(graph.TryConnectSockets(combineId, kMaskOutputSocketId, previewId, kPreviewInputSocketId),
        "exclude mask combine should preview as a scalar output");
}

void TestManualRawBaselineChainShape() {
    using namespace EditorNodeGraph;

    Graph graph;
    RawSourcePayload rawPayload;
    rawPayload.label = "RAW";
    rawPayload.sourcePath = "manual-baseline.dng";

    const int rawSourceId = NodeId(graph.AddRawSourceNode(rawPayload, { 0.0f, 0.0f }));
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(RawDecodePayload{}, { 280.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 560.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 840.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1120.0f, 0.0f }, true));

    std::string rawToImageError;
    Require(!graph.CanConnectSockets(rawSourceId, kRawOutputSocketId, toneCurveId, kImageInputSocketId, nullptr, &rawToImageError),
        "RAW source should not connect directly to an image layer input");
    Require(!rawToImageError.empty(),
        "rejected RAW-to-image connection should explain why");

    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "manual RAW baseline should connect RAW Source to RAW Decode");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "manual RAW baseline should connect RAW Decode to Tone Curve");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "manual RAW baseline should connect Tone Curve to View Transform");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "manual RAW baseline should connect View Transform to Output");
    Require(graph.IsOutputConnected(),
        "manual RAW baseline should complete an output chain");
}

void TestRawWorkspaceFolderCatalogFoundation() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_workspace_test");
    const std::filesystem::path dayOne = root / "Day 1";
    const std::filesystem::path nested = dayOne / "Nested";

    WriteRawWorkspaceTestFile(root / "root_image.ARW");
    WriteRawWorkspaceTestFile(dayOne / "image_0001.DNG");
    WriteRawWorkspaceTestFile(nested / "image_0002.raw");
    WriteRawWorkspaceTestFile(dayOne / "sidecar.xmp");

    std::string error;
    Require(RawWorkspace::EnsureManagedFolders(root, &error),
        "RAW Workspace should create managed folders");
    const RawWorkspace::ManagedLayout layout = RawWorkspace::BuildManagedLayout(root);
    Require(std::filesystem::exists(layout.thumbnailsDirectory),
        "RAW Workspace thumbnails folder should exist");
    Require(std::filesystem::exists(layout.projectsDirectory),
        "RAW Workspace projects folder should exist");
    Require(std::filesystem::exists(layout.catalogDirectory),
        "RAW Workspace catalog folder should exist");

    WriteRawWorkspaceTestFile(layout.thumbnailsDirectory / "hidden_thumb_source.DNG");
    WriteRawWorkspaceTestFile(layout.projectsDirectory / "hidden_project_source.ARW");
    WriteRawWorkspaceTestFile(layout.catalogDirectory / "hidden_catalog_source.raw");

    RawWorkspace::ScanProgress lastProgress;
    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(
        root,
        RawWorkspace::DefaultRawPathPredicate,
        [&](const RawWorkspace::ScanProgress& progress) {
            lastProgress = progress;
        });
    Require(scan.success, "RAW Workspace scan should succeed");
    Require(scan.sources.size() == 3, "RAW Workspace scan should find only user RAW files");
    Require(lastProgress.discoveredRawCount == 3, "RAW Workspace scan progress should count discovered RAW files");
    Require(scan.progress.managedDirectoriesSkipped >= 3,
        "RAW Workspace scan should skip managed folders");

    const auto hasSource = [&](const std::string& relativePath) {
        return std::any_of(scan.sources.begin(), scan.sources.end(), [&](const RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == relativePath;
        });
    };
    Require(hasSource("root_image.ARW"), "RAW Workspace scan should include root RAW files");
    Require(hasSource("Day 1/image_0001.DNG"), "RAW Workspace scan should include subfolder RAW files");
    Require(hasSource("Day 1/Nested/image_0002.raw"), "RAW Workspace scan should include nested RAW files");
    Require(!hasSource("Stack RAW Thumbnails/hidden_thumb_source.DNG"),
        "RAW Workspace scan should exclude thumbnail managed folder RAWs");
    Require(!hasSource("Stack RAW Projects/hidden_project_source.ARW"),
        "RAW Workspace scan should exclude project managed folder RAWs");
    Require(!hasSource("Stack RAW Catalog/hidden_catalog_source.raw"),
        "RAW Workspace scan should exclude catalog managed folder RAWs");

    const auto dayOneIt = std::find_if(scan.sources.begin(), scan.sources.end(), [&](const RawWorkspace::SourceRecord& source) {
        return source.relativePathKey == "Day 1/image_0001.DNG";
    });
    Require(dayOneIt != scan.sources.end(), "RAW Workspace test source should exist");
    Require(dayOneIt->parentFolderKey == "Day 1",
        "RAW Workspace source records should preserve parent folder grouping");

    Require(RawWorkspace::WriteCatalogSkeleton(scan.layout, scan.sources, "Day 1/image_0001.DNG", &error),
        "RAW Workspace should write catalog skeleton");
    Require(std::filesystem::exists(scan.layout.catalogPath),
        "RAW Workspace catalog.json should exist");
    Require(std::filesystem::exists(scan.layout.ratingsPath),
        "RAW Workspace ratings.json skeleton should exist");
    const nlohmann::json catalog = ReadRawWorkspaceJsonFile(scan.layout.catalogPath);
    Require(catalog.value("schema", std::string()) == "stack.rawWorkspace.catalog",
        "RAW Workspace catalog should preserve schema");
    Require(catalog["sources"].is_array(),
        "RAW Workspace catalog should serialize source array");
    Require(catalog["sources"].size() == scan.sources.size(),
        "RAW Workspace catalog should serialize every source");
    for (std::size_t index = 0; index < scan.sources.size(); ++index) {
        Require(catalog["sources"][index] == RawWorkspace::SerializeSourceRecord(scan.sources[index]),
            "RAW Workspace compact catalog snapshot should match source record serialization");
    }

    RawWorkspace::WorkspaceState state;
    state.workspaceRoot = scan.layout.workspaceRoot;
    state.sources = scan.sources;
    Require(RawWorkspace::SelectSourceByKey(state, "Day 1/image_0001.DNG"),
        "RAW Workspace should select a source record");
    Require(state.selectedSourceKey == "Day 1/image_0001.DNG",
        "RAW Workspace selection should be preview-only state");

    bool createdProject = false;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             scan.layout.projectsDirectory,
             std::filesystem::directory_options::skip_permission_denied,
             ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec) && !ec && entry.path().extension() == ".stack") {
            createdProject = true;
            break;
        }
        ec.clear();
    }
    Require(!createdProject, "RAW Workspace source selection should not create .stack projects");

    ec.clear();
    std::filesystem::remove_all(root, ec);
}

void TestRawWorkspaceThumbnailPipelineFoundation() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_thumbnail_test");
    WriteRawWorkspaceTestFile(root / "Day 1" / "image_0001.DNG");
    WriteRawWorkspaceTestFile(root / "Day 2" / "image_0002.ARW");
    WriteRawWorkspaceTestFile(root / "image_0003.raw");

    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(
        root,
        RawWorkspace::DefaultRawPathPredicate);
    Require(scan.success, "RAW Workspace thumbnail test scan should succeed");
    Require(scan.sources.size() == 3, "RAW Workspace thumbnail test should discover three source records");

    auto sourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& source) {
        return source.relativePathKey == "Day 1/image_0001.DNG";
    });
    Require(sourceIt != scan.sources.end(), "thumbnail test source should exist");

    RawWorkspace::ThumbnailInfo info = RawWorkspace::BuildThumbnailInfo(scan.layout, *sourceIt);
    Require(info.relativePath.generic_string() == "Day 1/image_0001.thumb.png",
        "thumbnail path should mirror source subfolder");
    Require(info.signatureRelativePath.generic_string() == "Day 1/image_0001.thumb.json",
        "thumbnail signature path should mirror source subfolder");

    WriteRawWorkspaceBinaryFile(info.absolutePath, "not-a-real-png-but-present");
    WriteRawWorkspaceJsonFile(
        info.signaturePath,
        BuildTestThumbnailSignatureJson(RawWorkspace::BuildThumbnailSignature(*sourceIt)));
    RawWorkspace::ThumbnailStatus validStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(validStatus == RawWorkspace::ThumbnailStatus::Valid,
        "matching thumbnail signature should classify as valid");
    Require(sourceIt->thumbnail.width == 8 && sourceIt->thumbnail.height == 6,
        "thumbnail dimensions should be loaded from signature sidecar");

    nlohmann::json nullDimensionSignature =
        BuildTestThumbnailSignatureJson(RawWorkspace::BuildThumbnailSignature(*sourceIt));
    nullDimensionSignature["thumbnailWidth"] = nullptr;
    nullDimensionSignature["thumbnailHeight"] = nullptr;
    WriteRawWorkspaceJsonFile(info.signaturePath, nullDimensionSignature);
    RawWorkspace::ThumbnailStatus nullDimensionStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(nullDimensionStatus == RawWorkspace::ThumbnailStatus::Valid,
        "null optional thumbnail dimensions should not invalidate a matching signature");
    Require(sourceIt->thumbnail.width == 0 && sourceIt->thumbnail.height == 0,
        "null optional thumbnail dimensions should fall back safely");

    nlohmann::json staleSignature = BuildTestThumbnailSignatureJson(RawWorkspace::BuildThumbnailSignature(*sourceIt));
    staleSignature["sourceFileSizeBytes"] = sourceIt->fileSizeBytes + 10;
    WriteRawWorkspaceJsonFile(info.signaturePath, staleSignature);
    RawWorkspace::ThumbnailStatus staleStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(staleStatus == RawWorkspace::ThumbnailStatus::Stale,
        "changed source signature should classify existing thumbnail as stale");

    nlohmann::json nullRequiredSignature =
        BuildTestThumbnailSignatureJson(RawWorkspace::BuildThumbnailSignature(*sourceIt));
    nullRequiredSignature["sourceRelativePath"] = nullptr;
    WriteRawWorkspaceJsonFile(info.signaturePath, nullRequiredSignature);
    RawWorkspace::ThumbnailStatus nullRequiredStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(nullRequiredStatus == RawWorkspace::ThumbnailStatus::Stale,
        "null required thumbnail signature fields should classify as stale");

    WriteRawWorkspaceTestFile(info.signaturePath);
    RawWorkspace::ThumbnailStatus malformedSignatureStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *sourceIt);
    Require(malformedSignatureStatus == RawWorkspace::ThumbnailStatus::Stale,
        "malformed thumbnail signature JSON should classify as stale");

    auto missingIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& source) {
        return source.relativePathKey == "Day 2/image_0002.ARW";
    });
    Require(missingIt != scan.sources.end(), "missing thumbnail test source should exist");
    RawWorkspace::ThumbnailStatus missingStatus = RawWorkspace::ClassifyThumbnail(scan.layout, *missingIt);
    Require(missingStatus == RawWorkspace::ThumbnailStatus::Missing,
        "absent thumbnail files should classify as missing");

    RawWorkspace::ClassifyThumbnails(scan.layout, scan.sources);
    RawWorkspace::ThumbnailProgress progress = RawWorkspace::BuildThumbnailProgress(scan.sources);
    Require(progress.total == 3, "thumbnail progress should count all sources");
    Require(progress.queued >= 2, "missing/stale thumbnails should be counted as queued work");

    RawWorkspace::SourceRecord invalidSource = *missingIt;
    invalidSource.absolutePath.clear();
    RawWorkspace::ThumbnailGenerationResult generated =
        RawWorkspace::GenerateNeutralThumbnail(scan.layout, invalidSource);
    Require(!generated.success, "invalid RAW source should fail neutral thumbnail generation");

    bool createdProject = false;
    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             scan.layout.projectsDirectory,
             std::filesystem::directory_options::skip_permission_denied,
             ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file(ec) && !ec && entry.path().extension() == ".stack") {
            createdProject = true;
            break;
        }
        ec.clear();
    }
    Require(!createdProject,
        "RAW Workspace thumbnail generation should not create .stack projects");

    ec.clear();
    std::filesystem::remove_all(root, ec);
}

void TestRawWorkspaceLoadingCancellationModel() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_loading_cancel_test");
    WriteRawWorkspaceTestFile(root / "image_0001.ARW");
    WriteRawWorkspaceTestFile(root / "Day 1" / "image_0002.DNG");
    WriteRawWorkspaceTestFile(root / "Day 1" / "image_0003.raw");

    bool cancelScan = false;
    RawWorkspace::ScanResult canceledScan = RawWorkspace::ScanWorkspace(
        root,
        RawWorkspace::DefaultRawPathPredicate,
        [&](const RawWorkspace::ScanProgress& progress) {
            if (progress.filesVisited >= 1) {
                cancelScan = true;
            }
        },
        [&]() {
            return cancelScan;
        });
    Require(!canceledScan.success, "RAW Workspace scan cancellation should stop the scan");
    Require(canceledScan.errorMessage.find("canceled") != std::string::npos,
        "RAW Workspace scan cancellation should report a canceled status");

    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(
        root,
        RawWorkspace::DefaultRawPathPredicate);
    Require(scan.success, "RAW Workspace cancellation baseline scan should succeed");
    Require(scan.sources.size() == 3, "RAW Workspace cancellation baseline should find all sources");

    bool classifyCanceled = RawWorkspace::ClassifyThumbnails(
        scan.layout,
        scan.sources,
        RawWorkspace::kNeutralThumbnailMaxDimension,
        []() {
            return true;
        });
    Require(!classifyCanceled, "RAW Workspace thumbnail classification should honor cancellation");

    bool discoverCanceled = RawWorkspace::DiscoverProjects(
        scan.layout,
        scan.sources,
        []() {
            return true;
        });
    Require(!discoverCanceled, "RAW Workspace project discovery should honor cancellation");

    RawWorkspace::ThumbnailGenerationResult thumbnailCanceled =
        RawWorkspace::GenerateNeutralThumbnail(
            scan.layout,
            scan.sources.front(),
            RawWorkspace::kNeutralThumbnailMaxDimension,
            []() {
                return true;
            });
    Require(!thumbnailCanceled.success, "RAW thumbnail generation should stop when canceled before decode");
    Require(thumbnailCanceled.thumbnail.status == RawWorkspace::ThumbnailStatus::Queued,
        "canceled RAW thumbnail generation should return the source to queued status");

    Raw::RawImageData canceledRaw;
    Require(!Raw::RawLoader::LoadFile(
            scan.sources.front().absolutePath.string(),
            canceledRaw,
            []() {
                return true;
            }),
        "RAW loader should honor cancellation before starting decode work");
    Require(canceledRaw.metadata.error.find("canceled") != std::string::npos,
        "canceled RAW loader should report a canceled status");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void TestRawWorkspaceJsonReadersTolerateNullOptionalFields() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_workspace_json_test");
    const std::filesystem::path statePath = root / "RawWorkspaceState.json";
    const std::filesystem::path recentRoot = root / "Recent Workspace";

    nlohmann::json state = nlohmann::json::object();
    state["schema"] = "stack.rawWorkspace.appState";
    state["schemaVersion"] = 1;
    state["lastWorkspaceRoot"] = nullptr;
    state["lastSelectedSource"] = nullptr;
    state["recentWorkspaces"] = nlohmann::json::array({
        nullptr,
        7,
        recentRoot.string()
    });
    WriteRawWorkspaceJsonFile(statePath, state);

    RawWorkspace::AppState loaded;
    std::string error;
    Require(RawWorkspace::LoadAppState(statePath, loaded, &error),
        "RAW Workspace app state with null optional fields should load");
    Require(loaded.lastWorkspaceRoot.empty(),
        "null last workspace root should load as empty");
    Require(loaded.lastSelectedSourceKey.empty(),
        "null selected source should load as empty");
    Require(loaded.recentWorkspaceRoots.size() == 1,
        "recent workspace loader should ignore null and non-string entries");

    const std::filesystem::path malformedPath = root / "MalformedRawWorkspaceState.json";
    WriteRawWorkspaceTestFile(malformedPath);
    loaded = {};
    error.clear();
    Require(RawWorkspace::LoadAppState(malformedPath, loaded, &error),
        "malformed RAW Workspace app state should be ignored without aborting");
    Require(loaded.lastWorkspaceRoot.empty() && loaded.recentWorkspaceRoots.empty(),
        "malformed RAW Workspace app state should leave default state");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void TestRawWorkspaceGalleryPresentation() {
    namespace RawWorkspace = Stack::RawWorkspace;

    RawWorkspace::WorkspaceState state;
    state.workspaceRoot = "C:/Workspace";

    RawWorkspace::SourceRecord rootSource;
    rootSource.relativePathKey = "root_image.ARW";
    rootSource.relativePath = "root_image.ARW";
    rootSource.fileName = "root_image.ARW";
    rootSource.fileSizeBytes = 2048;
    rootSource.thumbnail.status = RawWorkspace::ThumbnailStatus::Missing;

    RawWorkspace::SourceRecord dayOneSource;
    dayOneSource.relativePathKey = "Day 1/image_0001.DNG";
    dayOneSource.relativePath = "Day 1/image_0001.DNG";
    dayOneSource.fileName = "image_0001.DNG";
    dayOneSource.parentFolderKey = "Day 1";
    dayOneSource.fileSizeBytes = 4096;
    dayOneSource.thumbnail.status = RawWorkspace::ThumbnailStatus::Ready;
    dayOneSource.thumbnail.relativePath = "Day 1/image_0001.thumb.png";

    RawWorkspace::SourceRecord secondDayOneSource;
    secondDayOneSource.relativePathKey = "Day 1/image_0002.ARW";
    secondDayOneSource.relativePath = "Day 1/image_0002.ARW";
    secondDayOneSource.fileName = "image_0002.ARW";
    secondDayOneSource.parentFolderKey = "Day 1";
    secondDayOneSource.thumbnail.status = RawWorkspace::ThumbnailStatus::Failed;

    state.sources = { rootSource, dayOneSource, secondDayOneSource };
    state.selectedSourceKey = dayOneSource.relativePathKey;

    const RawWorkspace::GalleryPresentation presentation = RawWorkspace::BuildGalleryPresentation(state);
    Require(presentation.totalSources == 3,
        "RAW Workspace gallery presentation should count all sources");
    Require(presentation.groups.size() == 2,
        "RAW Workspace gallery presentation should preserve folder groups");
    Require(presentation.groups[0].label == "Workspace root",
        "RAW Workspace gallery presentation should label root sources");
    Require(presentation.groups[1].folderKey == "Day 1" && presentation.groups[1].sources.size() == 2,
        "RAW Workspace gallery presentation should group sibling folder sources");
    Require(presentation.hasSelection && presentation.selectedSourceKey == "Day 1/image_0001.DNG",
        "RAW Workspace gallery presentation should preserve preview-only selection");
    Require(presentation.groups[1].sources[0].selected,
        "RAW Workspace gallery source view should mark selected source");
    Require(presentation.groups[1].sources[0].projectStatus == RawWorkspace::ProjectStatus::Unknown,
        "Phase 3 project status should remain an unknown placeholder");
    Require(std::string(RawWorkspace::ProjectStatusLabel(presentation.groups[1].sources[0].projectStatus)) == "Unknown",
        "RAW Workspace project status label should expose the Phase 3 placeholder");
    Require(presentation.readyThumbnailCount == 1 &&
            presentation.queuedThumbnailCount == 1 &&
            presentation.failedThumbnailCount == 1,
        "RAW Workspace gallery presentation should summarize thumbnail states");
    Require(RawWorkspace::ResolveExclusiveGalleryPlacement(RawWorkspace::GalleryPlacementMode::RightGallery) ==
            RawWorkspace::GalleryPlacementMode::RightGallery,
        "RAW Workspace right gallery placement should be valid");
    Require(RawWorkspace::ResolveExclusiveGalleryPlacement(RawWorkspace::GalleryPlacementMode::BottomFilmstrip) ==
            RawWorkspace::GalleryPlacementMode::BottomFilmstrip,
        "RAW Workspace bottom filmstrip placement should be valid");
}

void TestRawWorkspacePanelStateModel() {
    namespace RawWorkspace = Stack::RawWorkspace;
    namespace RawRecipe = Stack::RawRecipe;

    Require(!RawWorkspace::BuildRawPanelState(nullptr).recipeControlsEditable,
        "RAW panel without a selection should not expose recipe controls");

    RawWorkspace::SourceRecord source;
    source.relativePathKey = "Day 1/image_0001.DNG";
    source.fileName = "image_0001.DNG";
    source.project.status = RawWorkspace::ProjectStatus::NoProject;
    source.project.mode = RawWorkspace::RawProjectMode::RecipeBacked;

    RawWorkspace::RawPanelState previewState = RawWorkspace::BuildRawPanelState(&source);
    Require(previewState.recipeControlsEditable,
        "Preview-only RAW selections should allow a first recipe edit");
    Require(previewState.editCreatesProject,
        "Preview-only RAW controls should report that the edit creates a project");
    Require(!previewState.openGraphEnabled,
        "Preview-only RAW selections should not open a graph project");
    Require(previewState.graphTooltip == "Make an edit to create this RAW project first.",
        "Preview-only Open In Graph tooltip should explain the first-edit requirement");

    source.project.status = RawWorkspace::ProjectStatus::Existing;
    source.project.mode = RawWorkspace::RawProjectMode::RecipeBacked;
    RawWorkspace::RawPanelState editedState = RawWorkspace::BuildRawPanelState(&source);
    Require(editedState.recipeControlsEditable && editedState.openGraphEnabled,
        "Recipe-backed RAW projects should be editable and openable in the graph");

    source.project.mode = RawWorkspace::RawProjectMode::CustomGraph;
    source.project.readOnlyReason.clear();
    RawWorkspace::RawPanelState customState = RawWorkspace::BuildRawPanelState(&source);
    Require(!customState.recipeControlsEditable && customState.openGraphEnabled,
        "Custom Graph Mode should keep graph access but block RAW recipe controls");
    Require(customState.readOnlyMessage.find("read-only") != std::string::npos,
        "Custom Graph Mode should expose a RAW tab read-only message");

    source.project.mode = RawWorkspace::RawProjectMode::Unknown;
    source.project.errorMessage = "Unsupported RAW Workspace mode";
    RawWorkspace::RawPanelState unknownModeState = RawWorkspace::BuildRawPanelState(&source);
    Require(!unknownModeState.recipeControlsEditable && unknownModeState.openGraphEnabled,
        "Unsupported RAW project modes should keep graph access but block RAW recipe controls");
    Require(unknownModeState.readOnlyMessage.find("Unsupported") != std::string::npos,
        "Unsupported RAW project modes should expose a read-only explanation");

    source.project.status = RawWorkspace::ProjectStatus::Invalid;
    RawWorkspace::RawPanelState invalidState = RawWorkspace::BuildRawPanelState(&source);
    Require(!invalidState.recipeControlsEditable && !invalidState.openGraphEnabled,
        "Invalid RAW projects should block recipe controls and graph opening");

    RawRecipe::RawDevelopmentRecipe recipe = RawRecipe::MakeDefaultRecipe("image_0001.DNG", "image_0001.DNG");
    const RawRecipe::WhiteBalanceMode modes[] = {
        RawRecipe::WhiteBalanceMode::AsShot,
        RawRecipe::WhiteBalanceMode::Auto,
        RawRecipe::WhiteBalanceMode::CustomMultipliers,
        RawRecipe::WhiteBalanceMode::SampledGrayPoint
    };
    for (RawRecipe::WhiteBalanceMode mode : modes) {
        recipe.whiteBalance.mode = mode;
        recipe.whiteBalance.hasTemperatureKelvin = true;
        recipe.whiteBalance.temperatureKelvin = 5500.0f;
        recipe.whiteBalance.hasTint = true;
        recipe.whiteBalance.tint = 0.0f;
        recipe.whiteBalance.hasSamplePoint = true;
        recipe.whiteBalance.sampleX = 0.42f;
        recipe.whiteBalance.sampleY = 0.58f;
        const RawRecipe::RawDevelopmentRecipe roundTrip =
            RawRecipe::DeserializeRecipe(RawRecipe::SerializeRecipe(recipe));
        Require(roundTrip.whiteBalance.mode == mode,
            "RAW recipe should preserve every Phase 6 white-balance panel mode");
    }
}

void TestRawWorkspaceProjectLifecycleModel() {
    namespace RawWorkspace = Stack::RawWorkspace;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_project_lifecycle_test");
    WriteRawWorkspaceTestFile(root / "Day 1" / "image_0001.DNG");
    WriteRawWorkspaceTestFile(root / "Day 2" / "image_0002.ARW");

    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(root, RawWorkspace::DefaultRawPathPredicate);
    Require(scan.success, "RAW Workspace project lifecycle scan should succeed");
    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);
    Require(scan.sources.size() == 2, "RAW Workspace project lifecycle test should find two RAW files");

    auto sourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& source) {
        return source.relativePathKey == "Day 1/image_0001.DNG";
    });
    Require(sourceIt != scan.sources.end(), "RAW project lifecycle source should exist");
    Require(sourceIt->project.status == RawWorkspace::ProjectStatus::NoProject,
        "RAW source should start preview-only with no project");

    RawWorkspace::WorkspaceState state;
    state.workspaceRoot = scan.layout.workspaceRoot;
    state.sources = scan.sources;
    Require(RawWorkspace::SelectSourceByKey(state, "Day 1/image_0001.DNG"),
        "RAW project lifecycle selection should succeed");
    Require(!std::filesystem::exists(scan.layout.projectsDirectory / "Day 1" / "image_0001.stack"),
        "Preview selection should not create a RAW project");

    RawWorkspace::SourceRecord source = *sourceIt;
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(source.absolutePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fileSizeBytes = static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    recipe.preToneExposureEv = 1.0f;

    EditorNodeGraph::Graph graph;
    EditorNodeGraph::RawDevelopmentPayload rawPayload;
    rawPayload.recipe = recipe;
    rawPayload.projectStatus = "Edited";
    rawPayload.edited = true;
    const int rawDevelopmentId = NodeId(graph.AddRawDevelopmentNode(rawPayload, { 0.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 260.0f, 0.0f }, true));
    Require(graph.TryConnectSockets(rawDevelopmentId, EditorNodeGraph::kImageOutputSocketId, outputId, EditorNodeGraph::kImageInputSocketId),
        "RAW project lifecycle graph should connect compact RAW Development to output");
    const nlohmann::json downstreamGraph = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), graph);

    StackBinaryFormat::ProjectDocument document;
    document.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
    document.metadata.projectName = "image_0001";
    document.metadata.sourceWidth = 1;
    document.metadata.sourceHeight = 1;
    document.thumbnailBytes = { 1, 2, 3 };
    document.sourceImageBytes = { 4, 5, 6 };
    document.pipelineData = downstreamGraph;
    Require(RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(source, recipe, downstreamGraph, document),
        "RAW project lifecycle should apply RAW metadata to a project document");
    Require(document.rawWorkspaceData.value("rawWorkspaceMode", std::string()) == "recipe-backed",
        "New RAW edit projects should be recipe-backed");
    Require(document.rawWorkspaceData["rawSourceRef"].value("linked", false),
        "New RAW edit projects should link RAW files by default");
    Require(document.rawWorkspaceData.contains("managedRawSection") &&
            document.rawWorkspaceData.contains("customRawSection"),
        "RAW project data should reserve managed/custom mode fields");
    document.rawWorkspaceData["managedRawSection"] = { { "sentinel", "managed" } };
    document.rawWorkspaceData["customRawSection"] = { { "sentinel", "custom" } };
    document.rawWorkspaceData["readOnlyReason"] = "preserve future read-only metadata";
    document.rawWorkspaceData["futureRawWorkspaceKey"] = { { "sentinel", true } };
    Stack::RawRecipe::RawDevelopmentRecipe editedRecipe = recipe;
    editedRecipe.preToneExposureEv = 1.5f;
    Require(RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(source, editedRecipe, downstreamGraph, document),
        "RAW project lifecycle should update owned recipe metadata in place");
    Require(document.rawWorkspaceData["managedRawSection"].value("sentinel", std::string()) == "managed",
        "RAW project lifecycle saves should preserve managed section metadata");
    Require(document.rawWorkspaceData["customRawSection"].value("sentinel", std::string()) == "custom",
        "RAW project lifecycle saves should preserve custom section metadata");
    Require(document.rawWorkspaceData.value("readOnlyReason", std::string()) == "preserve future read-only metadata",
        "RAW project lifecycle saves should preserve read-only metadata");
    Require(document.rawWorkspaceData["futureRawWorkspaceKey"].value("sentinel", false),
        "RAW project lifecycle saves should preserve future RAW workspace metadata");
    recipe = editedRecipe;

    const std::filesystem::path projectRelative = RawWorkspace::BuildProjectRelativePathForSource(source);
    Require(projectRelative.generic_string() == "Day 1/image_0001.stack",
        "RAW project path should mirror the source subfolder and filename stem");
    const std::filesystem::path projectPath = scan.layout.projectsDirectory / projectRelative;
    std::filesystem::create_directories(projectPath.parent_path());
    Require(StackBinaryFormat::WriteProjectFile(projectPath, document),
        "RAW project lifecycle should write a .stack project");

    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);
    sourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& candidate) {
        return candidate.relativePathKey == "Day 1/image_0001.DNG";
    });
    Require(sourceIt != scan.sources.end(), "RAW project lifecycle source should still exist after discovery");
    Require(sourceIt->project.status == RawWorkspace::ProjectStatus::Existing,
        "RAW project discovery should attach existing projects");
    Require(sourceIt->project.relativePath.generic_string() == "Day 1/image_0001.stack",
        "RAW project discovery should report the relative project path");

    StackBinaryFormat::ProjectDocument loadedDocument;
    Require(StackBinaryFormat::ReadProjectFile(projectPath, loadedDocument),
        "RAW project lifecycle should reload the project file");
    RawWorkspace::ProjectInfo loadedInfo;
    Stack::RawRecipe::RawDevelopmentRecipe loadedRecipe;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedDocument, loadedInfo, &loadedRecipe),
        "RAW project lifecycle should parse project RAW metadata");
    Require(loadedInfo.linkedRaw && !loadedInfo.embeddedRaw,
        "Reloaded RAW project should remain linked by default");
    Require(loadedInfo.mode == RawWorkspace::RawProjectMode::RecipeBacked,
        "Reloaded RAW project should preserve recipe-backed mode");
    Require(std::abs(loadedRecipe.preToneExposureEv - 1.5f) < 0.001f,
        "Reloaded RAW project should preserve recipe edits");
    Require(loadedDocument.rawWorkspaceData.contains("downstreamGraph"),
        "RAW project lifecycle should store downstream graph payload");

    StackBinaryFormat::ProjectDocument missingModeDocument = loadedDocument;
    missingModeDocument.rawWorkspaceData.erase("rawWorkspaceMode");
    RawWorkspace::ProjectInfo missingModeInfo;
    Require(RawWorkspace::ReadProjectInfoFromDocument(missingModeDocument, missingModeInfo, nullptr),
        "RAW project lifecycle should parse malformed metadata enough to report invalid status");
    Require(missingModeInfo.status == RawWorkspace::ProjectStatus::Invalid &&
            missingModeInfo.mode == RawWorkspace::RawProjectMode::Unknown,
        "Missing RAW project mode should be invalid instead of recipe-backed");

    StackBinaryFormat::ProjectDocument futureModeDocument = loadedDocument;
    futureModeDocument.rawWorkspaceData["rawWorkspaceMode"] = "future-managed-mode";
    RawWorkspace::ProjectInfo futureModeInfo;
    Require(RawWorkspace::ReadProjectInfoFromDocument(futureModeDocument, futureModeInfo, nullptr),
        "RAW project lifecycle should parse future-mode metadata enough to report invalid status");
    Require(futureModeInfo.status == RawWorkspace::ProjectStatus::Invalid &&
            futureModeInfo.mode == RawWorkspace::RawProjectMode::Unknown,
        "Unsupported RAW project mode should be invalid instead of recipe-backed");

    auto secondSourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& candidate) {
        return candidate.relativePathKey == "Day 2/image_0002.ARW";
    });
    Require(secondSourceIt != scan.sources.end(), "RAW project lifecycle second source should exist");
    Stack::RawRecipe::RawDevelopmentRecipe futureModeRecipe =
        Stack::RawRecipe::MakeDefaultRecipe(secondSourceIt->absolutePath.string(), secondSourceIt->fileName);
    futureModeRecipe.source.relativePathKey = secondSourceIt->relativePathKey;
    futureModeRecipe.source.fileSizeBytes = static_cast<std::uint64_t>(secondSourceIt->fileSizeBytes);
    futureModeRecipe.source.modifiedTimeTicks = secondSourceIt->modifiedTimeTicks;
    StackBinaryFormat::ProjectDocument futureModeProject;
    futureModeProject.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
    futureModeProject.metadata.projectName = "image_0002";
    futureModeProject.metadata.sourceWidth = 1;
    futureModeProject.metadata.sourceHeight = 1;
    futureModeProject.thumbnailBytes = { 1, 2, 3 };
    futureModeProject.sourceImageBytes = { 4, 5, 6 };
    futureModeProject.pipelineData = downstreamGraph;
    Require(RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
            *secondSourceIt,
            futureModeRecipe,
            downstreamGraph,
            futureModeProject),
        "RAW project lifecycle should create a second project document");
    futureModeProject.rawWorkspaceData["rawWorkspaceMode"] = "future-managed-mode";
    const std::filesystem::path futureModeProjectPath =
        scan.layout.projectsDirectory / RawWorkspace::BuildProjectRelativePathForSource(*secondSourceIt);
    std::filesystem::create_directories(futureModeProjectPath.parent_path());
    Require(StackBinaryFormat::WriteProjectFile(futureModeProjectPath, futureModeProject),
        "RAW project lifecycle should write a future-mode test project");
    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);
    secondSourceIt = std::find_if(scan.sources.begin(), scan.sources.end(), [](const RawWorkspace::SourceRecord& candidate) {
        return candidate.relativePathKey == "Day 2/image_0002.ARW";
    });
    Require(secondSourceIt != scan.sources.end(), "RAW project lifecycle second source should remain discoverable");
    Require(secondSourceIt->project.status == RawWorkspace::ProjectStatus::Invalid &&
            secondSourceIt->project.mode == RawWorkspace::RawProjectMode::Unknown,
        "Discovery should keep unsupported project modes invalid and read-only");

    RawWorkspace::SourceRecord relinkedSource = source;
    relinkedSource.absolutePath = root / "Day 2" / "renamed_image_0001.DNG";
    relinkedSource.relativePath = "Day 2/renamed_image_0001.DNG";
    relinkedSource.relativePathKey = "Day 2/renamed_image_0001.DNG";
    relinkedSource.parentFolderKey = "Day 2";
    relinkedSource.fileName = "renamed_image_0001.DNG";
    relinkedSource.stem = "renamed_image_0001";
    WriteRawWorkspaceTestFile(relinkedSource.absolutePath);
    std::string relinkError;
    Require(RawWorkspace::RelinkProjectDocumentToSource(relinkedSource, loadedDocument, &relinkError),
        "RAW project lifecycle should relink project metadata to a selected RAW");
    Stack::RawRecipe::RawDevelopmentRecipe relinkedRecipe =
        Stack::RawRecipe::DeserializeRecipe(loadedDocument.rawWorkspaceData["rawRecipe"]);
    Require(relinkedRecipe.source.relativePathKey == "Day 2/renamed_image_0001.DNG",
        "RAW project relink should update recipe source reference");
    Require(loadedDocument.rawWorkspaceData["rawSourceRef"].value("relativePathKey", std::string()) ==
            "Day 2/renamed_image_0001.DNG",
        "RAW project relink should update rawSourceRef");

    std::string embedError;
    Require(RawWorkspace::EmbedRawSourceInProjectDocument(relinkedSource, loadedDocument, &embedError),
        "RAW project lifecycle should embed a selected RAW source");
    Require(loadedDocument.rawWorkspaceData["embeddedRaw"].value("present", false),
        "RAW project embed should mark embedded raw data present");
    Require(loadedDocument.rawWorkspaceData["embeddedRaw"].contains("bytes") &&
            loadedDocument.rawWorkspaceData["embeddedRaw"]["bytes"].is_binary(),
        "RAW project embed should store RAW bytes in the project metadata");
    RawWorkspace::ProjectInfo embeddedInfo;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedDocument, embeddedInfo, nullptr),
        "RAW project lifecycle should parse embedded project metadata");
    Require(embeddedInfo.status == RawWorkspace::ProjectStatus::Embedded &&
            embeddedInfo.embeddedRaw &&
            !embeddedInfo.linkedRaw,
        "Embedded RAW project should report embedded status");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

Stack::RawRecipe::RawDevelopmentRecipe BuildRawWorkspaceReloadTestRecipe(
    const Stack::RawWorkspace::SourceRecord& source) {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(source.absolutePath.string(), source.fileName);
    recipe.source.relativePathKey = source.relativePathKey;
    recipe.source.fingerprint = source.fingerprint;
    recipe.source.fileSizeBytes = static_cast<std::uint64_t>(source.fileSizeBytes);
    recipe.source.modifiedTimeTicks = source.modifiedTimeTicks;
    return recipe;
}

const Stack::RawWorkspace::SourceRecord& FindReloadTestSource(
    const std::vector<Stack::RawWorkspace::SourceRecord>& sources,
    const std::string& relativePathKey) {
    const auto it = std::find_if(
        sources.begin(),
        sources.end(),
        [&](const Stack::RawWorkspace::SourceRecord& source) {
            return source.relativePathKey == relativePathKey;
        });
    Require(it != sources.end(), "RAW reload ownership test source should exist");
    return *it;
}

struct ManagedRawReloadTestGraph {
    EditorNodeGraph::Graph graph;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    Stack::RawWorkspace::ManagedRawSection section;
    int rawDecodeNodeId = 0;
    int toneCurveNodeId = 0;
};

ManagedRawReloadTestGraph BuildManagedRawReloadTestGraph(
    const Stack::RawWorkspace::SourceRecord& source,
    const char* sectionSuffix) {
    using namespace EditorNodeGraph;

    ManagedRawReloadTestGraph result;
    result.recipe = BuildRawWorkspaceReloadTestRecipe(source);

    RawSourcePayload sourcePayload;
    sourcePayload.label = source.fileName;
    sourcePayload.sourcePath = source.absolutePath.string();
    sourcePayload.metadata.sourcePath = sourcePayload.sourcePath;
    const int rawSourceId = NodeId(result.graph.AddRawSourceNode(sourcePayload, { 0.0f, 0.0f }));

    RawDecodePayload decodePayload;
    decodePayload.settings.exposureStops = 0.75f;
    decodePayload.settings.whiteBalanceMode = Raw::WhiteBalanceMode::Manual;
    decodePayload.settings.manualWhiteBalance = { 1.75f, 1.0f, 1.25f };
    decodePayload.settings.rotationDegrees = 180;
    result.rawDecodeNodeId = NodeId(result.graph.AddRawDecodeNode(decodePayload, { 260.0f, 0.0f }));
    result.toneCurveNodeId = NodeId(result.graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(result.graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(result.graph.AddOutputNode({ 1040.0f, 0.0f }, true));

    Require(result.graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, result.rawDecodeNodeId, kRawInputSocketId),
        "RAW reload ownership managed source should connect");
    Require(result.graph.TryConnectSockets(result.rawDecodeNodeId, kImageOutputSocketId, result.toneCurveNodeId, kImageInputSocketId),
        "RAW reload ownership managed decode should connect");
    Require(result.graph.TryConnectSockets(result.toneCurveNodeId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "RAW reload ownership managed tone should connect");
    Require(result.graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "RAW reload ownership managed view transform should connect");

    result.section = Stack::RawWorkspace::BuildManagedRawSection(
        std::string("managed-raw:reload-") + sectionSuffix,
        source.relativePathKey,
        source.relativePathKey,
        source.fingerprint,
        -1,
        rawSourceId,
        result.rawDecodeNodeId,
        result.toneCurveNodeId,
        viewTransformId);
    return result;
}

void WriteRawWorkspaceReloadTestProject(
    const Stack::RawWorkspace::ManagedLayout& layout,
    const Stack::RawWorkspace::SourceRecord& source,
    const Stack::RawRecipe::RawDevelopmentRecipe& recipe,
    const nlohmann::json& graphPayload,
    Stack::RawWorkspace::RawProjectMode mode,
    const nlohmann::json& managedRawSection,
    const nlohmann::json& customRawSection,
    const std::string& readOnlyReason = {}) {
    StackBinaryFormat::ProjectDocument document;
    document.metadata.projectKind = StackBinaryFormat::kEditorProjectKind;
    document.metadata.projectName = source.stem.empty() ? source.fileName : source.stem;
    document.metadata.sourceWidth = 1;
    document.metadata.sourceHeight = 1;
    document.thumbnailBytes = { 1, 2, 3 };
    document.sourceImageBytes = { 4, 5, 6, 7 };
    document.pipelineData = graphPayload;
    Require(Stack::RawWorkspace::ApplyRawWorkspaceDataToProjectDocument(
            source,
            recipe,
            graphPayload,
            document,
            mode),
        "RAW reload ownership test should apply RAW Workspace metadata");
    document.rawWorkspaceData["managedRawSection"] = managedRawSection;
    document.rawWorkspaceData["customRawSection"] = customRawSection;
    document.rawWorkspaceData["readOnlyReason"] =
        readOnlyReason.empty() ? nlohmann::json() : nlohmann::json(readOnlyReason);

    const std::filesystem::path projectPath =
        layout.projectsDirectory / Stack::RawWorkspace::BuildProjectRelativePathForSource(source);
    std::error_code ec;
    std::filesystem::create_directories(projectPath.parent_path(), ec);
    Require(!ec, "RAW reload ownership test should create project parent directory");
    Require(StackBinaryFormat::WriteProjectFile(projectPath, document),
        "RAW reload ownership test should write project file");
}

void TestRawWorkspaceProjectReloadPreservesOwnershipModes() {
    namespace RawWorkspace = Stack::RawWorkspace;
    namespace RawRecipe = Stack::RawRecipe;
    using namespace EditorNodeGraph;

    const std::filesystem::path root = MakeTempDirectory("stack_raw_project_reload_modes_test");
    WriteRawWorkspaceTestFile(root / "recipe_image.DNG");
    WriteRawWorkspaceTestFile(root / "managed_image.DNG");
    WriteRawWorkspaceTestFile(root / "custom_image.DNG");

    RawWorkspace::ScanResult scan = RawWorkspace::ScanWorkspace(root, RawWorkspace::DefaultRawPathPredicate);
    Require(scan.success, "RAW reload ownership scan should succeed");
    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);

    const RawWorkspace::SourceRecord& recipeSource =
        FindReloadTestSource(scan.sources, "recipe_image.DNG");
    const RawWorkspace::SourceRecord& managedSource =
        FindReloadTestSource(scan.sources, "managed_image.DNG");
    const RawWorkspace::SourceRecord& customSource =
        FindReloadTestSource(scan.sources, "custom_image.DNG");

    RawRecipe::RawDevelopmentRecipe recipeBackedRecipe =
        BuildRawWorkspaceReloadTestRecipe(recipeSource);
    recipeBackedRecipe.preToneExposureEv = 0.35f;
    Graph recipeGraph;
    RawDevelopmentPayload recipePayload;
    recipePayload.recipe = recipeBackedRecipe;
    recipePayload.projectStatus = "Edited";
    recipePayload.edited = true;
    const int rawDevelopmentId = NodeId(recipeGraph.AddRawDevelopmentNode(recipePayload, { 0.0f, 0.0f }));
    const int recipeOutputId = NodeId(recipeGraph.AddOutputNode({ 260.0f, 0.0f }, true));
    Require(recipeGraph.TryConnectSockets(rawDevelopmentId, kImageOutputSocketId, recipeOutputId, kImageInputSocketId),
        "RAW reload ownership recipe-backed graph should connect");
    const nlohmann::json recipeGraphPayload =
        SerializeGraphPayload(nlohmann::json::array(), recipeGraph);
    WriteRawWorkspaceReloadTestProject(
        scan.layout,
        recipeSource,
        recipeBackedRecipe,
        recipeGraphPayload,
        RawWorkspace::RawProjectMode::RecipeBacked,
        nullptr,
        nullptr);

    ManagedRawReloadTestGraph managed = BuildManagedRawReloadTestGraph(managedSource, "managed");
    const nlohmann::json managedGraphPayload =
        SerializeGraphPayload(nlohmann::json::array(), managed.graph);
    WriteRawWorkspaceReloadTestProject(
        scan.layout,
        managedSource,
        managed.recipe,
        managedGraphPayload,
        RawWorkspace::RawProjectMode::ManagedDecomposed,
        RawWorkspace::SerializeManagedRawSection(managed.section),
        nullptr);

    ManagedRawReloadTestGraph custom = BuildManagedRawReloadTestGraph(customSource, "custom");
    custom.graph.RemoveLink(custom.rawDecodeNodeId, kImageOutputSocketId, custom.toneCurveNodeId, kImageInputSocketId);
    const int mixId = NodeId(custom.graph.AddMixNode({ 390.0f, 90.0f }));
    Require(custom.graph.TryConnectSockets(custom.rawDecodeNodeId, kImageOutputSocketId, mixId, kMixInputASocketId),
        "RAW reload ownership custom graph should connect custom node after decode");
    Require(custom.graph.TryConnectSockets(mixId, kImageOutputSocketId, custom.toneCurveNodeId, kImageInputSocketId),
        "RAW reload ownership custom graph should reconnect to managed tone node");
    const nlohmann::json customRawSection = {
        { "schema", "stack.rawWorkspace.customRawSection" },
        { "schemaVersion", 1 },
        { "modeState", "custom-graph" },
        { "previousManagedSectionId", custom.section.sectionId },
        { "reason", RawWorkspace::kCustomGraphReadOnlyReason }
    };
    const nlohmann::json customGraphPayload =
        SerializeGraphPayload(nlohmann::json::array(), custom.graph);
    WriteRawWorkspaceReloadTestProject(
        scan.layout,
        customSource,
        custom.recipe,
        customGraphPayload,
        RawWorkspace::RawProjectMode::CustomGraph,
        RawWorkspace::SerializeManagedRawSection(custom.section),
        customRawSection,
        RawWorkspace::kCustomGraphReadOnlyReason);

    RawWorkspace::DiscoverProjects(scan.layout, scan.sources);
    Require(FindReloadTestSource(scan.sources, "recipe_image.DNG").project.mode ==
            RawWorkspace::RawProjectMode::RecipeBacked,
        "RAW project discovery should preserve recipe-backed ownership mode");
    Require(FindReloadTestSource(scan.sources, "managed_image.DNG").project.mode ==
            RawWorkspace::RawProjectMode::ManagedDecomposed,
        "RAW project discovery should preserve managed-decomposed ownership mode");
    Require(FindReloadTestSource(scan.sources, "custom_image.DNG").project.mode ==
            RawWorkspace::RawProjectMode::CustomGraph,
        "RAW project discovery should preserve custom graph ownership mode");

    const std::filesystem::path recipeProjectPath =
        scan.layout.projectsDirectory / RawWorkspace::BuildProjectRelativePathForSource(recipeSource);
    StackBinaryFormat::ProjectDocument loadedRecipeDocument;
    Require(StackBinaryFormat::ReadProjectFile(recipeProjectPath, loadedRecipeDocument),
        "RAW reload ownership recipe-backed project should reload from disk");
    RawWorkspace::ProjectInfo loadedRecipeInfo;
    RawRecipe::RawDevelopmentRecipe loadedRecipe;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedRecipeDocument, loadedRecipeInfo, &loadedRecipe),
        "RAW reload ownership recipe-backed metadata should parse");
    Require(loadedRecipeInfo.mode == RawWorkspace::RawProjectMode::RecipeBacked,
        "RAW reload ownership recipe-backed mode should survive file round-trip");
    Require(loadedRecipeDocument.rawWorkspaceData["managedRawSection"].is_null() &&
            loadedRecipeDocument.rawWorkspaceData["customRawSection"].is_null(),
        "RAW reload ownership recipe-backed project should not grow managed/custom payloads");
    Require(std::abs(loadedRecipe.preToneExposureEv - 0.35f) < 0.001f,
        "RAW reload ownership recipe-backed edits should survive file round-trip");

    const std::filesystem::path managedProjectPath =
        scan.layout.projectsDirectory / RawWorkspace::BuildProjectRelativePathForSource(managedSource);
    StackBinaryFormat::ProjectDocument loadedManagedDocument;
    Require(StackBinaryFormat::ReadProjectFile(managedProjectPath, loadedManagedDocument),
        "RAW reload ownership managed project should reload from disk");
    RawWorkspace::ProjectInfo loadedManagedInfo;
    RawRecipe::RawDevelopmentRecipe loadedManagedRecipe;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedManagedDocument, loadedManagedInfo, &loadedManagedRecipe),
        "RAW reload ownership managed metadata should parse");
    Require(loadedManagedInfo.mode == RawWorkspace::RawProjectMode::ManagedDecomposed,
        "RAW reload ownership managed mode should survive file round-trip");
    const RawWorkspace::ManagedRawSection loadedManagedSection =
        RawWorkspace::DeserializeManagedRawSection(
            loadedManagedDocument.rawWorkspaceData.value("managedRawSection", nlohmann::json::object()));
    Graph loadedManagedGraph;
    DeserializeGraphPayload(loadedManagedDocument.pipelineData, loadedManagedGraph, 2, {}, 0, 0, 0);
    const RawWorkspace::ManagedRawValidationResult managedValidation =
        RawWorkspace::ValidateManagedRawSection(loadedManagedGraph, loadedManagedSection, loadedManagedRecipe);
    if (!managedValidation.valid) {
        std::cerr << "Managed reload validation failed: " << managedValidation.message << "\n";
        std::cerr << "Managed section ids: source=" << loadedManagedSection.rawSourceNodeId
                  << " decode=" << loadedManagedSection.rawDecodeNodeId
                  << " tone=" << loadedManagedSection.toneCurveNodeId
                  << " view=" << loadedManagedSection.viewTransformNodeId << "\n";
        std::cerr << "Reloaded graph node ids:";
        for (const EditorNodeGraph::Node& node : loadedManagedGraph.GetNodes()) {
            std::cerr << " " << node.id;
        }
        std::cerr << "\n";
    }
    Require(managedValidation.valid,
        "RAW reload ownership managed graph should still validate after file round-trip");
    Require(std::abs(managedValidation.recipe.preToneExposureEv - 0.75f) < 0.001f,
        "RAW reload ownership managed decode exposure should still sync after reload");
    Require(managedValidation.recipe.whiteBalance.mode == RawRecipe::WhiteBalanceMode::CustomMultipliers &&
            managedValidation.recipe.whiteBalance.hasMultipliers,
        "RAW reload ownership managed white balance should still sync after reload");
    Require(managedValidation.recipe.cropRotation.rotationDegrees == 180,
        "RAW reload ownership managed rotation should still sync after reload");

    const std::filesystem::path customProjectPath =
        scan.layout.projectsDirectory / RawWorkspace::BuildProjectRelativePathForSource(customSource);
    StackBinaryFormat::ProjectDocument loadedCustomDocument;
    Require(StackBinaryFormat::ReadProjectFile(customProjectPath, loadedCustomDocument),
        "RAW reload ownership custom project should reload from disk");
    RawWorkspace::ProjectInfo loadedCustomInfo;
    RawRecipe::RawDevelopmentRecipe loadedCustomRecipe;
    Require(RawWorkspace::ReadProjectInfoFromDocument(loadedCustomDocument, loadedCustomInfo, &loadedCustomRecipe),
        "RAW reload ownership custom metadata should parse");
    Require(loadedCustomInfo.mode == RawWorkspace::RawProjectMode::CustomGraph,
        "RAW reload ownership custom graph mode should survive file round-trip");
    Require(loadedCustomInfo.readOnlyReason.find("read-only") != std::string::npos,
        "RAW reload ownership custom graph read-only reason should survive file round-trip");
    Require(loadedCustomDocument.rawWorkspaceData["customRawSection"].value("modeState", std::string()) == "custom-graph",
        "RAW reload ownership custom section payload should survive file round-trip");
    const RawWorkspace::ManagedRawSection loadedPreviousManagedSection =
        RawWorkspace::DeserializeManagedRawSection(
            loadedCustomDocument.rawWorkspaceData.value("managedRawSection", nlohmann::json::object()));
    Graph loadedCustomGraph;
    DeserializeGraphPayload(loadedCustomDocument.pipelineData, loadedCustomGraph, 2, {}, 0, 0, 0);
    Require(!RawWorkspace::ValidateManagedRawSection(
                loadedCustomGraph,
                loadedPreviousManagedSection,
                loadedCustomRecipe).valid,
        "RAW reload ownership custom graph should remain outside managed editability after reload");

    std::error_code ec;
    std::filesystem::remove_all(root, ec);
}

void TestScalarThroughDataMathToPreviewAndScalarTargets() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int maskId = NodeId(graph.AddCustomMaskNode({}, { 0.0f, 0.0f }));
    const int clampId = NodeId(graph.AddDataMathNode(DataMathMode::Clamp, { 220.0f, 0.0f }));
    const int addId = NodeId(graph.AddDataMathNode(DataMathMode::Add, { 440.0f, 0.0f }));
    const int averageId = NodeId(graph.AddDataMathNode(DataMathMode::Average, { 660.0f, 0.0f }));
    const int previewId = NodeId(graph.AddPreviewNode({ 880.0f, 0.0f }));
    const int layerId = NodeId(graph.AddLayerNode(LayerType::Brightness, 0, { 880.0f, 140.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 880.0f, 280.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 880.0f, 420.0f }, true));

    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, clampId, kMixInputASocketId),
        "custom mask should feed Data Math clamp input A");
    Require(graph.IsScalarSocketStream(clampId, kImageOutputSocketId),
        "Data Math with only scalar inputs should output a scalar stream");
    Require(graph.TryConnectSockets(clampId, kImageOutputSocketId, previewId, kPreviewInputSocketId),
        "scalar Data Math output should connect to preview");

    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, addId, kMixInputASocketId),
        "custom mask should feed Data Math add input A");
    Require(graph.TryConnectSockets(clampId, kImageOutputSocketId, addId, kMixInputBSocketId),
        "scalar Data Math output should feed Data Math input B");
    Require(graph.IsScalarSocketStream(addId, kImageOutputSocketId),
        "Data Math with two scalar inputs should preserve scalar output classification");
    Require(graph.TryConnectSockets(addId, kImageOutputSocketId, layerId, kMaskInputSocketId),
        "scalar Data Math output should connect to a layer mask");

    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, averageId, kMixInputASocketId),
        "custom mask should feed Data Math average input A");
    Require(graph.TryConnectSockets(addId, kImageOutputSocketId, averageId, kMixInputBSocketId),
        "scalar Data Math add output should feed Data Math average input B");
    Require(graph.TryConnectSockets(averageId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "scalar Data Math output should connect to mix factor");
    Require(graph.TryConnectSockets(averageId, kImageOutputSocketId, outputId, "a"),
        "scalar Data Math output should connect to output RGBA channel pins");
}

void TestImageThroughDataMathToOutput() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int clampId = NodeId(graph.AddDataMathNode(DataMathMode::Clamp, { 220.0f, 0.0f }));
    const int averageId = NodeId(graph.AddDataMathNode(DataMathMode::ImageAverage, { 440.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 660.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, clampId, kMixInputASocketId),
        "full image should feed Data Math clamp input A");
    Require(!graph.IsScalarSocketStream(clampId, kImageOutputSocketId),
        "Data Math with a full image input should output an image stream");
    Require(graph.TryConnectSockets(clampId, kImageOutputSocketId, averageId, kMixInputASocketId),
        "full image Data Math output should feed another Data Math image input");
    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, averageId, kMixInputBSocketId),
        "full image should feed Data Math input B");
    Require(graph.TryConnectSockets(averageId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "image Data Math output should connect to output image input");
}

void TestAverageNodeInputRules() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageAId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int imageBId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 160.0f }));
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 320.0f }));
    const int splitId = NodeId(graph.AddChannelSplitNode({ 220.0f, 0.0f }));
    const int scalarAverageId = NodeId(graph.AddDataMathNode(DataMathMode::Average, { 440.0f, 0.0f }));
    const int imageAverageId = NodeId(graph.AddDataMathNode(DataMathMode::ImageAverage, { 440.0f, 180.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 660.0f, 180.0f }, true));

    Require(!graph.FindSocket(scalarAverageId, kDataMathBaseInputSocketId),
        "scalar Average should not expose a masked Base input");
    Require(!graph.FindSocket(scalarAverageId, kMaskInputSocketId),
        "scalar Average should not expose a blend Mask input");
    Require(!graph.CanConnectSockets(imageAId, kImageOutputSocketId, scalarAverageId, kMixInputASocketId),
        "scalar Average should reject full image inputs");
    Require(graph.CanInsertImageToScalarExtractor(imageAId, kImageOutputSocketId, scalarAverageId, kMixInputASocketId),
        "full images should be convertible into scalar Average inputs through an extractor");
    Require(graph.TryConnectSockets(imageAId, kImageOutputSocketId, splitId, kImageInputSocketId),
        "full image should feed channel split");
    Require(graph.TryConnectSockets(splitId, "r", scalarAverageId, kMixInputASocketId),
        "scalar Average should accept split channel inputs");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, scalarAverageId, kMixInputBSocketId),
        "scalar Average should accept mask inputs");
    Require(graph.IsScalarSocketStream(scalarAverageId, kImageOutputSocketId),
        "scalar Average should always output a scalar stream");

    Require(!graph.CanConnectSockets(maskId, kMaskOutputSocketId, imageAverageId, kMixInputASocketId),
        "Average Images should reject mask inputs");
    Require(!graph.CanConnectSockets(scalarAverageId, kImageOutputSocketId, imageAverageId, kMixInputASocketId),
        "Average Images should reject scalar math outputs");
    Require(graph.TryConnectSockets(imageAId, kImageOutputSocketId, imageAverageId, kMixInputASocketId),
        "Average Images should accept full image input A");
    Require(graph.TryConnectSockets(imageBId, kImageOutputSocketId, imageAverageId, kMixInputBSocketId),
        "Average Images should accept full image input B");
    Require(!graph.IsScalarSocketStream(imageAverageId, kImageOutputSocketId),
        "Average Images should output a full image stream");
    Require(graph.TryConnectSockets(imageAverageId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "Average Images should feed image outputs");
}

void TestImageAndScalarThroughDataMathStaysImage() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 120.0f }));
    const int multiplyId = NodeId(graph.AddDataMathNode(DataMathMode::Multiply, { 220.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 440.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, multiplyId, kMixInputASocketId),
        "full image should feed Data Math multiply input A");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, multiplyId, kMixInputBSocketId),
        "scalar stream should broadcast into Data Math input B");
    Require(!graph.IsScalarSocketStream(multiplyId, kImageOutputSocketId),
        "Data Math with image plus scalar inputs should remain a full image stream");
    Require(graph.TryConnectSockets(multiplyId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "image plus scalar Data Math output should connect to output image input");
}

void TestFullImageDataMathRejectedByScalarOnlyInputs() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int clampId = NodeId(graph.AddDataMathNode(DataMathMode::Clamp, { 220.0f, 0.0f }));
    const int mixId = NodeId(graph.AddMixNode({ 440.0f, 0.0f }));

    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, clampId, kMixInputASocketId),
        "full image should feed Data Math clamp input A");
    Require(!graph.CanConnectSockets(clampId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "full-image Data Math output should not connect to scalar-only factor inputs");
}

void TestLegacyMaskCombineRemainsScalar() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int maskAId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 0.0f }));
    const int maskBId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::RadialGradient, { 0.0f, 120.0f }));
    const int combineId = NodeId(graph.AddMaskCombineNode(MaskCombineMode::Intersect, { 220.0f, 0.0f }));
    const int previewId = NodeId(graph.AddPreviewNode({ 440.0f, 0.0f }));

    Require(graph.TryConnectSockets(maskAId, kMaskOutputSocketId, combineId, kMaskCombineInputASocketId),
        "legacy MaskCombine input A should still accept scalar streams");
    Require(graph.TryConnectSockets(maskBId, kMaskOutputSocketId, combineId, kMaskCombineInputBSocketId),
        "legacy MaskCombine input B should still accept scalar streams");
    Require(graph.IsScalarSocketStream(combineId, kMaskOutputSocketId),
        "legacy MaskCombine should still output a scalar stream");
    Require(graph.TryConnectSockets(combineId, kMaskOutputSocketId, previewId, kPreviewInputSocketId),
        "legacy MaskCombine saved-node behavior should still preview as scalar");
}

void TestLutNodeConnectionsAndScalarPropagation() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int maskId = NodeId(graph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 120.0f }));
    const int lutId = NodeId(graph.AddLutNode({}, { 220.0f, 0.0f }));
    const int scalarLutId = NodeId(graph.AddLutNode({}, { 220.0f, 160.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 440.0f, 0.0f }, true));
    const int mixId = NodeId(graph.AddMixNode({ 440.0f, 180.0f }));

    Require(graph.CanConnectSockets(imageId, kImageOutputSocketId, lutId, kImageInputSocketId),
        "full image output should connect to LUT image input");
    Require(graph.TryConnectSockets(imageId, kImageOutputSocketId, lutId, kImageInputSocketId),
        "full image output should wire into LUT image input");
    Require(graph.CanConnectSockets(maskId, kMaskOutputSocketId, lutId, kMaskInputSocketId),
        "mask output should connect to LUT mask input");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, lutId, kMaskInputSocketId),
        "mask output should wire into LUT mask input");
    Require(graph.TryConnectSockets(lutId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "LUT image output should connect to output image input");

    Require(graph.CanConnectSockets(maskId, kMaskOutputSocketId, scalarLutId, kImageInputSocketId),
        "scalar mask output should be allowed into LUT image input");
    Require(graph.TryConnectSockets(maskId, kMaskOutputSocketId, scalarLutId, kImageInputSocketId),
        "scalar mask output should connect into LUT image input");
    Require(graph.IsScalarSocketStream(scalarLutId, kImageOutputSocketId),
        "LUT image output should preserve scalar lineage when fed from a scalar image stream");
    Require(graph.TryConnectSockets(scalarLutId, kImageOutputSocketId, mixId, kMixFactorSocketId),
        "scalar LUT output should connect to scalar-only downstream inputs");
}

void TestViewportTilePlannerCoverage() {
    ViewportTilingSettings settings;
    settings.mode = ViewportTilingMode::Always;
    settings.tileSize = 512;
    settings.haloPixels = 16;

    const int width = 1300;
    const int height = 777;
    const std::vector<RenderTileRect> tiles = RenderTiling::PlanTiles(width, height, settings);
    Require(tiles.size() == 6, "odd-sized image should be split into the expected tile count");

    std::vector<unsigned char> coverage(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    for (const RenderTileRect& tile : tiles) {
        Require(tile.width > 0 && tile.height > 0, "tile content rect should be non-empty");
        Require(tile.x >= 0 && tile.y >= 0, "tile content rect should start inside the canvas");
        Require(tile.x + tile.width <= width && tile.y + tile.height <= height,
            "tile content rect should be clamped to the canvas");
        Require(tile.haloX >= 0 && tile.haloY >= 0, "tile halo should start inside the canvas");
        Require(tile.haloX + tile.haloWidth <= width && tile.haloY + tile.haloHeight <= height,
            "tile halo should be clamped to the canvas");
        Require(tile.haloX <= tile.x && tile.haloY <= tile.y,
            "tile halo should include the content origin");
        Require(tile.haloX + tile.haloWidth >= tile.x + tile.width &&
                tile.haloY + tile.haloHeight >= tile.y + tile.height,
            "tile halo should include the full content rect");

        for (int y = tile.y; y < tile.y + tile.height; ++y) {
            for (int x = tile.x; x < tile.x + tile.width; ++x) {
                unsigned char& count = coverage[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                ++count;
            }
        }
    }

    for (unsigned char count : coverage) {
        Require(count == 1, "tile content coverage should have no gaps or duplicate pixels");
    }
}

void TestViewportTilingModeDecisions() {
    ViewportTilingSettings settings;
    settings.tileSize = 1024;
    settings.autoPixelThresholdMegapixels = 4;

    settings.mode = ViewportTilingMode::Off;
    Require(!RenderTiling::ShouldUseTiling(settings, 4096, 4096),
        "Off mode should never select tiled rendering");

    settings.mode = ViewportTilingMode::Always;
    Require(!RenderTiling::ShouldUseTiling(settings, 512, 512),
        "Always mode should skip tiling when the image fits in one tile");
    Require(RenderTiling::ShouldUseTiling(settings, 2048, 512),
        "Always mode should tile images larger than the selected tile size");

    settings.mode = ViewportTilingMode::Auto;
    settings.tileSize = 4096;
    Require(!RenderTiling::ShouldUseTiling(settings, 1000, 1000),
        "Auto mode should leave small images on the full-canvas path");
    Require(RenderTiling::ShouldUseTiling(settings, 2500, 2000),
        "Auto mode should tile images beyond the megapixel threshold");
}

void TestViewportTileSafeGraphClassification() {
    RenderGraphSnapshot graph;
    graph.outputNodeId = 3;

    RenderGraphNode image;
    image.nodeId = 1;
    image.kind = RenderGraphNodeKind::Image;
    image.image.width = 1024;
    image.image.height = 768;
    image.image.channels = 4;
    image.image.pixels = MakeSharedPixelBufferOwned(std::vector<unsigned char>(1024u * 768u * 4u, 255u));

    RenderGraphNode layer;
    layer.nodeId = 2;
    layer.kind = RenderGraphNodeKind::Layer;
    layer.layerJson["type"] = "Brightness";

    RenderGraphNode output;
    output.nodeId = 3;
    output.kind = RenderGraphNodeKind::Output;

    graph.nodes = { image, layer, output };
    graph.links = {
        { 1, "imageOut", 2, "imageIn" },
        { 2, "imageOut", 3, "imageIn" },
    };

    std::string reason;
    Require(RenderTiling::IsGraphTileSafe(graph, 1024, 768, &reason),
        "simple image plus basic adjustment graph should be tile-safe");

    graph.nodes[1].layerJson["type"] = "Blur";
    Require(!RenderTiling::IsGraphTileSafe(graph, 1024, 768, &reason),
        "unsupported layer types should require full-canvas fallback");

    graph.nodes[1].kind = RenderGraphNodeKind::RawDevelop;
    Require(!RenderTiling::IsGraphTileSafe(graph, 1024, 768, &reason),
        "Raw Develop should remain on full-canvas fallback until it is tile-aware");
}

void TestLutImporterCubeVariants() {
    const std::string lut1dPath = WriteTempTextFile(
        "stack_lut_1d",
        ".cube",
        "TITLE \"OneD\"\n"
        "LUT_1D_SIZE 2\n"
        "DOMAIN_MIN 0 0 0\n"
        "DOMAIN_MAX 1 1 1\n"
        "0 0 0\n"
        "1 1 1\n");
    const ColorLut::LutImportResult lut1d = ColorLut::ImportLutFile(lut1dPath);
    Require(lut1d.success, ".cube 1D LUT should import successfully");
    Require(ColorLut::HasLut1D(lut1d.payload), ".cube 1D LUT should populate canonical 1D data");
    Require(!ColorLut::HasLut3D(lut1d.payload), ".cube 1D LUT should not populate 3D data");

    const std::string lut3dPath = WriteTempTextFile(
        "stack_lut_3d",
        ".cube",
        "TITLE \"ThreeD\"\n"
        "LUT_3D_SIZE 2\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "1 1 0\n"
        "0 0 1\n"
        "1 0 1\n"
        "0 1 1\n"
        "1 1 1\n");
    const ColorLut::LutImportResult lut3d = ColorLut::ImportLutFile(lut3dPath);
    Require(lut3d.success, ".cube 3D LUT should import successfully");
    Require(ColorLut::HasLut3D(lut3d.payload), ".cube 3D LUT should populate canonical 3D data");
    Require(!ColorLut::HasLut1D(lut3d.payload), ".cube 3D LUT should not populate standalone 1D data");

    const std::string combinedPath = WriteTempTextFile(
        "stack_lut_combined",
        ".cube",
        "TITLE \"Combined\"\n"
        "LUT_1D_SIZE 2\n"
        "LUT_3D_SIZE 2\n"
        "0 0 0\n"
        "1 1 1\n"
        "0 0 0\n"
        "1 0 0\n"
        "0 1 0\n"
        "1 1 0\n"
        "0 0 1\n"
        "1 0 1\n"
        "0 1 1\n"
        "1 1 1\n");
    const ColorLut::LutImportResult combined = ColorLut::ImportLutFile(combinedPath);
    Require(combined.success, "combined .cube LUT should import successfully");
    Require(ColorLut::HasShaper1D(combined.payload), "combined .cube LUT should populate canonical shaper 1D data");
    Require(ColorLut::HasLut3D(combined.payload), "combined .cube LUT should populate canonical 3D data");

    const std::string invalidPath = WriteTempTextFile(
        "stack_lut_invalid",
        ".cube",
        "TITLE \"Broken\"\n"
        "0 0 0\n");
    const ColorLut::LutImportResult invalid = ColorLut::ImportLutFile(invalidPath);
    Require(!invalid.success, "malformed .cube LUT should fail import");
}

void TestLutCreatorRoundTripSidecar() {
    ColorLut::LutCreatorImage source;
    ColorLut::LutCreatorImage target;
    source.sourcePath = "source.png";
    target.sourcePath = "target.png";
    source.width = 4;
    source.height = 4;
    target.width = 4;
    target.height = 4;
    source.channels = 4;
    source.originalChannels = 4;
    target.channels = 4;
    target.originalChannels = 4;
    source.pixels.resize(4u * 4u * 4u, 255u);
    target.pixels.resize(4u * 4u * 4u, 255u);

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const std::size_t base = (static_cast<std::size_t>(y) * 4u + static_cast<std::size_t>(x)) * 4u;
            const float r = static_cast<float>(x) / 3.0f;
            const float g = static_cast<float>(y) / 3.0f;
            const float b = static_cast<float>(x + y) / 6.0f;
            source.pixels[base] = static_cast<unsigned char>(std::round(r * 255.0f));
            source.pixels[base + 1u] = static_cast<unsigned char>(std::round(g * 255.0f));
            source.pixels[base + 2u] = static_cast<unsigned char>(std::round(b * 255.0f));

            const float mappedR = std::clamp(r * 0.80f + 0.10f, 0.0f, 1.0f);
            const float mappedG = std::clamp(g * 0.65f + 0.18f, 0.0f, 1.0f);
            const float mappedB = std::clamp(b * 0.75f + 0.12f, 0.0f, 1.0f);
            target.pixels[base] = static_cast<unsigned char>(std::round(mappedR * 255.0f));
            target.pixels[base + 1u] = static_cast<unsigned char>(std::round(mappedG * 255.0f));
            target.pixels[base + 2u] = static_cast<unsigned char>(std::round(mappedB * 255.0f));
        }
    }

    ColorLut::LutCreatorSettings settings;
    settings.lutSize = 17;
    settings.maxSamples = 128;
    settings.manualStride = 1;
    settings.smoothPasses = 1;
    settings.smoothStrength = 0.25f;
    settings.identityBias = 0.08f;
    settings.observationThreshold = 1.0f;
    settings.label = "RoundTrip Label";
    settings.importedTitle = "RoundTrip Title";
    settings.useMode = ColorLut::LutUseMode::PreViewTransform;
    settings.inputTransform = ColorLut::LutTransferFunction::SrgbEncode;
    settings.outputTransform = ColorLut::LutTransferFunction::SrgbDecode;

    const ColorLut::LutCreatorResult created =
        ColorLut::CreateLutFromImages(source, target, settings);
    Require(created.success, "LUT creator should succeed for matching source and target images");
    Require(ColorLut::HasLut3D(created.payload), "LUT creator should populate 3D LUT data");
    Require(created.stats.meanAbsoluteError < 0.08f, "LUT creator should fit the test mapping with a low mean absolute error");

    const std::string path = WriteTempTextFile("stack_generated_lut", ".cube", "");
    std::string saveMessage;
    Require(
        ColorLut::SaveCubeLutWithSidecar(
            path,
            created.payload,
            settings,
            created.stats,
            source.sourcePath,
            target.sourcePath,
            &saveMessage),
        "generated LUT should save as .cube plus sidecar");

    const ColorLut::LutImportResult imported = ColorLut::ImportLutFile(path);
    Require(imported.success, "saved generated LUT should import successfully");
    Require(imported.payload.importedTitle == "RoundTrip Title", "generated LUT sidecar should restore the saved imported title");
    Require(imported.payload.label == "RoundTrip Label", "generated LUT sidecar should restore the saved label");
    Require(imported.payload.useMode == ColorLut::LutUseMode::PreViewTransform, "generated LUT sidecar should restore the saved use mode");
    Require(imported.payload.inputTransform == ColorLut::LutTransferFunction::SrgbEncode, "generated LUT sidecar should restore the saved input transform");
    Require(imported.payload.outputTransform == ColorLut::LutTransferFunction::SrgbDecode, "generated LUT sidecar should restore the saved output transform");
}

Stack::Mfsr::MfsrFramePacketSummary TestMfsrFrame(
    Stack::Mfsr::MfsrFrameClass frameClass,
    bool reference,
    int width = 4000,
    int height = 3000) {
    Stack::Mfsr::MfsrFramePacketSummary frame;
    frame.frameClass = frameClass;
    frame.isReference = reference;
    frame.width = width;
    frame.height = height;
    frame.bitDepth = frameClass == Stack::Mfsr::MfsrFrameClass::RasterLinear ? 16 : 14;
    frame.source.sourcePath = reference ? "reference" : "support";
    frame.source.sourceFingerprint = static_cast<std::size_t>(width * 31 + height);
    if (frameClass == Stack::Mfsr::MfsrFrameClass::RawMosaic ||
        frameClass == Stack::Mfsr::MfsrFrameClass::RawLinear) {
        frame.raw.present = true;
        frame.raw.visibleWidth = width;
        frame.raw.visibleHeight = height;
        frame.raw.rawWidth = width;
        frame.raw.rawHeight = height;
        frame.raw.bitDepth = frame.bitDepth;
        frame.raw.cfaPattern = Raw::CfaPattern::RGGB;
        frame.raw.pixelLayout = frameClass == Stack::Mfsr::MfsrFrameClass::RawMosaic
            ? Raw::RawPixelLayout::MosaicBayer
            : Raw::RawPixelLayout::LinearRgb;
        frame.raw.sampleFormat = Raw::RawSampleFormat::UInt16;
        frame.raw.mosaiced = frameClass == Stack::Mfsr::MfsrFrameClass::RawMosaic;
        frame.raw.blackLevel = 512.0f;
        frame.raw.whiteLevel = 16383.0f;
    } else if (frameClass == Stack::Mfsr::MfsrFrameClass::RasterLinear) {
        frame.raster.present = true;
        frame.raster.width = width;
        frame.raster.height = height;
        frame.raster.channels = 4;
        frame.raster.bitDepth = frame.bitDepth;
        frame.raster.linearLight = true;
        frame.raster.colorSpaceKnown = true;
        frame.raster.colorSpaceName = "linear-test";
    }
    return frame;
}

void TestMfsrValidationRejectsEmptyAndMissingReference() {
    using namespace Stack::Mfsr;

    const MfsrValidationResult empty = ValidateMfsrFrameSet({});
    Require(!empty.valid, "MFSR validation should reject zero inputs");
    Require(empty.HasError(MfsrValidationCode::EmptyInputSet), "MFSR zero-input rejection should be explicit");

    const std::vector<MfsrFramePacketSummary> noReference = {
        TestMfsrFrame(MfsrFrameClass::RawMosaic, false)
    };
    const MfsrValidationResult missingReference = ValidateMfsrFrameSet(noReference);
    Require(!missingReference.valid, "MFSR validation should require a reference input");
    Require(missingReference.HasError(MfsrValidationCode::MissingReferenceInput),
        "MFSR missing-reference rejection should be explicit");
}

void TestMfsrValidationRejectsMixedAndUnknownInputs() {
    using namespace Stack::Mfsr;

    const std::vector<MfsrFramePacketSummary> mixed = {
        TestMfsrFrame(MfsrFrameClass::RawMosaic, true),
        TestMfsrFrame(MfsrFrameClass::RasterLinear, false)
    };
    const MfsrValidationResult mixedResult = ValidateMfsrFrameSet(mixed);
    Require(!mixedResult.valid, "MFSR validation should reject mixed RAW/raster bursts");
    Require(mixedResult.inputFamily == MfsrInputFamily::MixedUnsupported,
        "MFSR mixed bursts should report the mixed unsupported family");
    Require(mixedResult.HasError(MfsrValidationCode::MixedInputFamilies),
        "MFSR mixed-family rejection should be explicit");

    std::vector<MfsrFramePacketSummary> unknown = {
        TestMfsrFrame(MfsrFrameClass::Unknown, true)
    };
    const MfsrValidationResult unknownResult = ValidateMfsrFrameSet(unknown);
    Require(!unknownResult.valid, "MFSR validation should reject unknown frame kinds");
    Require(unknownResult.HasError(MfsrValidationCode::UnsupportedInputKind),
        "MFSR unknown-kind rejection should be explicit");
}

void TestMfsrValidationAllowsSingleFamilyWithReference() {
    using namespace Stack::Mfsr;

    std::vector<MfsrFramePacketSummary> rawBurst = {
        TestMfsrFrame(MfsrFrameClass::RawMosaic, true),
        TestMfsrFrame(MfsrFrameClass::RawMosaic, false)
    };
    const MfsrValidationResult rawResult = ValidateMfsrFrameSet(rawBurst);
    Require(rawResult.valid, "MFSR validation should allow a simple RAW burst with one reference");
    Require(rawResult.inputFamily == MfsrInputFamily::RawBurst,
        "MFSR RAW burst validation should report RAW family");
    Require(rawResult.referenceFrameIndex == 0, "MFSR validation should remember the reference frame index");

    rawBurst[1].width = 3996;
    const MfsrValidationResult dimensionWarning = ValidateMfsrFrameSet(rawBurst);
    Require(dimensionWarning.valid, "MFSR Phase 1 should warn, not fail, for dimension mismatch");
    Require(dimensionWarning.HasWarning(MfsrValidationCode::IncompatibleDimensions),
        "MFSR dimension mismatch should produce a warning for later phases");

    rawBurst[1].raw.cfaPattern = Raw::CfaPattern::BGGR;
    const MfsrValidationResult metadataWarning = ValidateMfsrFrameSet(rawBurst);
    Require(metadataWarning.valid, "MFSR Phase 1 should warn, not fail, for RAW metadata mismatch");
    Require(metadataWarning.HasWarning(MfsrValidationCode::IncompatibleRawMetadata),
        "MFSR RAW metadata mismatch should produce a warning for later phases");
}

void TestMfsrCacheKeyFingerprintsReactToInputsAndSettings() {
    using namespace Stack::Mfsr;

    MfsrSettings settingsA;
    MfsrSettings settingsB = settingsA;
    settingsB.scalePreset = MfsrScalePreset::Scale150;

    const std::size_t settingsFingerprintA = BuildMfsrSettingsFingerprint(settingsA);
    const std::size_t settingsFingerprintB = BuildMfsrSettingsFingerprint(settingsB);
    Require(settingsFingerprintA != settingsFingerprintB,
        "MFSR settings fingerprint should change when settings change");

    MfsrCacheKey key;
    key.algorithmVersion = settingsA.algorithmVersion;
    key.inputFamily = MfsrInputFamily::RawBurst;
    key.inputSetFingerprint = BuildMfsrFrameSourceFingerprint(TestMfsrFrame(MfsrFrameClass::RawMosaic, true).source);
    key.settingsFingerprint = settingsFingerprintA;

    const std::size_t keyFingerprintA = BuildMfsrCacheKeyFingerprint(key);
    key.settingsFingerprint = settingsFingerprintB;
    const std::size_t keyFingerprintB = BuildMfsrCacheKeyFingerprint(key);
    Require(keyFingerprintA != keyFingerprintB,
        "MFSR cache-key fingerprint should include settings fingerprint");

    key.settingsFingerprint = settingsFingerprintA;
    key.inputSetFingerprint += 1;
    const std::size_t keyFingerprintC = BuildMfsrCacheKeyFingerprint(key);
    Require(keyFingerprintA != keyFingerprintC,
        "MFSR cache-key fingerprint should include input-set fingerprint");
}

void TestMfsrNodeShellSocketsAndConnections() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageAId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int imageBId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 180.0f }));
    const int mfsrId = NodeId(graph.AddMfsrNode(MfsrPayload{}, { 260.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 520.0f, 0.0f }, true));

    const Node* mfsr = graph.FindNode(mfsrId);
    Require(mfsr && mfsr->kind == NodeKind::Mfsr, "MFSR node shell should be created");
    Require(mfsr->title == "MFSR", "MFSR node shell should have a stable visible title");
    Require(graph.DefaultInputSocket(*mfsr) == kMfsrReferenceInputSocketId,
        "MFSR default input should be the reference socket");
    Require(graph.DefaultOutputSocket(*mfsr) == kImageOutputSocketId,
        "MFSR default output should be the image socket");
    Require(graph.FindSocket(mfsrId, kMfsrReferenceInputSocketId),
        "MFSR should expose a reference input");
    Require(graph.FindSocket(mfsrId, MfsrInputSocketId(1)),
        "MFSR should expose a first support frame input");
    Require(graph.FindSocket(mfsrId, kImageOutputSocketId),
        "MFSR should expose an image output");

    std::vector<SocketDefinition> visibleSockets = graph.GetSockets(*mfsr, true);
    const auto visibleSocketCount = [&](const std::string& socketId) {
        return std::count_if(visibleSockets.begin(), visibleSockets.end(), [&](const SocketDefinition& socket) {
            return socket.id == socketId;
        });
    };
    Require(visibleSocketCount(kMfsrReferenceInputSocketId) == 1,
        "MFSR visible sockets should include Reference");
    Require(visibleSocketCount(MfsrInputSocketId(1)) == 1,
        "MFSR visible sockets should include Frame 2");
    Require(visibleSocketCount(MfsrInputSocketId(2)) == 0,
        "MFSR should not reveal Frame 3 before a support frame is connected");

    Require(graph.TryConnectSockets(imageAId, kImageOutputSocketId, mfsrId, kMfsrReferenceInputSocketId),
        "image should connect to MFSR reference input");
    Require(graph.TryConnectSockets(imageBId, kImageOutputSocketId, mfsrId, MfsrInputSocketId(1)),
        "image should connect to MFSR support input");
    visibleSockets = graph.GetSockets(*graph.FindNode(mfsrId), true);
    Require(std::any_of(visibleSockets.begin(), visibleSockets.end(), [&](const SocketDefinition& socket) {
        return socket.id == MfsrInputSocketId(2);
    }), "MFSR should reveal the next support input after Frame 2 is connected");

    Require(graph.TryConnectSockets(mfsrId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "MFSR image output should connect downstream");
    Require(graph.IsOutputConnected(), "MFSR with reference and support should complete an output chain");
}

void TestMfsrNodeShellRejectsScalarAndMixedFamilies() {
    using namespace EditorNodeGraph;

    Graph scalarGraph;
    const int maskId = NodeId(scalarGraph.AddMaskGeneratorNode(MaskGeneratorKind::Solid, { 0.0f, 0.0f }));
    const int layerId = NodeId(scalarGraph.AddLayerNode(LayerType::Brightness, 0, { 220.0f, 0.0f }));
    const int mfsrScalarId = NodeId(scalarGraph.AddMfsrNode(MfsrPayload{}, { 440.0f, 0.0f }));
    Require(scalarGraph.TryConnectSockets(maskId, kMaskOutputSocketId, layerId, kImageInputSocketId),
        "test setup should connect scalar mask through a layer");
    std::string scalarError;
    Require(!scalarGraph.CanConnectSockets(layerId, kImageOutputSocketId, mfsrScalarId, kMfsrReferenceInputSocketId, nullptr, &scalarError),
        "MFSR should reject scalar image streams");
    Require(scalarError.find("full image") != std::string::npos,
        "MFSR scalar rejection should explain the full-image requirement");

    Graph mixedGraph;
    RawSourcePayload rawPayload;
    rawPayload.label = "RAW";
    rawPayload.sourcePath = "burst-a.dng";
    const int rawSourceId = NodeId(mixedGraph.AddRawSourceNode(rawPayload, { 0.0f, 0.0f }));
    const int rawDevelopId = NodeId(mixedGraph.AddRawDevelopNode(RawDevelopPayload{}, { 220.0f, 0.0f }));
    const int rasterId = NodeId(mixedGraph.AddImageNode(TestImagePayload(), { 0.0f, 220.0f }));
    const int mfsrId = NodeId(mixedGraph.AddMfsrNode(MfsrPayload{}, { 440.0f, 0.0f }));

    Require(mixedGraph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDevelopId, kRawInputSocketId),
        "test setup should connect RAW source to develop");
    Require(mixedGraph.TryConnectSockets(rawDevelopId, kImageOutputSocketId, mfsrId, kMfsrReferenceInputSocketId),
        "MFSR should accept a RAW-derived reference image");

    std::string mixedError;
    Require(!mixedGraph.CanConnectSockets(rasterId, kImageOutputSocketId, mfsrId, MfsrInputSocketId(1), nullptr, &mixedError),
        "MFSR should reject mixed RAW-derived and raster-derived inputs");
    Require(mixedError.find("RAW-derived") != std::string::npos &&
            mixedError.find("raster-derived") != std::string::npos,
        "MFSR mixed-family rejection should name RAW-derived and raster-derived inputs");
}

void TestMfsrNodeShellSerializesRoundTrip() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int imageAId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 0.0f }));
    const int imageBId = NodeId(graph.AddImageNode(TestImagePayload(), { 0.0f, 180.0f }));
    MfsrPayload payload;
    payload.settings.scalePreset = Stack::Mfsr::MfsrScalePreset::Scale150;
    payload.settings.qualityPreset = Stack::Mfsr::MfsrQualityPreset::Conservative;
    payload.placeholderStatus = "Phase 2 placeholder";
    const int mfsrId = NodeId(graph.AddMfsrNode(payload, { 260.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 520.0f, 0.0f }, true));
    Require(graph.TryConnectSockets(imageAId, kImageOutputSocketId, mfsrId, kMfsrReferenceInputSocketId),
        "test setup should connect MFSR reference before serialization");
    Require(graph.TryConnectSockets(imageBId, kImageOutputSocketId, mfsrId, MfsrInputSocketId(1)),
        "test setup should connect MFSR support before serialization");
    Require(graph.TryConnectSockets(mfsrId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "test setup should connect MFSR output before serialization");

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);

    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);
    const Node* loadedMfsr = nullptr;
    for (const Node& node : loaded.GetNodes()) {
        if (node.kind == NodeKind::Mfsr) {
            loadedMfsr = &node;
            break;
        }
    }
    Require(loadedMfsr != nullptr, "MFSR node should survive graph serialization");
    Require(loadedMfsr->title == "MFSR", "MFSR title should survive graph serialization");
    Require(loadedMfsr->mfsr.settings.scalePreset == Stack::Mfsr::MfsrScalePreset::Scale150,
        "MFSR scale setting should survive graph serialization");
    Require(loadedMfsr->mfsr.settings.qualityPreset == Stack::Mfsr::MfsrQualityPreset::Conservative,
        "MFSR quality setting should survive graph serialization");
    Require(loadedMfsr->mfsr.placeholderStatus == "Phase 2 placeholder",
        "MFSR placeholder status should survive graph serialization");
    Require(loaded.IsOutputConnected(), "loaded MFSR graph should keep its completed output chain");
}

void TestCompositeNodeSerializesRoundTrip() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int compositeId = NodeId(graph.AddCompositeNode({ 120.0f, 80.0f }));
    if (Node* compositeNode = graph.FindNode(compositeId)) {
        compositeNode->title = "Composite Test";
        compositeNode->expanded = true;
    }

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);

    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);

    const Node* compositeNode = loaded.FindNode(compositeId);
    Require(compositeNode != nullptr, "Composite node should survive graph serialization");
    Require(compositeNode->kind == NodeKind::Composite, "Composite node should deserialize with the Composite kind");
    Require(compositeNode->title == "Composite Test", "Composite node title should survive graph serialization");
    Require(compositeNode->expanded, "Composite node expanded state should survive graph serialization");
}

void TestHdrMergeDeghostModeMediumRoundTrip() {
    using namespace EditorNodeGraph;

    Graph graph;
    HdrMergePayload payload;
    payload.settings.deghostMode = Raw::HdrMergeDeghostMode::Medium;
    const int hdrMergeId = NodeId(graph.AddHdrMergeNode(std::move(payload), { 180.0f, 110.0f }));

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);

    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);

    const Node* hdrMergeNode = loaded.FindNode(hdrMergeId);
    Require(hdrMergeNode != nullptr, "HDR merge node should survive graph serialization");
    Require(hdrMergeNode->kind == NodeKind::HdrMerge, "HDR merge node kind should survive graph serialization");
    Require(hdrMergeNode->hdrMerge.settings.deghostMode == Raw::HdrMergeDeghostMode::Medium,
        "HDR merge deghost mode Medium should survive graph serialization");
}

void TestRawDevelopmentRecipeDefaultsAndRoundTrip() {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0001.dng", "IMG_0001.dng");
    recipe.source.relativePathKey = "card/IMG_0001.dng";
    recipe.source.fingerprint = "sample-fingerprint";
    recipe.source.fileSizeBytes = 1234567;
    recipe.source.modifiedTimeTicks = 42;
    recipe.preToneExposureEv = 0.75f;
    recipe.whiteBalance.mode = Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
    recipe.whiteBalance.hasMultipliers = true;
    recipe.whiteBalance.multipliers = { 2.0f, 1.0f, 1.5f };
    recipe.whiteBalance.hasTemperatureKelvin = true;
    recipe.whiteBalance.temperatureKelvin = 5200.0f;
    recipe.whiteBalance.hasTint = true;
    recipe.whiteBalance.tint = 8.0f;
    recipe.whiteBalance.hasSamplePoint = true;
    recipe.whiteBalance.sampleX = 0.42f;
    recipe.whiteBalance.sampleY = 0.58f;
    recipe.toneCurve.mode = Stack::RawRecipe::ToneCurveMode::Custom;
    recipe.toneCurve.points = {
        { 0.0f, 0.0f },
        { 0.45f, 0.50f },
        { 1.0f, 1.0f }
    };
    recipe.finishTone.layerJson = Stack::RawRecipe::FinishToneJsonFromLegacyToneCurve(recipe.toneCurve);
    recipe.viewTransform.layerJson = Stack::RawRecipe::DefaultViewTransformJson();
    recipe.viewTransform.layerJson["contrast"] = 1.18f;
    recipe.viewTransform.layerJson["saturation"] = 0.92f;
    recipe.localExposure.enabled = true;
    recipe.localExposure.amount = 0.72f;
    recipe.localExposure.shadowLiftEv = 0.50f;
    recipe.localExposure.highlightCompressionEv = -0.35f;
    recipe.localExposure.localBaselineEv = 0.10f;
    recipe.localExposure.noiseGuardBias = 0.20f;
    recipe.localExposure.highlightGuardBias = 0.15f;
    recipe.localExposure.shadowGuardBias = -0.10f;
    recipe.localExposure.smoothGradientProtection = 0.80f;
    recipe.localExposure.haloGuard = 0.95f;
    recipe.localRange.enabled = true;
    recipe.localRange.strength = 0.80f;
    recipe.localRange.middleGrey = 0.20f;
    recipe.localRange.minEv = -9.0f;
    recipe.localRange.maxEv = 5.0f;
    recipe.localRange.points = {
        { -9.0f, 1.25f },
        { -1.0f, 0.20f },
        { 4.0f, -0.75f }
    };
    recipe.localRange.smoothness = 0.70f;
    recipe.localRange.edgeProtection = 0.82f;
    recipe.localRange.detailProtection = 0.61f;
    recipe.localRange.highlightProtection = 0.55f;
    recipe.localRange.maskPreviewMode = "delta-map";
    recipe.localRange.regionMaskEnabled = true;
    recipe.localRange.regionMaskMode = "radial-gradient";
    recipe.localRange.regionMaskInvert = true;
    recipe.localRange.regionMaskCenterX = 0.35f;
    recipe.localRange.regionMaskCenterY = 0.62f;
    recipe.localRange.regionMaskAngleDegrees = 23.0f;
    recipe.localRange.regionMaskSize = 0.42f;
    recipe.localRange.regionMaskFeather = 0.27f;
    recipe.localRange.regionMaskLowEv = -5.0f;
    recipe.localRange.regionMaskHighEv = 2.5f;
    recipe.localRange.colorMaskEnabled = true;
    recipe.localRange.colorMaskTargetR = 0.12f;
    recipe.localRange.colorMaskTargetG = 0.74f;
    recipe.localRange.colorMaskTargetB = 0.18f;
    recipe.localRange.colorMaskHueWidth = 0.24f;
    recipe.localRange.colorMaskFeather = 0.31f;
    recipe.localRange.colorMaskMinChroma = 0.10f;
    recipe.cropRotation.rotationDegrees = 90;

    const std::vector<std::string>& defaultOrder = Stack::RawRecipe::DefaultStageOrder();
    Require(defaultOrder.size() >= 7, "RAW recipe should define a stable default stage order");
    Require(defaultOrder[0] == "source", "RAW recipe stage order should begin with source");
    Require(std::find(defaultOrder.begin(), defaultOrder.end(), "white-balance") != defaultOrder.end(),
        "RAW recipe stage order should include white balance");
    Require(std::find(defaultOrder.begin(), defaultOrder.end(), "pre-tone-exposure") != defaultOrder.end(),
        "RAW recipe stage order should include pre-tone exposure");
    const auto localExposureStage = std::find(defaultOrder.begin(), defaultOrder.end(), "local-exposure");
    const auto localRangeStage = std::find(defaultOrder.begin(), defaultOrder.end(), "local-range");
    const auto toneCurveStage = std::find(defaultOrder.begin(), defaultOrder.end(), "tone-curve");
    Require(localExposureStage != defaultOrder.end() && toneCurveStage != defaultOrder.end() && localExposureStage < toneCurveStage,
        "RAW recipe stage order should place local exposure before tone curve");
    Require(localRangeStage != defaultOrder.end() && toneCurveStage != defaultOrder.end() && localRangeStage < toneCurveStage,
        "RAW recipe stage order should place local range before tone curve");
    Require(localExposureStage != defaultOrder.end() && localRangeStage != defaultOrder.end() && localExposureStage < localRangeStage,
        "RAW recipe stage order should keep legacy local exposure before local range");
    Require(std::find(defaultOrder.begin(), defaultOrder.end(), "view-transform") != defaultOrder.end(),
        "RAW recipe stage order should include view transform");

    const nlohmann::json serialized = Stack::RawRecipe::SerializeRecipe(recipe);
    Require(serialized.value("rawRecipeVersion", 0) == Stack::RawRecipe::kRawDevelopmentRecipeVersion,
        "RAW recipe should serialize the current compact recipe version");
    Require(serialized.contains("sourceRef"), "RAW recipe should serialize sourceRef");
    Require(serialized.contains("exposureEv"), "RAW recipe should serialize exposureEv");
    Require(serialized.contains("localExposure"), "RAW recipe should serialize localExposure");
    Require(serialized.contains("localRange"), "RAW recipe should serialize localRange");
    Require(serialized.contains("cropRotate"), "RAW recipe should serialize cropRotate");
    Require(serialized.contains("finishTone"), "RAW recipe should serialize finish tone layer state");
    Require(serialized.contains("viewTransform"), "RAW recipe should serialize view transform layer state");
    Require(serialized["previewOutput"].value("intent", std::string()) == "developed-preview",
        "RAW recipe should serialize previewOutput intent");
    const Stack::RawRecipe::RawDevelopmentRecipe loaded =
        Stack::RawRecipe::DeserializeRecipe(serialized);
    Require(loaded.rawRecipeVersion == Stack::RawRecipe::kRawDevelopmentRecipeVersion,
        "RAW recipe version should survive serialization");
    Require(loaded.source.sourcePath == recipe.source.sourcePath,
        "RAW recipe source path should survive serialization");
    Require(loaded.source.relativePathKey == recipe.source.relativePathKey,
        "RAW recipe relative source key should survive serialization");
    Require(loaded.source.fileSizeBytes == recipe.source.fileSizeBytes,
        "RAW recipe source file size should survive serialization");
    Require(loaded.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers,
        "RAW recipe white balance mode should survive serialization");
    Require(std::abs(loaded.whiteBalance.multipliers[0] - 2.0f) < 0.001f,
        "RAW recipe white balance multipliers should survive serialization");
    Require(loaded.toneCurve.mode == Stack::RawRecipe::ToneCurveMode::Custom,
        "RAW recipe tone curve mode should survive serialization");
    Require(loaded.toneCurve.points.size() == 3,
        "RAW recipe tone curve points should survive serialization");
    Require(loaded.finishTone.layerJson.value("type", std::string()) == "ToneCurve",
        "RAW recipe finish tone should deserialize as ToneCurve layer state");
    Require(loaded.finishTone.layerJson.value("domain", -1) == 0,
        "RAW recipe converted legacy finish tone should preserve its scene-linear domain");
    Require(loaded.finishTone.layerJson.contains("points") && loaded.finishTone.layerJson["points"].size() == 3,
        "RAW recipe finish tone points should survive serialization");
    Require(std::abs(loaded.finishTone.layerJson["points"][1].value("x", 0.0f) - 0.45f) < 0.001f &&
            std::abs(loaded.finishTone.layerJson["points"][1].value("y", 0.0f) - 0.50f) < 0.001f,
        "RAW recipe finish tone point coordinates should survive serialization");
    Require(loaded.viewTransform.layerJson.value("type", std::string()) == "ViewTransform",
        "RAW recipe view transform should deserialize as ViewTransform layer state");
    Require(std::abs(loaded.viewTransform.layerJson.value("contrast", 0.0f) - 1.18f) < 0.001f &&
            std::abs(loaded.viewTransform.layerJson.value("saturation", 0.0f) - 0.92f) < 0.001f,
        "RAW recipe view transform values should survive serialization");
    Require(Stack::RawRecipe::IsLocalExposureEnabled(loaded),
        "RAW recipe local exposure enabled state should survive serialization");
    Require(std::abs(loaded.localExposure.shadowLiftEv - 0.50f) < 0.001f &&
            std::abs(loaded.localExposure.highlightCompressionEv - -0.35f) < 0.001f,
        "RAW recipe local exposure EV controls should survive serialization");
    Require(Stack::RawRecipe::IsLocalRangeEnabled(loaded),
        "RAW recipe local range enabled state should survive serialization");
    Require(std::abs(loaded.localRange.strength - 0.80f) < 0.001f &&
            std::abs(loaded.localRange.middleGrey - 0.20f) < 0.001f,
        "RAW recipe local range scalar settings should survive serialization");
    Require(loaded.localRange.points.size() == 3 &&
            std::abs(loaded.localRange.points[0].ev - -9.0f) < 0.001f &&
            std::abs(loaded.localRange.points[0].deltaEv - 1.25f) < 0.001f &&
            std::abs(loaded.localRange.points[2].deltaEv - -0.75f) < 0.001f,
        "RAW recipe local range points should survive serialization");
    Require(loaded.localRange.maskPreviewMode == "delta-map",
        "RAW recipe local range mask preview mode should survive serialization");
    Require(loaded.localRange.regionMaskEnabled &&
            loaded.localRange.regionMaskMode == "radial-gradient" &&
            loaded.localRange.regionMaskInvert,
        "RAW recipe local range region mask mode should survive serialization");
    Require(std::abs(loaded.localRange.regionMaskCenterX - 0.35f) < 0.001f &&
            std::abs(loaded.localRange.regionMaskCenterY - 0.62f) < 0.001f &&
            std::abs(loaded.localRange.regionMaskSize - 0.42f) < 0.001f &&
            std::abs(loaded.localRange.regionMaskFeather - 0.27f) < 0.001f,
        "RAW recipe local range region mask geometry should survive serialization");
    Require(std::abs(loaded.localRange.regionMaskLowEv - -5.0f) < 0.001f &&
            std::abs(loaded.localRange.regionMaskHighEv - 2.5f) < 0.001f,
        "RAW recipe local range luminance region mask EV range should survive serialization");
    Require(loaded.localRange.colorMaskEnabled &&
            std::abs(loaded.localRange.colorMaskTargetR - 0.12f) < 0.001f &&
            std::abs(loaded.localRange.colorMaskTargetG - 0.74f) < 0.001f &&
            std::abs(loaded.localRange.colorMaskTargetB - 0.18f) < 0.001f,
        "RAW recipe local range color qualification target should survive serialization");
    Require(std::abs(loaded.localRange.colorMaskHueWidth - 0.24f) < 0.001f &&
            std::abs(loaded.localRange.colorMaskFeather - 0.31f) < 0.001f &&
            std::abs(loaded.localRange.colorMaskMinChroma - 0.10f) < 0.001f,
        "RAW recipe local range color qualification width and guards should survive serialization");
    Require(loaded.previewOutput.internalViewTransform == "scene-linear-to-display",
        "RAW recipe should carry an internal view transform mapping");

    const Raw::RawDevelopSettings rawSettings = Stack::RawRecipe::ToRawDevelopSettings(loaded);
    Require(std::abs(rawSettings.exposureStops - 0.75f) < 0.001f,
        "RAW recipe pre-tone exposure should map to RAW develop settings");
    Require(rawSettings.whiteBalanceMode == Raw::WhiteBalanceMode::Manual,
        "RAW recipe custom multipliers should map to manual RAW white balance");
    Require(rawSettings.toneCurvePoints.empty(),
        "RAW recipe finish tone should be applied after RAW develop settings");
    Require(rawSettings.rotationDegrees == 90,
        "RAW recipe rotation placeholder should map to RAW develop settings");
    const Raw::RawDetailFusionSettings localExposureSettings =
        Stack::RawRecipe::ToRawDetailFusionSettings(loaded);
    Require(std::abs(localExposureSettings.strength - 0.72f) < 0.001f,
        "RAW recipe local exposure amount should map to RawDetailFusion strength");
    Require(localExposureSettings.overrideMaxEv && localExposureSettings.overrideMinEv && localExposureSettings.overrideBaseEv,
        "RAW recipe local exposure should use explicit RawDetailFusion EV overrides");
    Require(std::abs(localExposureSettings.maxEv - 0.50f) < 0.001f &&
            std::abs(localExposureSettings.minEv - -0.35f) < 0.001f &&
            std::abs(localExposureSettings.baseEv - 0.10f) < 0.001f,
        "RAW recipe local exposure EV controls should map to explicit RawDetailFusion EV windows");
    Require(std::abs(localExposureSettings.smoothGradientProtection - 0.80f) < 0.001f &&
            std::abs(localExposureSettings.haloGuard - 0.95f) < 0.001f,
        "RAW recipe local exposure guards should map to RawDetailFusion protection settings");

    Stack::RawRecipe::RawDevelopmentRecipe defaultRecipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0003.dng", "IMG_0003.dng");
    Require(defaultRecipe.finishTone.layerJson.value("type", std::string()) == "ToneCurve" &&
            defaultRecipe.finishTone.layerJson.value("mode", -1) == 1 &&
            defaultRecipe.finishTone.layerJson.value("domain", -1) == 1,
        "RAW recipe defaults should use RGB Log Scene finish tone");
    Require(defaultRecipe.viewTransform.layerJson.value("type", std::string()) == "ViewTransform",
        "RAW recipe defaults should include view transform layer state");
    const Raw::RawDevelopSettings defaultSettings =
        Stack::RawRecipe::ToRawDevelopSettings(defaultRecipe);
    Require(defaultSettings.toneCurvePoints.empty(),
        "RAW recipe default tone curve should remain identity in RAW develop settings");
    Require(!Stack::RawRecipe::IsLocalExposureEnabled(defaultRecipe),
        "RAW recipe local exposure should be disabled by default");
    Require(!Stack::RawRecipe::IsLocalRangeEnabled(defaultRecipe),
        "RAW recipe local range should be disabled by default");
    Require(defaultRecipe.localRange.points.size() == 3 &&
            std::abs(defaultRecipe.localRange.points.front().ev - -8.0f) < 0.001f &&
            std::abs(defaultRecipe.localRange.points.back().ev - 6.0f) < 0.001f,
        "RAW recipe local range should use identity EV anchor points by default");
    Require(std::abs(Stack::RawRecipe::ToRawDetailFusionSettings(defaultRecipe).strength - 1.0f) < 0.001f,
        "RAW recipe local exposure should use full strength by default for direct EV budgets");
    defaultRecipe.localExposure.enabled = true;
    Require(!Stack::RawRecipe::IsLocalExposureEnabled(defaultRecipe),
        "RAW recipe local exposure should stay neutral when enabled with no direct EV budget");
    defaultRecipe.localExposure.shadowLiftEv = 1.25f;
    Require(Stack::RawRecipe::IsLocalExposureEnabled(defaultRecipe),
        "RAW recipe local exposure should enable when a direct EV budget is present");
    defaultRecipe.localRange.enabled = true;
    Require(!Stack::RawRecipe::IsLocalRangeEnabled(defaultRecipe),
        "RAW recipe local range should stay neutral when enabled with identity points");
    defaultRecipe.localRange.points[1].deltaEv = 1.0f;
    Require(Stack::RawRecipe::IsLocalRangeEnabled(defaultRecipe),
        "RAW recipe local range should enable when an EV point delta is present");
    const Stack::RawRecipe::RawLocalRangeRecipe openShadowsPreset =
        Stack::RawRecipe::ApplyLocalRangePreset(
            Stack::RawRecipe::DefaultLocalRangeRecipe(),
            Stack::RawRecipe::RawLocalRangePreset::OpenShadows);
    Require(Stack::RawRecipe::IsLocalRangeEnabled(openShadowsPreset) &&
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(openShadowsPreset, -5.0f) > 1.0f &&
            std::abs(Stack::RawRecipe::EvaluateLocalRangeDeltaEv(openShadowsPreset, 0.0f)) < 0.05f,
        "RAW recipe Open Shadows preset should lift shadow EV zones while leaving midtones near identity");
    const Stack::RawRecipe::RawLocalRangeRecipe holdHighlightsPreset =
        Stack::RawRecipe::ApplyLocalRangePreset(
            Stack::RawRecipe::DefaultLocalRangeRecipe(),
            Stack::RawRecipe::RawLocalRangePreset::HoldHighlights);
    Require(Stack::RawRecipe::IsLocalRangeEnabled(holdHighlightsPreset) &&
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(holdHighlightsPreset, 4.0f) < -0.80f &&
            std::abs(Stack::RawRecipe::EvaluateLocalRangeDeltaEv(holdHighlightsPreset, -4.0f)) < 0.05f,
        "RAW recipe Hold Highlights preset should compress highlight EV zones while leaving shadows near identity");
    const Stack::RawRecipe::RawLocalRangeRecipe compressRangePreset =
        Stack::RawRecipe::ApplyLocalRangePreset(
            Stack::RawRecipe::DefaultLocalRangeRecipe(),
            Stack::RawRecipe::RawLocalRangePreset::CompressRange);
    Require(Stack::RawRecipe::EvaluateLocalRangeDeltaEv(compressRangePreset, -3.0f) > 0.50f &&
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(compressRangePreset, 4.0f) < -0.65f,
        "RAW recipe Compress Range preset should lift dark zones and compress bright zones");
    const Stack::RawRecipe::RawLocalRangeRecipe resetPreset =
        Stack::RawRecipe::ApplyLocalRangePreset(
            compressRangePreset,
            Stack::RawRecipe::RawLocalRangePreset::Reset);
    Require(!Stack::RawRecipe::IsLocalRangeEnabled(resetPreset),
        "RAW recipe Reset preset should return Local Range to disabled identity");
    Stack::RawRecipe::RawLocalExposureRecipe legacyLocalExposure;
    legacyLocalExposure.enabled = true;
    legacyLocalExposure.amount = 0.80f;
    legacyLocalExposure.shadowLiftEv = 1.50f;
    legacyLocalExposure.highlightCompressionEv = -1.00f;
    legacyLocalExposure.localBaselineEv = 0.20f;
    legacyLocalExposure.smoothGradientProtection = 0.90f;
    legacyLocalExposure.haloGuard = 0.95f;
    const Stack::RawRecipe::RawLocalRangeRecipe convertedLegacyRange =
        Stack::RawRecipe::LocalRangeRecipeFromLocalExposure(
            legacyLocalExposure,
            Stack::RawRecipe::DefaultLocalRangeRecipe());
    Require(Stack::RawRecipe::IsLocalRangeEnabled(convertedLegacyRange) &&
            std::abs(convertedLegacyRange.strength - 0.80f) < 0.001f &&
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(convertedLegacyRange, -4.0f) > 0.55f &&
            Stack::RawRecipe::EvaluateLocalRangeDeltaEv(convertedLegacyRange, 3.0f) < -0.20f,
        "RAW recipe legacy Local Exposure conversion should produce a comparable Local Range graph");

    Stack::RawRecipe::RawLocalRangeRecipe gradientLocalRange =
        Stack::RawRecipe::DefaultLocalRangeRecipe();
    gradientLocalRange.enabled = true;
    gradientLocalRange.strength = 1.0f;
    gradientLocalRange.points = {
        { -8.0f, 0.0f },
        { -3.0f, 1.0f },
        { 0.0f, 0.0f },
        { 4.0f, -1.0f },
        { 6.0f, 0.0f }
    };
    const float middleGrey = gradientLocalRange.middleGrey;
    const float shadowLuma = middleGrey * std::pow(2.0f, -3.0f);
    const float midtoneLuma = middleGrey;
    const float highlightLuma = middleGrey * std::pow(2.0f, 4.0f);
    Require(std::abs(Stack::RawRecipe::EvaluateLocalRangeDeltaEv(gradientLocalRange, -3.0f) - 1.0f) < 0.001f,
        "RAW recipe local range evaluator should return the requested shadow-zone EV delta");
    Require(std::abs(Stack::RawRecipe::LocalRangeExposureScaleForLuma(gradientLocalRange, shadowLuma) - 2.0f) < 0.02f,
        "RAW recipe local range should brighten a matching shadow zone by roughly one stop");
    Require(std::abs(Stack::RawRecipe::LocalRangeExposureScaleForLuma(gradientLocalRange, midtoneLuma) - 1.0f) < 0.02f,
        "RAW recipe local range should keep identity midtones near identity when the curve says zero");
    Require(std::abs(Stack::RawRecipe::LocalRangeExposureScaleForLuma(gradientLocalRange, highlightLuma) - 0.5f) < 0.02f,
        "RAW recipe local range should compress a matching highlight zone by roughly one stop");
    gradientLocalRange.enabled = false;
    Require(std::abs(Stack::RawRecipe::LocalRangeExposureScaleForLuma(gradientLocalRange, shadowLuma) - 1.0f) < 0.001f,
        "RAW recipe disabled local range should be indistinguishable from no local range");

    Stack::RawRecipe::RawLocalRangeRecipe edgeAwareLocalRange =
        Stack::RawRecipe::DefaultLocalRangeRecipe();
    edgeAwareLocalRange.enabled = true;
    edgeAwareLocalRange.strength = 1.0f;
    edgeAwareLocalRange.smoothness = 1.0f;
    edgeAwareLocalRange.edgeProtection = 0.92f;
    edgeAwareLocalRange.detailProtection = 0.80f;
    edgeAwareLocalRange.highlightProtection = 0.50f;
    edgeAwareLocalRange.points = {
        { -8.0f, 0.0f },
        { -3.0f, 1.0f },
        { 0.0f, 0.0f },
        { 4.0f, -1.0f },
        { 6.0f, 0.0f }
    };
    const float protectedDarkDelta = Stack::RawRecipe::EdgeAwareLocalRangeDeltaEvForSamples(
        edgeAwareLocalRange,
        -3.0f,
        { -3.2f, -2.9f, -3.1f, 4.0f, 4.2f, 3.8f });
    Stack::RawRecipe::RawLocalRangeRecipe unprotectedEdgeRange = edgeAwareLocalRange;
    unprotectedEdgeRange.edgeProtection = 0.0f;
    const float bleedingDarkDelta = Stack::RawRecipe::EdgeAwareLocalRangeDeltaEvForSamples(
        unprotectedEdgeRange,
        -3.0f,
        { -3.2f, -2.9f, -3.1f, 4.0f, 4.2f, 3.8f });
    Require(protectedDarkDelta > 0.70f && protectedDarkDelta > bleedingDarkDelta + 0.45f,
        "RAW recipe edge-aware local range should let dark regions lift while rejecting bright cross-edge samples");

    Stack::RawRecipe::RawLocalRangeRecipe detailAwareLocalRange =
        Stack::RawRecipe::DefaultLocalRangeRecipe();
    detailAwareLocalRange.enabled = true;
    detailAwareLocalRange.strength = 1.0f;
    detailAwareLocalRange.smoothness = 1.0f;
    detailAwareLocalRange.edgeProtection = 0.70f;
    detailAwareLocalRange.detailProtection = 1.0f;
    detailAwareLocalRange.highlightProtection = 0.50f;
    detailAwareLocalRange.points = {
        { -8.0f, 0.0f },
        { -4.0f, 2.0f },
        { -3.0f, 0.0f },
        { 6.0f, 0.0f }
    };
    const float directTextureDeltaSpread = std::abs(
        Stack::RawRecipe::EvaluateLocalRangeDeltaEv(detailAwareLocalRange, -4.20f) -
        Stack::RawRecipe::EvaluateLocalRangeDeltaEv(detailAwareLocalRange, -3.80f));
    const float edgeAwareTextureDeltaSpread = std::abs(
        Stack::RawRecipe::EdgeAwareLocalRangeDeltaEvForSamples(
            detailAwareLocalRange,
            -4.20f,
            { -4.05f, -3.95f, -4.10f, -3.90f }) -
        Stack::RawRecipe::EdgeAwareLocalRangeDeltaEvForSamples(
            detailAwareLocalRange,
            -3.80f,
            { -4.05f, -3.95f, -4.10f, -3.90f }));
    Require(edgeAwareTextureDeltaSpread < directTextureDeltaSpread * 0.70f,
        "RAW recipe detail-aware local range should reduce per-pixel texture-driven EV variation");

    Stack::RawRecipe::RawLocalRangeRecipe linearMaskRange = gradientLocalRange;
    linearMaskRange.enabled = true;
    linearMaskRange.regionMaskEnabled = true;
    linearMaskRange.regionMaskMode = "linear-gradient";
    linearMaskRange.regionMaskInvert = false;
    linearMaskRange.regionMaskCenterX = 0.5f;
    linearMaskRange.regionMaskCenterY = 0.5f;
    linearMaskRange.regionMaskAngleDegrees = 0.0f;
    linearMaskRange.regionMaskSize = 0.25f;
    linearMaskRange.regionMaskFeather = 0.5f;
    Require(Stack::RawRecipe::EvaluateLocalRangeRegionMask(linearMaskRange, 0.10f, 0.5f, -3.0f) < 0.05f &&
            Stack::RawRecipe::EvaluateLocalRangeRegionMask(linearMaskRange, 0.90f, 0.5f, -3.0f) > 0.95f,
        "RAW recipe linear region mask should gate one side of the image with a soft transition");

    Stack::RawRecipe::RawLocalRangeRecipe radialMaskRange = gradientLocalRange;
    radialMaskRange.enabled = true;
    radialMaskRange.regionMaskEnabled = true;
    radialMaskRange.regionMaskMode = "radial-gradient";
    radialMaskRange.regionMaskCenterX = 0.5f;
    radialMaskRange.regionMaskCenterY = 0.5f;
    radialMaskRange.regionMaskSize = 0.25f;
    radialMaskRange.regionMaskFeather = 0.20f;
    Require(Stack::RawRecipe::EvaluateLocalRangeRegionMask(radialMaskRange, 0.50f, 0.5f, -3.0f) > 0.95f &&
            Stack::RawRecipe::EvaluateLocalRangeRegionMask(radialMaskRange, 0.95f, 0.95f, -3.0f) < 0.05f,
        "RAW recipe radial region mask should keep the center selected and fade out beyond the radius");

    Stack::RawRecipe::RawLocalRangeRecipe luminanceMaskRange = gradientLocalRange;
    luminanceMaskRange.enabled = true;
    luminanceMaskRange.regionMaskEnabled = true;
    luminanceMaskRange.regionMaskMode = "luminance-range";
    luminanceMaskRange.regionMaskLowEv = -4.0f;
    luminanceMaskRange.regionMaskHighEv = 2.0f;
    luminanceMaskRange.regionMaskFeather = 0.25f;
    Require(Stack::RawRecipe::EvaluateLocalRangeRegionMask(luminanceMaskRange, 0.50f, 0.5f, -3.0f) > 0.95f &&
            Stack::RawRecipe::EvaluateLocalRangeRegionMask(luminanceMaskRange, 0.50f, 0.5f, 5.0f) < 0.05f,
        "RAW recipe luminance range mask should gate by scene EV independently of image position");
    luminanceMaskRange.regionMaskInvert = true;
    Require(Stack::RawRecipe::EvaluateLocalRangeRegionMask(luminanceMaskRange, 0.50f, 0.5f, -3.0f) < 0.05f &&
            Stack::RawRecipe::EvaluateLocalRangeRegionMask(luminanceMaskRange, 0.50f, 0.5f, 5.0f) > 0.95f,
        "RAW recipe local range region mask invert should flip the gate");

    Stack::RawRecipe::RawLocalRangeRecipe colorMaskRange = gradientLocalRange;
    colorMaskRange.enabled = true;
    colorMaskRange.colorMaskEnabled = true;
    colorMaskRange.colorMaskTargetR = 0.12f;
    colorMaskRange.colorMaskTargetG = 0.74f;
    colorMaskRange.colorMaskTargetB = 0.18f;
    colorMaskRange.colorMaskHueWidth = 0.24f;
    colorMaskRange.colorMaskFeather = 0.35f;
    colorMaskRange.colorMaskMinChroma = 0.10f;
    Require(Stack::RawRecipe::EvaluateLocalRangeColorMask(colorMaskRange, 0.10f, 0.72f, 0.16f) > 0.90f,
        "RAW recipe local range color qualifier should accept scene colors near the sampled target");
    Require(Stack::RawRecipe::EvaluateLocalRangeColorMask(colorMaskRange, 0.12f, 0.22f, 0.82f) < 0.20f,
        "RAW recipe local range color qualifier should reject a different saturated hue at similar brightness");
    Require(Stack::RawRecipe::EvaluateLocalRangeColorMask(colorMaskRange, 0.80f, 0.82f, 0.78f) < 0.20f,
        "RAW recipe local range color qualifier should reject low-chroma white or grey regions for colored targets");
    colorMaskRange.colorMaskEnabled = false;
    Require(std::abs(Stack::RawRecipe::EvaluateLocalRangeColorMask(colorMaskRange, 0.12f, 0.22f, 0.82f) - 1.0f) < 0.001f,
        "RAW recipe disabled local range color qualifier should be neutral");

    nlohmann::json clampedLocalExposureRecipe = serialized;
    clampedLocalExposureRecipe["localExposure"]["amount"] = 2.0f;
    clampedLocalExposureRecipe["localExposure"]["shadowLiftEv"] = 9.0f;
    clampedLocalExposureRecipe["localExposure"]["highlightCompressionEv"] = -9.0f;
    const Stack::RawRecipe::RawDevelopmentRecipe clampedLocalExposure =
        Stack::RawRecipe::DeserializeRecipe(clampedLocalExposureRecipe);
    Require(std::abs(clampedLocalExposure.localExposure.amount - 1.0f) < 0.001f &&
            std::abs(clampedLocalExposure.localExposure.shadowLiftEv - 4.0f) < 0.001f &&
            std::abs(clampedLocalExposure.localExposure.highlightCompressionEv - -4.0f) < 0.001f,
        "RAW recipe local exposure should clamp direct EV budgets to the supported range");
    const Raw::RawDetailFusionSettings clampedLocalExposureSettings =
        Stack::RawRecipe::ToRawDetailFusionSettings(clampedLocalExposure);
    Require(std::abs(clampedLocalExposureSettings.maxEv - 4.0f) < 0.001f &&
            std::abs(clampedLocalExposureSettings.minEv - -4.0f) < 0.001f,
        "RAW recipe clamped direct EV budgets should map to explicit RawDetailFusion limits");

    nlohmann::json clampedLocalRangeRecipe = serialized;
    clampedLocalRangeRecipe["localRange"]["strength"] = 2.0f;
    clampedLocalRangeRecipe["localRange"]["middleGrey"] = 5.0f;
    clampedLocalRangeRecipe["localRange"]["minEv"] = -40.0f;
    clampedLocalRangeRecipe["localRange"]["maxEv"] = 40.0f;
    clampedLocalRangeRecipe["localRange"]["points"] = nlohmann::json::array({
        { { "ev", -99.0f }, { "deltaEv", 9.0f } },
        { { "ev", 99.0f }, { "deltaEv", -9.0f } }
    });
    clampedLocalRangeRecipe["localRange"]["maskPreviewMode"] = "not-a-mode";
    clampedLocalRangeRecipe["localRange"]["regionMaskMode"] = "not-a-mask";
    clampedLocalRangeRecipe["localRange"]["regionMaskCenterX"] = -3.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskCenterY"] = 4.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskAngleDegrees"] = 420.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskSize"] = -1.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskFeather"] = 9.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskLowEv"] = 14.0f;
    clampedLocalRangeRecipe["localRange"]["regionMaskHighEv"] = 12.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskTargetR"] = -4.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskTargetG"] = 44.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskTargetB"] = 128.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskHueWidth"] = 9.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskFeather"] = -3.0f;
    clampedLocalRangeRecipe["localRange"]["colorMaskMinChroma"] = 2.0f;
    const Stack::RawRecipe::RawDevelopmentRecipe clampedLocalRange =
        Stack::RawRecipe::DeserializeRecipe(clampedLocalRangeRecipe);
    Require(std::abs(clampedLocalRange.localRange.strength - 1.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.middleGrey - 1.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.minEv - -16.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.maxEv - 16.0f) < 0.001f,
        "RAW recipe local range scalar fields should clamp to supported ranges");
    Require(clampedLocalRange.localRange.points.size() == 2 &&
            std::abs(clampedLocalRange.localRange.points[0].ev - -16.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.points[0].deltaEv - 4.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.points[1].ev - 16.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.points[1].deltaEv - -4.0f) < 0.001f,
        "RAW recipe local range points should clamp and sort by scene EV");
    Require(clampedLocalRange.localRange.maskPreviewMode == "none",
        "RAW recipe local range should reject unknown mask preview modes");
    Require(clampedLocalRange.localRange.regionMaskMode == "linear-gradient" &&
            std::abs(clampedLocalRange.localRange.regionMaskCenterX - 0.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.regionMaskCenterY - 1.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.regionMaskAngleDegrees - 180.0f) < 0.001f,
        "RAW recipe local range region mask geometry should clamp to supported ranges");
    Require(std::abs(clampedLocalRange.localRange.regionMaskSize - 0.02f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.regionMaskFeather - 1.0f) < 0.001f &&
            clampedLocalRange.localRange.regionMaskHighEv > clampedLocalRange.localRange.regionMaskLowEv,
        "RAW recipe local range region mask feather, size, and EV range should sanitize");
    Require(std::abs(clampedLocalRange.localRange.colorMaskTargetR - 0.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskTargetG - 32.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskTargetB - 32.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskHueWidth - 1.20f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskFeather - 0.0f) < 0.001f &&
            std::abs(clampedLocalRange.localRange.colorMaskMinChroma - 1.0f) < 0.001f,
        "RAW recipe local range color qualification values should clamp to supported ranges");

    Stack::RawRecipe::RawDevelopmentRecipe finishChanged = loaded;
    finishChanged.finishTone.layerJson["domain"] = 1;
    Require(!Stack::RawRecipe::FinishStateEquals(loaded, finishChanged),
        "RAW recipe finish-state comparison should detect finish tone changes");
    Require(Stack::RawRecipe::FinishStateHash(loaded) != Stack::RawRecipe::FinishStateHash(finishChanged),
        "RAW recipe finish-state hash should detect finish tone changes");
    Stack::RawRecipe::RawDevelopmentRecipe viewChanged = loaded;
    viewChanged.viewTransform.layerJson["contrast"] = 1.40f;
    Require(!Stack::RawRecipe::FinishStateEquals(loaded, viewChanged),
        "RAW recipe finish-state comparison should detect view transform changes");
    Stack::RawRecipe::RawDevelopmentRecipe localRangeChanged = loaded;
    localRangeChanged.localRange.points[1].deltaEv = 1.50f;
    Require(!Stack::RawRecipe::LocalRangeStateEquals(loaded, localRangeChanged),
        "RAW recipe local-range comparison should detect point changes");
    Require(Stack::RawRecipe::LocalRangeStateHash(loaded) != Stack::RawRecipe::LocalRangeStateHash(localRangeChanged),
        "RAW recipe local-range hash should detect point changes");
    Require(Stack::RawRecipe::SerializeRecipe(loaded).dump() != Stack::RawRecipe::SerializeRecipe(localRangeChanged).dump(),
        "RAW Development render identity should change when enabled local range points change");
    Stack::RawRecipe::RawDevelopmentRecipe localRangeMaskChanged = loaded;
    localRangeMaskChanged.localRange.regionMaskCenterX = 0.75f;
    Require(!Stack::RawRecipe::LocalRangeStateEquals(loaded, localRangeMaskChanged),
        "RAW recipe local-range comparison should detect region mask changes");
    Require(Stack::RawRecipe::LocalRangeStateHash(loaded) != Stack::RawRecipe::LocalRangeStateHash(localRangeMaskChanged),
        "RAW recipe local-range hash should detect region mask changes");
    Require(Stack::RawRecipe::SerializeRecipe(loaded).dump() != Stack::RawRecipe::SerializeRecipe(localRangeMaskChanged).dump(),
        "RAW Development render identity should change when local range region mask changes");
    Stack::RawRecipe::RawDevelopmentRecipe localRangeColorChanged = loaded;
    localRangeColorChanged.localRange.colorMaskTargetG = 0.52f;
    Require(!Stack::RawRecipe::LocalRangeStateEquals(loaded, localRangeColorChanged),
        "RAW recipe local-range comparison should detect color qualification changes");
    Require(Stack::RawRecipe::LocalRangeStateHash(loaded) != Stack::RawRecipe::LocalRangeStateHash(localRangeColorChanged),
        "RAW recipe local-range hash should detect color qualification changes");
    Require(Stack::RawRecipe::SerializeRecipe(loaded).dump() != Stack::RawRecipe::SerializeRecipe(localRangeColorChanged).dump(),
        "RAW Development render identity should change when local range color qualification changes");

    nlohmann::json legacyRecipe = serialized;
    legacyRecipe["rawRecipeVersion"] = 2;
    legacyRecipe.erase("localRange");
    legacyRecipe.erase("finishTone");
    legacyRecipe.erase("viewTransform");
    const Stack::RawRecipe::RawDevelopmentRecipe migrated =
        Stack::RawRecipe::DeserializeRecipe(legacyRecipe);
    Require(migrated.rawRecipeVersion == Stack::RawRecipe::kRawDevelopmentRecipeVersion,
        "legacy RAW recipes should migrate to the current compact recipe version");
    Require(migrated.finishTone.layerJson.value("type", std::string()) == "ToneCurve" &&
            migrated.finishTone.layerJson.value("domain", -1) == 0,
        "legacy non-identity RAW tone curves should migrate into scene-linear finish tone state");
    Require(migrated.finishTone.layerJson.contains("points") && migrated.finishTone.layerJson["points"].size() == 3,
        "legacy RAW tone curve points should migrate into finish tone points");
    Require(migrated.viewTransform.layerJson.value("type", std::string()) == "ViewTransform",
        "legacy RAW recipes should receive default view transform state");
    Require(!Stack::RawRecipe::IsLocalRangeEnabled(migrated),
        "legacy RAW recipes without localRange should receive disabled identity local range state");

    nlohmann::json legacyLocalExposureOnlyRecipe = serialized;
    legacyLocalExposureOnlyRecipe["rawRecipeVersion"] = 3;
    legacyLocalExposureOnlyRecipe.erase("localRange");
    const Stack::RawRecipe::RawDevelopmentRecipe legacyLocalExposureOnlyLoaded =
        Stack::RawRecipe::DeserializeRecipe(legacyLocalExposureOnlyRecipe);
    Require(Stack::RawRecipe::IsLocalExposureEnabled(legacyLocalExposureOnlyLoaded),
        "existing RAW projects with legacy localExposure should still load that state");
    Require(!Stack::RawRecipe::IsLocalRangeEnabled(legacyLocalExposureOnlyLoaded),
        "existing RAW projects without localRange should not silently create an active local range edit");

    nlohmann::json legacyStageOrderRecipe = serialized;
    legacyStageOrderRecipe["stageOrder"] = {
        "source",
        "raw-decode",
        "white-balance",
        "pre-tone-exposure",
        "tone-curve",
        "view-transform",
        "crop-rotation",
        "output"
    };
    const Stack::RawRecipe::RawDevelopmentRecipe legacyLoaded =
        Stack::RawRecipe::DeserializeRecipe(legacyStageOrderRecipe);
    const auto legacyLocalExposureStage =
        std::find(legacyLoaded.stageOrder.begin(), legacyLoaded.stageOrder.end(), "local-exposure");
    const auto legacyLocalRangeStage =
        std::find(legacyLoaded.stageOrder.begin(), legacyLoaded.stageOrder.end(), "local-range");
    const auto legacyToneCurveStage =
        std::find(legacyLoaded.stageOrder.begin(), legacyLoaded.stageOrder.end(), "tone-curve");
    Require(legacyLocalExposureStage != legacyLoaded.stageOrder.end() &&
            legacyLocalRangeStage != legacyLoaded.stageOrder.end() &&
            legacyToneCurveStage != legacyLoaded.stageOrder.end() &&
            legacyLocalExposureStage < legacyLocalRangeStage &&
            legacyLocalRangeStage < legacyToneCurveStage,
        "RAW recipe should normalize legacy stage order with local exposure and local range before tone curve");
}

void TestRawImageAnalysisPercentilesAndFallbackGuards() {
    using Stack::RawAnalysis::AnalysisStageStatus;

    Stack::RawAnalysis::PercentileStats stats =
        Stack::RawAnalysis::BuildPercentileStatsFromLumas(
            { 0.01f, 0.02f, 0.18f, 1.0f, 4.0f },
            87.5f,
            AnalysisStageStatus::Complete,
            "test stats");
    Require(stats.valid, "RAW image analysis percentile stats should be valid for non-empty luma samples");
    Require(stats.status == AnalysisStageStatus::Complete, "RAW image analysis should preserve stage status");
    Require(std::abs(stats.p50Luma - 0.18f) < 0.0001f, "RAW image analysis median luma should be stable");
    Require(std::abs(stats.p50Ev - Stack::RawAnalysis::SafeLog2Luma(0.18f)) < 0.0001f,
        "RAW image analysis median EV should use safe log2 luma");
    Require(stats.dynamicRangeEv > 8.0f, "RAW image analysis should report broad percentile dynamic range");
    Require(std::abs(stats.validPixelPercent - 87.5f) < 0.0001f,
        "RAW image analysis should preserve valid pixel percentage");

    Stack::RawAnalysis::CurrentFrameInputStats currentFrameStats;
    currentFrameStats.valid = true;
    currentFrameStats.p001Luma = 0.005f;
    currentFrameStats.p01Luma = 0.01f;
    currentFrameStats.p05Luma = 0.03f;
    currentFrameStats.p50Luma = 0.18f;
    currentFrameStats.p95Luma = 1.2f;
    currentFrameStats.p99Luma = 2.0f;
    currentFrameStats.p999Luma = 3.0f;
    currentFrameStats.logAverageLuma = 0.16f;
    currentFrameStats.dynamicRangeEv = 7.64f;
    currentFrameStats.validPixelPercent = 100.0f;
    currentFrameStats.hdrPixelPercent = 2.5f;
    currentFrameStats.displayClipPercent = 0.25f;

    const Stack::RawAnalysis::RawImageAnalysis analysis =
        Stack::RawAnalysis::BuildCurrentFrameAnalysisFromCurrentFrameStats(currentFrameStats, "raw/test.dng");
    Require(analysis.valid, "RAW current-frame analysis should be valid when texture stats are valid");
    Require(analysis.currentFrameStats.valid, "RAW current-frame stats should be populated");
    Require(analysis.currentFrameStats.status == AnalysisStageStatus::Complete,
        "RAW current-frame analysis should be marked complete for rendered stats");
    Require(analysis.technicalStats.status == AnalysisStageStatus::Unavailable,
        "RAW technical analysis should remain unavailable for rendered fallback stats");
    Require(analysis.highlight.sensorStatus == AnalysisStageStatus::Unavailable,
        "RAW sensor clipping should remain unavailable without sensor-domain analysis");
    Require(analysis.highlight.displayStatus == AnalysisStageStatus::Complete,
        "RAW display clipping can be complete for rendered texture stats");
    Require(analysis.highlight.blocksPositiveRawExposure,
        "RAW fallback analysis should block positive RAW exposure recommendations");
    Require(std::abs(analysis.currentFrameStats.p99Luma - currentFrameStats.p99Luma) < 0.0001f,
        "RAW current-frame analysis should preserve render percentile luma");
}

Stack::RawAnalysis::RawImageAnalysis BuildAutoBaseTestAnalysis(
    float p01Ev,
    float p05Ev,
    float p50Ev,
    float p99Ev,
    float p999Ev,
    float dynamicRangeEv,
    float hdrPercent = 0.0f,
    float displayClipPercent = 0.0f,
    float anySensorClipPercent = 0.0f,
    float allSensorClipPercent = 0.0f,
    bool partialClipColorRisk = false,
    bool blocksPositiveRawExposure = false) {
    Stack::RawAnalysis::RawImageAnalysis analysis;
    analysis.valid = true;
    analysis.sourceKey = "raw/auto-base-test.dng";
    analysis.currentFrameStats.valid = true;
    analysis.currentFrameStats.status = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.currentFrameStats.p01Ev = p01Ev;
    analysis.currentFrameStats.p05Ev = p05Ev;
    analysis.currentFrameStats.p50Ev = p50Ev;
    analysis.currentFrameStats.p99Ev = p99Ev;
    analysis.currentFrameStats.p999Ev = p999Ev;
    analysis.currentFrameStats.p01Luma = std::exp2(p01Ev);
    analysis.currentFrameStats.p05Luma = std::exp2(p05Ev);
    analysis.currentFrameStats.p50Luma = std::exp2(p50Ev);
    analysis.currentFrameStats.p99Luma = std::exp2(p99Ev);
    analysis.currentFrameStats.p999Luma = std::exp2(p999Ev);
    analysis.currentFrameStats.dynamicRangeEv = dynamicRangeEv;
    analysis.currentFrameStats.validPixelPercent = 100.0f;
    analysis.technicalStats = analysis.currentFrameStats;
    analysis.technicalStats.status = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.valid = true;
    analysis.highlight.sensorStatus = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.displayStatus = Stack::RawAnalysis::AnalysisStageStatus::Complete;
    analysis.highlight.hdrPixelPercent = hdrPercent;
    analysis.highlight.displayClipPercent = displayClipPercent;
    analysis.highlight.anyChannelClipPercent = anySensorClipPercent;
    analysis.highlight.allChannelClipPercent = allSensorClipPercent;
    analysis.highlight.partialClipColorRisk = partialClipColorRisk;
    analysis.highlight.severeSensorClip = allSensorClipPercent > 0.005f;
    analysis.highlight.blocksPositiveRawExposure = blocksPositiveRawExposure;
    return analysis;
}

void TestRawAutoBaseViewTransformFit() {
    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0004.dng", "IMG_0004.dng");
    recipe.preToneExposureEv = 0.75f;

    const Stack::RawAnalysis::RawImageAnalysis normal =
        BuildAutoBaseTestAnalysis(-4.0f, -2.5f, -1.0f, 2.5f, 3.0f, 7.0f);
    const Stack::RawAutoBase::ViewFitDecision normalDecision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(normal, recipe);
    Require(normalDecision.canApply, "RAW Auto Base should apply when current-frame stats are valid");
    Require(std::abs(normalDecision.fit.middleGrey - 0.5f) < 0.001f,
        "RAW Auto Base should anchor middle grey near p50 luma");
    Require(normalDecision.fit.whiteEv >= 2.5f && normalDecision.fit.whiteEv <= 10.0f,
        "RAW Auto Base white EV should stay within the researched clamp");
    Require(normalDecision.fit.blackEv <= -4.0f && normalDecision.fit.blackEv >= -14.0f,
        "RAW Auto Base black EV should store a negative offset within the researched clamp");

    const Stack::RawAnalysis::RawImageAnalysis darkSky =
        BuildAutoBaseTestAnalysis(-10.0f, -8.0f, -5.0f, 2.0f, 4.0f, 14.0f, 2.0f, 1.0f);
    const Stack::RawAutoBase::ViewTransformFit darkSkyFit =
        Stack::RawAutoBase::FitViewTransformFromAnalysis(darkSky);
    Require(darkSkyFit.valid, "RAW Auto Base should fit dark foreground / bright sky stats");
    Require(darkSkyFit.shoulder > normalDecision.fit.shoulder,
        "RAW Auto Base should increase shoulder when highlight risk is higher");
    Require(std::abs(recipe.preToneExposureEv - 0.75f) < 0.001f,
        "RAW Auto Base fit computation should not mutate RAW exposure");

    const Stack::RawAnalysis::RawImageAnalysis lowRange =
        BuildAutoBaseTestAnalysis(-1.0f, -0.8f, -0.5f, 0.0f, 0.2f, 1.2f);
    const Stack::RawAutoBase::ViewTransformFit lowRangeFit =
        Stack::RawAutoBase::FitViewTransformFromAnalysis(lowRange);
    Require(lowRangeFit.valid, "RAW Auto Base should fit low dynamic range stats");
    Require(std::abs(lowRangeFit.blackEv + 4.0f) < 0.001f,
        "RAW Auto Base should avoid extreme black EV for low dynamic range images");
    Require(std::abs(lowRangeFit.whiteEv - 2.5f) < 0.001f,
        "RAW Auto Base should avoid extreme white EV for low dynamic range images");

    const Stack::RawAnalysis::RawImageAnalysis clipped =
        BuildAutoBaseTestAnalysis(-5.0f, -3.0f, -1.0f, 4.0f, 5.0f, 11.0f, 6.0f, 4.0f, 0.10f);
    const Stack::RawAutoBase::ViewTransformFit clippedFit =
        Stack::RawAutoBase::FitViewTransformFromAnalysis(clipped);
    Require(clippedFit.valid, "RAW Auto Base should fit clipped highlight stats");
    Require(clippedFit.whiteMarginEv > 0.35f && clippedFit.shoulder >= 0.55f,
        "RAW Auto Base should widen white margin and shoulder for highlight risk");

    Stack::RawAnalysis::RawImageAnalysis invalid;
    const Stack::RawAutoBase::ViewFitDecision invalidDecision =
        Stack::RawAutoBase::BuildAutoBaseViewFitDecision(invalid, recipe);
    Require(!invalidDecision.canApply, "RAW Auto Base should not apply without valid stats");

    Stack::RawAutoBase::ApplyViewTransformFitToRecipe(recipe, normalDecision.fit);
    Require(std::abs(recipe.preToneExposureEv - 0.75f) < 0.001f,
        "RAW Auto Base application should leave RAW Exposure unchanged");
    Require(std::abs(recipe.viewTransform.layerJson.value("middleGrey", 0.0f) - normalDecision.fit.middleGrey) < 0.001f,
        "RAW Auto Base application should write the fitted View Transform middle grey");
}

void TestRawAutoBaseRecommendations() {
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawAnalysis = Stack::RawAnalysis;
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0005.dng", "IMG_0005.dng");

    const RawAnalysis::RawImageAnalysis darkSafe =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -1.0f, -0.8f, 7.2f);
    const RawAutoBase::RawExposureRecommendation darkExposure =
        RawAutoBase::BuildRawExposureRecommendation(darkSafe, recipe);
    Require(darkExposure.valid && darkExposure.deltaEv > 0.0f,
        "RAW exposure recommendations should suggest a positive lift for dark safe images");

    const RawAnalysis::RawImageAnalysis clippedDark =
        BuildAutoBaseTestAnalysis(-8.0f, -7.0f, -5.0f, -0.5f, -0.2f, 8.0f, 0.0f, 0.0f, 0.08f, 0.0f, false, true);
    const RawAutoBase::RawExposureRecommendation clippedExposure =
        RawAutoBase::BuildRawExposureRecommendation(clippedDark, recipe);
    Require(clippedExposure.valid && clippedExposure.deltaEv > 0.0f,
        "RAW exposure recommendations should still report a blocked lift when dark content has highlight risk");
    Require(clippedExposure.blockedByHighlightRisk && !clippedExposure.autoApplyAllowed,
        "RAW exposure recommendations should not auto-apply positive exposure when highlight risk blocks it");

    const RawAnalysis::RawImageAnalysis hdrScene =
        BuildAutoBaseTestAnalysis(-10.0f, -8.0f, -6.0f, 3.5f, 4.0f, 14.0f, 6.0f, 3.0f, 0.0f, 0.0f, true, false);
    const RawAutoBase::RawExposureRecommendation hdrExposure =
        RawAutoBase::BuildRawExposureRecommendation(hdrScene, recipe);
    Require(hdrExposure.valid && hdrExposure.confidence < 0.85f && !hdrExposure.autoApplyAllowed,
        "Low-confidence HDR RAW exposure recommendations should remain suggestion-only");

    const RawAnalysis::RawImageAnalysis smallDelta =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -2.9f, -0.2f, 0.0f, 5.0f);
    const RawAutoBase::RawExposureRecommendation smallExposure =
        RawAutoBase::BuildRawExposureRecommendation(smallDelta, recipe);
    Require(smallExposure.valid && smallExposure.autoApplyAllowed,
        "Small high-confidence RAW exposure deltas should be marked safe for future opt-in automation");
    Require(std::abs(recipe.preToneExposureEv) < 0.001f,
        "Building RAW exposure recommendations should not mutate the visible recipe");

    RawAnalysis::PercentileStats wbStats;
    wbStats.valid = true;
    wbStats.p05Luma = 0.05f;
    wbStats.p95Luma = 0.95f;
    std::vector<RawAutoBase::WhiteBalanceSample> warmNeutralSamples(64);
    for (RawAutoBase::WhiteBalanceSample& sample : warmNeutralSamples) {
        sample.r = 1.4f;
        sample.g = 1.0f;
        sample.b = 0.7f;
        sample.luma = 0.45f;
    }
    const RawAutoBase::WhiteBalanceCandidateEvidence grayWorld =
        RawAutoBase::BuildWhiteBalanceCandidateEvidence(
            warmNeutralSamples,
            wbStats,
            RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld);
    Require(grayWorld.valid && grayWorld.neutralResidualAfter < grayWorld.neutralResidualBefore,
        "Gray World WB evidence should reduce neutral residual for a consistent neutral cast");

    RawAnalysis::RawImageAnalysis cameraWbAnalysis = smallDelta;
    cameraWbAnalysis.metadata.hasCameraWhiteBalance = true;
    cameraWbAnalysis.metadata.cameraWbR = 2.0f;
    cameraWbAnalysis.metadata.cameraWbG = 1.0f;
    cameraWbAnalysis.metadata.cameraWbB = 1.4f;
    const RawAutoBase::WhiteBalanceRecommendation cameraWb =
        RawAutoBase::BuildWhiteBalanceRecommendation(cameraWbAnalysis, recipe, &grayWorld);
    Require(cameraWb.valid && cameraWb.alternateCandidateAvailable && !cameraWb.autoApplyAllowed,
        "Alternate WB should remain suggestion-only when camera/as-shot WB exists");

    RawAnalysis::RawImageAnalysis noCameraWbAnalysis = smallDelta;
    const RawAutoBase::WhiteBalanceRecommendation noCameraWb =
        RawAutoBase::BuildWhiteBalanceRecommendation(noCameraWbAnalysis, recipe, &grayWorld);
    Require(noCameraWb.valid && noCameraWb.autoApplyAllowed,
        "Alternate WB can be marked auto-apply safe when camera WB is absent and neutral evidence is strong");

    std::vector<RawAutoBase::WhiteBalanceSample> fewEligibleSamples(100);
    for (std::size_t index = 0; index < fewEligibleSamples.size(); ++index) {
        fewEligibleSamples[index].r = index == 0 ? 1.2f : 1.0f;
        fewEligibleSamples[index].g = index == 0 ? 1.0f : 0.1f;
        fewEligibleSamples[index].b = index == 0 ? 0.8f : 0.1f;
        fewEligibleSamples[index].luma = 0.45f;
    }
    const RawAutoBase::WhiteBalanceCandidateEvidence fewEligible =
        RawAutoBase::BuildWhiteBalanceCandidateEvidence(
            fewEligibleSamples,
            wbStats,
            RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld);
    const RawAutoBase::WhiteBalanceRecommendation fewEligibleWb =
        RawAutoBase::BuildWhiteBalanceRecommendation(noCameraWbAnalysis, recipe, &fewEligible);
    Require(fewEligibleWb.confidence < 0.85f && !fewEligibleWb.autoApplyAllowed,
        "Few eligible neutral pixels should lower alternate WB confidence");

    std::vector<RawAutoBase::WhiteBalanceSample> extremeGainSamples(64);
    for (RawAutoBase::WhiteBalanceSample& sample : extremeGainSamples) {
        sample.r = 0.4f;
        sample.g = 1.0f;
        sample.b = 1.0f;
        sample.luma = 0.55f;
    }
    const RawAutoBase::WhiteBalanceCandidateEvidence extremeEvidence =
        RawAutoBase::BuildWhiteBalanceCandidateEvidence(
            extremeGainSamples,
            wbStats,
            RawAutoBase::WhiteBalanceRecommendation::Method::GrayWorld);
    const RawAutoBase::WhiteBalanceRecommendation extremeWb =
        RawAutoBase::BuildWhiteBalanceRecommendation(noCameraWbAnalysis, recipe, &extremeEvidence);
    Require(extremeEvidence.candidateGainsAreExtreme && extremeWb.confidence < 0.85f,
        "Extreme alternate WB gains should lower confidence");

    const RawAnalysis::RawImageAnalysis partialClip =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -1.0f, 2.0f, 3.0f, 8.0f, 0.0f, 0.0f, 0.12f, 0.01f, true, true);
    const RawAutoBase::HighlightRecommendation partialHighlight =
        RawAutoBase::BuildHighlightRecommendation(partialClip);
    Require(partialHighlight.recommendReconstruction && partialHighlight.recommendAchromaticClip,
        "Partial channel clipping should recommend reconstruction and achromatic highlight handling");

    const RawAnalysis::RawImageAnalysis allChannelClip =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -1.0f, 2.0f, 3.0f, 8.0f, 0.0f, 0.0f, 0.12f, 0.08f, false, true);
    const RawAutoBase::HighlightRecommendation allChannelHighlight =
        RawAutoBase::BuildHighlightRecommendation(allChannelClip);
    Require(allChannelClip.highlight.severeSensorClip && allChannelHighlight.recommendReconstruction,
        "All-channel clipping should be treated as severe sensor clipping");

    const RawAnalysis::RawImageAnalysis displayClipOnly =
        BuildAutoBaseTestAnalysis(-5.0f, -4.0f, -1.0f, 2.0f, 3.0f, 8.0f, 3.0f, 2.5f);
    const RawAutoBase::HighlightRecommendation displayHighlight =
        RawAutoBase::BuildHighlightRecommendation(displayClipOnly);
    Require(displayHighlight.recommendProtectiveViewShoulder && !displayHighlight.recommendReconstruction,
        "Display clipping should recommend view protection without claiming sensor reconstruction is needed");

    const RawAutoBase::RawExposureRecommendation blockedExposure =
        RawAutoBase::BuildRawExposureRecommendation(partialClip, recipe);
    Require(blockedExposure.blockedByHighlightRisk && !blockedExposure.autoApplyAllowed,
        "Highlight risk should block positive RAW exposure automation");
}

void TestRawAutoBaseNoiseDetailRecommendations() {
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawAnalysis = Stack::RawAnalysis;
    namespace RawRecipe = Stack::RawRecipe;

    RawRecipe::RawDevelopmentRecipe recipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0006.dng", "IMG_0006.dng");
    RawAnalysis::RawImageAnalysis baseAnalysis =
        BuildAutoBaseTestAnalysis(-7.0f, -5.0f, -2.5f, 1.0f, 2.0f, 8.0f);

    RawAnalysis::RawImageAnalysis lowIsoAnalysis = baseAnalysis;
    lowIsoAnalysis.metadata.iso = 100.0f;
    const RawAutoBase::NoiseDetailRecommendation lowIso =
        RawAutoBase::BuildNoiseDetailRecommendation(lowIsoAnalysis, recipe);
    Require(lowIso.valid && !lowIso.suggestChromaDenoise && !lowIso.suggestLumaDenoise,
        "Low ISO RAW noise/detail recommendations should not suggest denoise");
    Require(std::abs(lowIso.sharpeningScale - 1.0f) < 0.001f,
        "Low ISO RAW noise/detail recommendations should preserve sharpening scale");

    RawAnalysis::RawImageAnalysis mediumIsoAnalysis = baseAnalysis;
    mediumIsoAnalysis.metadata.iso = 1600.0f;
    const RawAutoBase::NoiseDetailRecommendation mediumIso =
        RawAutoBase::BuildNoiseDetailRecommendation(mediumIsoAnalysis, recipe);
    Require(mediumIso.effectiveNoiseScore > lowIso.effectiveNoiseScore,
        "Effective noise score should increase with ISO");
    Require(mediumIso.suggestChromaDenoise && !mediumIso.suggestLumaDenoise,
        "Moderate ISO RAW noise/detail recommendations should suggest mild chroma denoise only");

    RawRecipe::RawDevelopmentRecipe shadowLiftRecipe = recipe;
    shadowLiftRecipe.localRange.enabled = true;
    shadowLiftRecipe.localRange.strength = 1.0f;
    shadowLiftRecipe.localRange.points = {
        { -8.0f, 0.0f },
        { -4.0f, 1.0f },
        { 0.0f, 0.0f },
        { 6.0f, 0.0f }
    };
    const RawAutoBase::NoiseDetailRecommendation mediumIsoLifted =
        RawAutoBase::BuildNoiseDetailRecommendation(mediumIsoAnalysis, shadowLiftRecipe);
    Require(mediumIsoLifted.shadowLiftEv >= 0.9f &&
            mediumIsoLifted.effectiveNoiseScore > mediumIso.effectiveNoiseScore,
        "Effective noise score should increase when shadow lift is applied");
    Require(mediumIsoLifted.sharpeningScale < mediumIso.sharpeningScale,
        "High enough ISO plus shadow lift should reduce sharpening scale before increasing denoise");

    RawAnalysis::RawImageAnalysis highIsoAnalysis = baseAnalysis;
    highIsoAnalysis.metadata.iso = 6400.0f;
    const RawAutoBase::NoiseDetailRecommendation highIso =
        RawAutoBase::BuildNoiseDetailRecommendation(highIsoAnalysis, recipe);
    Require(highIso.suggestChromaDenoise && highIso.suggestLumaDenoise,
        "High ISO RAW noise/detail recommendations should suggest chroma and light luma denoise");
    Require(highIso.suggestReduceSharpening && highIso.sharpeningScale <= 0.75f,
        "High ISO RAW noise/detail recommendations should reduce sharpening scale");
    Require(!highIso.autoApplyMinimalChromaDenoise,
        "Noise/detail recommendations should not auto-apply when RAW workspace controls are not visible");

    const RawAutoBase::NoiseDetailRecommendation highIsoControlsVisible =
        RawAutoBase::BuildNoiseDetailRecommendation(highIsoAnalysis, recipe, nullptr, true);
    Require(highIsoControlsVisible.autoApplyMinimalChromaDenoise,
        "Minimal chroma denoise may only be marked auto-apply safe when visible controls exist");

    RawAnalysis::RawImageAnalysis missingIsoAnalysis = baseAnalysis;
    missingIsoAnalysis.metadata.iso = 0.0f;
    const RawAutoBase::NoiseDetailRecommendation missingIso =
        RawAutoBase::BuildNoiseDetailRecommendation(missingIsoAnalysis, recipe);
    Require(!missingIso.valid && missingIso.confidence < 0.25f,
        "Missing ISO should lower confidence instead of inventing a noise/detail recommendation");
    Require(!missingIso.suggestChromaDenoise && !missingIso.suggestLumaDenoise &&
            !missingIso.autoApplyMinimalChromaDenoise,
        "Missing ISO should avoid denoise suggestions and auto-apply");

    RawAutoBase::SuggestedLocalAdjustment shadowSuggestion;
    shadowSuggestion.valid = true;
    shadowSuggestion.kind = RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows;
    shadowSuggestion.deltaEv = 1.25f;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> suggestions = { shadowSuggestion };
    Require(RawAutoBase::EstimateShadowLiftEvForNoiseDetail(recipe, &suggestions) >= 1.20f,
        "Noise/detail shadow lift estimation should include suggested Local Range shadow lifts");
}

Stack::RawAutoBase::LocalSuggestionAnalysisImage MakeLocalSuggestionImage(
    int width,
    int height,
    float r,
    float g,
    float b) {
    Stack::RawAutoBase::LocalSuggestionAnalysisImage image;
    image.valid = true;
    image.sceneLinearBeforeLocalRange = true;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (Stack::RawAutoBase::LocalSuggestionPixel& pixel : image.pixels) {
        pixel.valid = true;
        pixel.r = r;
        pixel.g = g;
        pixel.b = b;
    }
    return image;
}

void FillLocalSuggestionRect(
    Stack::RawAutoBase::LocalSuggestionAnalysisImage& image,
    int x0,
    int y0,
    int x1,
    int y1,
    float r,
    float g,
    float b,
    bool textured = false) {
    x0 = std::clamp(x0, 0, image.width);
    y0 = std::clamp(y0, 0, image.height);
    x1 = std::clamp(x1, 0, image.width);
    y1 = std::clamp(y1, 0, image.height);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const float texture = textured
                ? (((x + y) & 1) == 0 ? 0.68f : 1.32f)
                : 1.0f;
            Stack::RawAutoBase::LocalSuggestionPixel& pixel =
                image.pixels[static_cast<std::size_t>(y * image.width + x)];
            pixel.valid = true;
            pixel.r = r * texture;
            pixel.g = g * texture;
            pixel.b = b * texture;
        }
    }
}

bool HasLocalSuggestionKind(
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions,
    Stack::RawAutoBase::SuggestedLocalAdjustmentKind kind) {
    return std::any_of(
        suggestions.begin(),
        suggestions.end(),
        [kind](const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
            return suggestion.kind == kind;
        });
}

const Stack::RawAutoBase::SuggestedLocalAdjustment* FindLocalSuggestionKind(
    const std::vector<Stack::RawAutoBase::SuggestedLocalAdjustment>& suggestions,
    Stack::RawAutoBase::SuggestedLocalAdjustmentKind kind) {
    const auto it = std::find_if(
        suggestions.begin(),
        suggestions.end(),
        [kind](const Stack::RawAutoBase::SuggestedLocalAdjustment& suggestion) {
            return suggestion.kind == kind;
        });
    return it == suggestions.end() ? nullptr : &*it;
}

void TestRawAutoBaseLocalRangeSuggestions() {
    namespace RawAutoBase = Stack::RawAutoBase;
    namespace RawRecipe = Stack::RawRecipe;

    const Stack::RawAnalysis::RawImageAnalysis baseAnalysis =
        BuildAutoBaseTestAnalysis(-7.0f, -5.0f, -2.5f, 1.0f, 2.5f, 9.0f);

    RawAutoBase::LocalSuggestionAnalysisImage skyImage =
        MakeLocalSuggestionImage(80, 60, 0.06f, 0.06f, 0.06f);
    FillLocalSuggestionRect(skyImage, 0, 0, 80, 22, 0.22f, 0.55f, 1.15f);
    RawAutoBase::LocalSuggestionComponentReport skyReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> skySuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, skyImage, &skyReport);
    Require(skyReport.valid && skyReport.skyAreaPercent > 8.0f,
        "Local suggestions should classify a connected bright blue top region as sky");
    Require(HasLocalSuggestionKind(skySuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::ProtectSky),
        "Local suggestions should include Protect sky for bright sky over dark foreground");

    RawAutoBase::LocalSuggestionAnalysisImage lowerBlueImage =
        MakeLocalSuggestionImage(80, 60, 0.18f, 0.18f, 0.18f);
    FillLocalSuggestionRect(lowerBlueImage, 8, 36, 72, 56, 0.12f, 0.35f, 1.10f);
    RawAutoBase::LocalSuggestionComponentReport lowerBlueReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> lowerBlueSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, lowerBlueImage, &lowerBlueReport);
    Require(lowerBlueReport.valid && lowerBlueReport.skyAreaPercent < 3.0f,
        "Local suggestions should not classify a lower-frame blue object as sky");
    Require(!HasLocalSuggestionKind(lowerBlueSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::ProtectSky),
        "Local suggestions should not offer Protect sky for a lower-frame blue object");

    RawAutoBase::LocalSuggestionAnalysisImage foliageImage =
        MakeLocalSuggestionImage(80, 60, 0.18f, 0.18f, 0.18f);
    FillLocalSuggestionRect(foliageImage, 0, 24, 80, 60, 0.10f, 0.42f, 0.08f, true);
    RawAutoBase::LocalSuggestionComponentReport foliageReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> foliageSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, foliageImage, &foliageReport);
    const RawAutoBase::SuggestedLocalAdjustment* foliageSuggestion =
        FindLocalSuggestionKind(foliageSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::BrightenFoliage);
    Require(foliageReport.valid && foliageReport.foliageAreaPercent > 3.0f,
        "Local suggestions should classify textured green/yellow-green regions as foliage");
    Require(foliageSuggestion != nullptr && foliageSuggestion->colorQualifierEnabled,
        "Brighten foliage must use a color qualifier");

    RawAutoBase::LocalSuggestionAnalysisImage flatGreenImage =
        MakeLocalSuggestionImage(80, 60, 0.10f, 0.42f, 0.08f);
    RawAutoBase::LocalSuggestionComponentReport flatGreenReport;
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> flatGreenSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, flatGreenImage, &flatGreenReport);
    Require(flatGreenReport.valid && flatGreenReport.foliageAreaPercent < 3.0f,
        "Local suggestions should reject flat green walls as foliage");
    Require(!HasLocalSuggestionKind(flatGreenSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::BrightenFoliage),
        "Local suggestions should not offer Brighten foliage for flat green walls");

    RawAutoBase::LocalSuggestionAnalysisImage backlitImage =
        MakeLocalSuggestionImage(80, 60, 0.08f, 0.08f, 0.08f);
    FillLocalSuggestionRect(backlitImage, 0, 0, 80, 26, 0.25f, 0.62f, 1.25f);
    FillLocalSuggestionRect(backlitImage, 26, 28, 54, 54, 0.035f, 0.035f, 0.035f);
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> backlitSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, backlitImage, nullptr);
    Require(HasLocalSuggestionKind(backlitSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::OpenBacklitSubject),
        "Local suggestions should include Open backlit subject for dark center under a bright sky");

    RawAutoBase::LocalSuggestionAnalysisImage shadowImage =
        MakeLocalSuggestionImage(80, 60, 0.40f, 0.40f, 0.40f);
    FillLocalSuggestionRect(shadowImage, 0, 0, 32, 60, 0.020f, 0.020f, 0.020f);
    const std::vector<RawAutoBase::SuggestedLocalAdjustment> shadowSuggestions =
        RawAutoBase::BuildSuggestedLocalAdjustments(baseAnalysis, shadowImage, nullptr);
    const RawAutoBase::SuggestedLocalAdjustment* shadowSuggestion =
        FindLocalSuggestionKind(shadowSuggestions, RawAutoBase::SuggestedLocalAdjustmentKind::OpenShadows);
    Require(shadowSuggestion != nullptr && !shadowSuggestion->colorQualifierEnabled,
        "Local suggestions should include a luminance-only Open shadows suggestion for large shadow masses");

    RawRecipe::RawDevelopmentRecipe foliageRecipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0005.dng", "IMG_0005.dng");
    Require(foliageSuggestion != nullptr, "foliage suggestion should exist before recipe apply test");
    Require(RawAutoBase::ApplySuggestedLocalAdjustment(*foliageSuggestion, foliageRecipe),
        "Applying a foliage suggestion should update the recipe");
    Require(foliageRecipe.localRange.enabled && foliageRecipe.localRange.colorMaskEnabled,
        "Applying a foliage suggestion should enable Local Range and color qualification");

    RawRecipe::RawDevelopmentRecipe shadowRecipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0005.dng", "IMG_0005.dng");
    shadowRecipe.localRange.colorMaskEnabled = true;
    shadowRecipe.localRange.colorMaskTargetR = 0.8f;
    shadowRecipe.localRange.colorMaskTargetG = 0.4f;
    shadowRecipe.localRange.colorMaskTargetB = 0.2f;
    Require(shadowSuggestion != nullptr, "shadow suggestion should exist before recipe apply test");
    Require(RawAutoBase::ApplySuggestedLocalAdjustment(*shadowSuggestion, shadowRecipe),
        "Applying a shadow suggestion should update the recipe");
    Require(shadowRecipe.localRange.colorMaskEnabled &&
            std::abs(shadowRecipe.localRange.colorMaskTargetR - 0.8f) < 0.001f,
        "Applying a non-color local suggestion should preserve an existing user color mask");

    RawRecipe::RawDevelopmentRecipe overlapRecipe =
        RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0005.dng", "IMG_0005.dng");
    overlapRecipe.localRange.points.push_back({ shadowSuggestion->targetEv + 0.10f, 0.25f });
    const std::size_t beforePointCount = overlapRecipe.localRange.points.size();
    Require(!RawAutoBase::ApplySuggestedLocalAdjustment(*shadowSuggestion, overlapRecipe),
        "Applying a local suggestion should refuse to overwrite a nearby user point");
    Require(overlapRecipe.localRange.points.size() == beforePointCount,
        "Failed local suggestion application should leave Local Range points unchanged");
}

Raw::RawImageData BuildMosaicRawProxyFixture(int width, int height) {
    Raw::RawImageData raw;
    raw.metadata.rawWidth = width;
    raw.metadata.rawHeight = height;
    raw.metadata.visibleWidth = width;
    raw.metadata.visibleHeight = height;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::MosaicBayer;
    raw.metadata.cfaPattern = Raw::CfaPattern::RGGB;
    raw.metadata.bitDepth = 16;
    raw.metadata.whiteLevel = 65535.0f;
    raw.rawBuffer.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < raw.rawBuffer.size(); ++i) {
        raw.rawBuffer[i] = static_cast<std::uint16_t>(i % 65535u);
    }
    return raw;
}

Raw::RawImageData BuildLinearRawProxyFixture(int width, int height, int channels) {
    Raw::RawImageData raw;
    raw.metadata.rawWidth = width;
    raw.metadata.rawHeight = height;
    raw.metadata.visibleWidth = width;
    raw.metadata.visibleHeight = height;
    raw.metadata.pixelLayout = Raw::RawPixelLayout::LinearRgb;
    raw.metadata.linearChannels = channels;
    raw.metadata.linearSampleFormat = Raw::RawSampleFormat::Float32;
    raw.metadata.whiteLevel = 1.0f;
    raw.linearFloatBuffer.resize(
        static_cast<std::size_t>(width) *
        static_cast<std::size_t>(height) *
        static_cast<std::size_t>(channels));
    for (std::size_t i = 0; i < raw.linearFloatBuffer.size(); ++i) {
        raw.linearFloatBuffer[i] = static_cast<float>((i % 1024u) / 1023.0f);
    }
    return raw;
}

void TestRawPreviewProxyUsesCappedRawData() {
    const Raw::RawImageData mosaic = BuildMosaicRawProxyFixture(800, 600);
    Raw::RawImageData mosaicPreview;
    Require(Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(mosaic, 200, mosaicPreview),
        "capped mosaic RAW preview should build a proxy buffer");
    const Stack::Renderer::RawPreviewProxy::Summary mosaicSummary =
        Stack::Renderer::RawPreviewProxy::Summarize(mosaicPreview, true);
    Require(mosaicSummary.usedProxy, "mosaic preview summary should report a proxy");
    Require(mosaicSummary.rawWidth == 200 && mosaicSummary.rawHeight == 150,
        "mosaic proxy should respect the requested preview cap");
    Require((mosaicSummary.rawWidth % 2) == 0 && (mosaicSummary.rawHeight % 2) == 0,
        "mosaic proxy dimensions should preserve Bayer parity");
    Require(mosaicSummary.rawSampleCount < mosaic.rawBuffer.size(),
        "mosaic proxy should use fewer RAW samples than the source");

    Raw::RawImageData uncappedPreview;
    Require(!Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(mosaic, 0, uncappedPreview),
        "uncapped RAW preview should not build or reuse a capped proxy");

    const std::string cap200Key =
        Stack::Renderer::RawPreviewProxy::BuildCacheKey("source", mosaic, 200);
    const std::string cap160Key =
        Stack::Renderer::RawPreviewProxy::BuildCacheKey("source", mosaic, 160);
    const std::string uncappedKey =
        Stack::Renderer::RawPreviewProxy::BuildCacheKey("source", mosaic, 0);
    Require(cap200Key != cap160Key,
        "RAW preview cache key should include the preview cap");
    Require(cap200Key != uncappedKey,
        "capped RAW preview cache key should not satisfy an uncapped render");

    Raw::RawImageData smallerMosaicPreview;
    Require(Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(mosaic, 160, smallerMosaicPreview),
        "alternate capped mosaic RAW preview should build a proxy buffer");
    Require(smallerMosaicPreview.metadata.rawWidth == 160 &&
            smallerMosaicPreview.metadata.rawHeight == 120,
        "alternate preview cap should produce distinct proxy dimensions");

    Raw::RawImageData gainMapMosaic = mosaic;
    gainMapMosaic.metadata.dngGainMapCount = 1;
    gainMapMosaic.metadata.dngGainMaps.push_back(Raw::DngGainMapOpcode {});
    Raw::RawImageData gainMapPreview;
    Require(!Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(gainMapMosaic, 200, gainMapPreview),
        "DNG gain-map RAW preview should not build a proxy that strips gain-map correction");

    const Raw::RawImageData linear = BuildLinearRawProxyFixture(800, 600, 3);
    Raw::RawImageData linearPreview;
    Require(Stack::Renderer::RawPreviewProxy::BuildPreviewRawData(linear, 200, linearPreview),
        "capped linear RAW preview should build a proxy buffer");
    const Stack::Renderer::RawPreviewProxy::Summary linearSummary =
        Stack::Renderer::RawPreviewProxy::Summarize(linearPreview, true);
    Require(linearSummary.rawWidth == 200 && linearSummary.rawHeight == 150,
        "linear proxy should respect the requested preview cap");
    Require(linearSummary.linearFloatSampleCount < linear.linearFloatBuffer.size(),
        "linear proxy should use fewer linear samples than the source");
    Require(linearSummary.linearFloatSampleCount ==
            static_cast<std::size_t>(200) * static_cast<std::size_t>(150) * static_cast<std::size_t>(3),
        "linear proxy sample count should match dimensions and channel count");
}

void TestCompactRawDevelopmentNodeGraphContract() {
    using namespace EditorNodeGraph;

    RawDevelopmentPayload payload;
    payload.recipe = Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0002.dng", "IMG_0002.dng");
    payload.recipe.source.relativePathKey = "card/IMG_0002.dng";
    payload.recipe.preToneExposureEv = -0.25f;
    payload.projectStatus = "Unknown";

    Graph graph;
    const int rawDevelopmentId = NodeId(graph.AddRawDevelopmentNode(payload, { 0.0f, 0.0f }));
    const int layerId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 260.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 520.0f, 0.0f }, true));
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(RawDecodePayload{}, { 260.0f, 180.0f }));

    const Node* rawDevelopmentNode = graph.FindNode(rawDevelopmentId);
    Require(rawDevelopmentNode != nullptr, "RAW Development node should be created");
    Require(rawDevelopmentNode->kind == NodeKind::RawDevelopment,
        "RAW Development node should use the compact node kind");
    Require(EditorNodeGraphDefinitions::DefaultInputSocket(*rawDevelopmentNode).empty(),
        "RAW Development node should not expose RAW internals as an input by default");
    Require(EditorNodeGraphDefinitions::DefaultOutputSocket(*rawDevelopmentNode) == kImageOutputSocketId,
        "RAW Development node should expose an image output");

    Require(graph.TryConnectSockets(rawDevelopmentId, kImageOutputSocketId, layerId, kImageInputSocketId),
        "RAW Development image output should connect to downstream graph editing");
    Require(graph.TryConnectSockets(layerId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "RAW Development downstream edit should connect to output");
    Require(graph.IsOutputConnected(),
        "RAW Development node should complete a normal image output chain");
    Require(!graph.CanConnectSockets(rawDevelopmentId, kImageOutputSocketId, rawDecodeId, kRawInputSocketId),
        "RAW Development node should not feed legacy RAW sockets");

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);
    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 1, {}, 0, 0, 0);
    const Node* loadedRawDevelopment = loaded.FindNode(rawDevelopmentId);
    Require(loadedRawDevelopment != nullptr,
        "RAW Development node should survive graph serialization");
    Require(loadedRawDevelopment->kind == NodeKind::RawDevelopment,
        "RAW Development node kind should survive graph serialization");
    Require(loadedRawDevelopment->rawDevelopment.recipe.source.relativePathKey == "card/IMG_0002.dng",
        "RAW Development recipe should survive graph serialization");
    Require(std::abs(loadedRawDevelopment->rawDevelopment.recipe.preToneExposureEv - -0.25f) < 0.001f,
        "RAW Development recipe exposure should survive graph serialization");
    Require(loadedRawDevelopment->rawDevelopment.projectStatus == "Unknown",
        "RAW Development neutral project status should survive graph serialization");
}

void TestLegacyRawDevelopNodeStillSerializesRoundTrip() {
    using namespace EditorNodeGraph;

    Graph graph;
    RawDevelopPayload payload;
    payload.settings.exposureStops = 1.25f;
    payload.settings.whiteBalanceMode = Raw::WhiteBalanceMode::Auto;
    payload.scenePrepEnabled = true;
    payload.integratedToneEnabled = true;
    const int rawDevelopId = NodeId(graph.AddRawDevelopNode(payload, { 180.0f, 120.0f }));

    const nlohmann::json serialized = SerializeGraphPayload(nlohmann::json::array(), graph);

    Graph loaded;
    DeserializeGraphPayload(serialized, loaded, 0, {}, 0, 0, 0);
    const Node* rawDevelopNode = loaded.FindNode(rawDevelopId);
    Require(rawDevelopNode != nullptr, "Legacy RawDevelop node should survive graph serialization");
    Require(rawDevelopNode->kind == NodeKind::RawDevelop,
        "Legacy RawDevelop node should not deserialize as compact RAW Development");
    Require(std::abs(rawDevelopNode->rawDevelop.settings.exposureStops - 1.25f) < 0.001f,
        "Legacy RawDevelop exposure should survive graph serialization");
    Require(rawDevelopNode->rawDevelop.settings.whiteBalanceMode == Raw::WhiteBalanceMode::Auto,
        "Legacy RawDevelop white balance should survive graph serialization");
}

void TestManagedRawSectionValidationAndSync() {
    using namespace EditorNodeGraph;

    Graph graph;
    RawSourcePayload sourcePayload;
    sourcePayload.sourcePath = "D:/shoot/card/IMG_0100.dng";
    sourcePayload.label = "IMG_0100.dng";
    sourcePayload.metadata.sourcePath = sourcePayload.sourcePath;
    const int rawSourceId = NodeId(graph.AddRawSourceNode(sourcePayload, { 0.0f, 0.0f }));

    RawDecodePayload decodePayload;
    decodePayload.settings.exposureStops = 1.25f;
    decodePayload.settings.whiteBalanceMode = Raw::WhiteBalanceMode::Manual;
    decodePayload.settings.manualWhiteBalance = { 2.0f, 1.0f, 1.5f };
    decodePayload.settings.rotationDegrees = 90;
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(decodePayload, { 260.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "managed RAW source should connect to RAW Decode");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "managed RAW Decode should connect to Tone Curve");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "managed Tone Curve should connect to View Transform");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "managed View Transform should connect downstream");

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(sourcePayload.sourcePath, sourcePayload.label);
    recipe.source.relativePathKey = "Day 1/IMG_0100.dng";
    recipe.source.fingerprint = "fingerprint-0100";
    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);

    Stack::RawWorkspace::ManagedRawValidationResult validation =
        Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe);
    Require(validation.valid, "baseline managed RAW section should validate");
    Require(std::abs(validation.recipe.preToneExposureEv - 1.25f) < 0.001f,
        "managed RAW Decode exposure should sync back to recipe");
    Require(validation.recipe.whiteBalance.mode == Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers,
        "manual RAW Decode white balance should sync back to custom multiplier recipe mode");
    Require(validation.recipe.whiteBalance.hasMultipliers,
        "manual RAW Decode white balance should preserve multipliers in recipe");
    Require(validation.recipe.cropRotation.rotationDegrees == 90,
        "managed RAW Decode rotation should sync back to recipe");

    const Stack::RawWorkspace::ManagedRawSection loadedSection =
        Stack::RawWorkspace::DeserializeManagedRawSection(
            Stack::RawWorkspace::SerializeManagedRawSection(section));
    validation = Stack::RawWorkspace::ValidateManagedRawSection(graph, loadedSection, recipe);
    Require(validation.valid, "serialized managed RAW section metadata should validate");

    const std::string originalSourcePath = graph.FindNode(rawSourceId)->rawSource.sourcePath;
    graph.FindNode(rawSourceId)->rawSource.sourcePath = "D:/shoot/card/IMG_9999.dng";
    validation = Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe);
    Require(!validation.valid,
        "managed RAW section should reject a raw source path that drifts away from the recipe");
    graph.FindNode(rawSourceId)->rawSource.sourcePath = originalSourcePath;

    Stack::RawWorkspace::ManagedRawSection adoptedSection;
    Stack::RawRecipe::RawDevelopmentRecipe adoptedRecipe;
    std::string reason;
    Require(Stack::RawWorkspace::TryBuildManagedRawSectionFromGraph(
                graph,
                recipe,
                adoptedSection,
                adoptedRecipe,
                &reason),
        "valid graph-first RAW chain should be adoptable as managed RAW");
    Require(adoptedSection.rawDecodeNodeId == rawDecodeId,
        "graph-first adoption should capture the RAW Decode node id");
}

void TestManagedRawSectionRejectsCustomGraphChanges() {
    using namespace EditorNodeGraph;

    Graph graph;
    const int rawSourceId = NodeId(graph.AddRawSourceNode(RawSourcePayload{}, { 0.0f, 0.0f }));
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(RawDecodePayload{}, { 260.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));
    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "managed RAW source should connect");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "managed RAW Decode should connect");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "managed tone should connect");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "managed view should connect");

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0101.dng", "IMG_0101.dng");
    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-invalid",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);
    Require(Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe).valid,
        "baseline managed RAW graph should validate before mutation");

    graph.RemoveLink(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId);
    const int mixId = NodeId(graph.AddMixNode({ 390.0f, 80.0f }));
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, mixId, kMixInputASocketId),
        "custom inserted mix should connect after RAW Decode");
    Require(graph.TryConnectSockets(mixId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "custom inserted mix should feed Tone Curve");
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe).valid,
        "custom node insertion inside managed RAW section should fail validation");
}

void TestManagedRawSectionRepairsMissingRequiredLinksOnly() {
    using namespace EditorNodeGraph;

    auto buildManagedGraph = [](
        Graph& graph,
        Stack::RawRecipe::RawDevelopmentRecipe& recipe,
        Stack::RawWorkspace::ManagedRawSection& section,
        int& rawSourceId,
        int& rawDecodeId,
        int& toneCurveId,
        int& viewTransformId) {
        RawSourcePayload sourcePayload;
        sourcePayload.sourcePath = "D:/shoot/card/IMG_0200.dng";
        sourcePayload.label = "IMG_0200.dng";
        sourcePayload.metadata.sourcePath = sourcePayload.sourcePath;
        rawSourceId = NodeId(graph.AddRawSourceNode(sourcePayload, { 0.0f, 0.0f }));

        RawDecodePayload decodePayload;
        decodePayload.settings.exposureStops = 0.5f;
        rawDecodeId = NodeId(graph.AddRawDecodeNode(decodePayload, { 260.0f, 0.0f }));
        toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
        viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
        const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));

        Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
            "repair test managed RAW source should connect");
        Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
            "repair test managed RAW decode should connect");
        Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
            "repair test managed RAW tone should connect");
        Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
            "repair test managed RAW view should connect downstream");

        recipe = Stack::RawRecipe::MakeDefaultRecipe(sourcePayload.sourcePath, sourcePayload.label);
        recipe.source.relativePathKey = "Day 1/IMG_0200.dng";
        recipe.source.fingerprint = "fingerprint-0200";
        section = Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-repair",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);
    };

    Graph brokenLinkGraph;
    Stack::RawRecipe::RawDevelopmentRecipe recipe;
    Stack::RawWorkspace::ManagedRawSection section;
    int rawSourceId = 0;
    int rawDecodeId = 0;
    int toneCurveId = 0;
    int viewTransformId = 0;
    buildManagedGraph(brokenLinkGraph, recipe, section, rawSourceId, rawDecodeId, toneCurveId, viewTransformId);
    Require(Stack::RawWorkspace::ValidateManagedRawSection(brokenLinkGraph, section, recipe).valid,
        "repair test baseline managed RAW graph should validate");

    Require(brokenLinkGraph.RemoveLink(
            rawDecodeId,
            kImageOutputSocketId,
            toneCurveId,
            kImageInputSocketId),
        "repair test should remove the managed decode-to-tone link");
    const Stack::RawWorkspace::ManagedRawValidationResult brokenValidation =
        Stack::RawWorkspace::ValidateManagedRawSection(brokenLinkGraph, section, recipe);
    Require(!brokenValidation.valid && brokenValidation.repairable,
        "missing required managed link should be marked repairable");
    const Stack::RawWorkspace::ManagedRawRepairResult repaired =
        Stack::RawWorkspace::RepairManagedRawSectionGraph(brokenLinkGraph, section, recipe);
    Require(repaired.repaired && repaired.changed,
        "repair should reconnect a missing required managed RAW link");
    Require(Stack::RawWorkspace::ValidateManagedRawSection(brokenLinkGraph, section, recipe).valid,
        "repaired managed RAW graph should validate");
    Require(brokenLinkGraph.HasLink(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "repair should restore the required decode-to-tone link");

    Graph customGraph;
    Stack::RawRecipe::RawDevelopmentRecipe customRecipe;
    Stack::RawWorkspace::ManagedRawSection customSection;
    int customRawSourceId = 0;
    int customRawDecodeId = 0;
    int customToneCurveId = 0;
    int customViewTransformId = 0;
    buildManagedGraph(
        customGraph,
        customRecipe,
        customSection,
        customRawSourceId,
        customRawDecodeId,
        customToneCurveId,
        customViewTransformId);
    Require(customGraph.RemoveLink(
            customRawDecodeId,
            kImageOutputSocketId,
            customToneCurveId,
            kImageInputSocketId),
        "repair refusal test should remove the managed decode-to-tone link");
    const int mixId = NodeId(customGraph.AddMixNode({ 390.0f, 80.0f }));
    Require(customGraph.TryConnectSockets(customRawDecodeId, kImageOutputSocketId, mixId, kMixInputASocketId),
        "repair refusal test custom mix should connect after RAW Decode");
    Require(customGraph.TryConnectSockets(mixId, kImageOutputSocketId, customToneCurveId, kImageInputSocketId),
        "repair refusal test custom mix should feed Tone Curve");
    const Stack::RawWorkspace::ManagedRawRepairResult refused =
        Stack::RawWorkspace::RepairManagedRawSectionGraph(customGraph, customSection, customRecipe);
    Require(!refused.repaired && !refused.changed,
        "repair should refuse custom internal managed RAW graph changes");
    Require(customGraph.HasLink(customRawDecodeId, kImageOutputSocketId, mixId, kMixInputASocketId) &&
            customGraph.HasLink(mixId, kImageOutputSocketId, customToneCurveId, kImageInputSocketId),
        "repair should not remove or bypass custom internal graph edits");
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(customGraph, customSection, customRecipe).valid,
        "custom internal graph should remain invalid after refused repair");
}

void TestManagedRawSectionMutationWarnings() {
    using namespace EditorNodeGraph;

    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-warnings",
            "project-local-test",
            "Day 1/IMG_0201.dng",
            "fingerprint-0201",
            -1,
            1,
            2,
            3,
            4);

    Require(!Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                section,
                1,
                kRawOutputSocketId,
                2,
                kRawInputSocketId).requiresConfirmation,
        "required managed RAW source-to-decode connection should not warn");
    Require(Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                section,
                2,
                kImageOutputSocketId,
                99,
                kImageInputSocketId).requiresConfirmation,
        "branching from an internal managed RAW stage should warn before mutation");
    Require(Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                section,
                99,
                kImageOutputSocketId,
                3,
                kImageInputSocketId).requiresConfirmation,
        "replacing an internal managed RAW stage input should warn before mutation");
    Require(!Stack::RawWorkspace::BuildManagedRawGraphConnectionWarning(
                section,
                4,
                kImageOutputSocketId,
                99,
                kImageInputSocketId).requiresConfirmation,
        "connecting downstream from managed View Transform output should remain allowed without warning");

    Require(Stack::RawWorkspace::BuildManagedRawGraphLinkRemovalWarning(
                section,
                2,
                kImageOutputSocketId,
                3,
                kImageInputSocketId).requiresConfirmation,
        "removing a required managed RAW link should warn before mutation");
    Require(!Stack::RawWorkspace::BuildManagedRawGraphLinkRemovalWarning(
                section,
                4,
                kImageOutputSocketId,
                99,
                kImageInputSocketId).requiresConfirmation,
        "removing a downstream link after the managed View Transform should not warn");

    Require(Stack::RawWorkspace::BuildManagedRawGraphNodeRemovalWarning(section, 2).requiresConfirmation,
        "removing a managed RAW chain node should warn before mutation");
    Require(!Stack::RawWorkspace::BuildManagedRawGraphNodeRemovalWarning(section, 99).requiresConfirmation,
        "removing a non-managed node should not warn");
}

void TestManagedRawSectionRejectsFlexibleReorderingInV1() {
    using namespace EditorNodeGraph;

    Graph graph;
    RawSourcePayload sourcePayload;
    sourcePayload.sourcePath = "D:/shoot/card/IMG_0202.dng";
    sourcePayload.label = "IMG_0202.dng";
    sourcePayload.metadata.sourcePath = sourcePayload.sourcePath;
    const int rawSourceId = NodeId(graph.AddRawSourceNode(sourcePayload, { 0.0f, 0.0f }));
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(RawDecodePayload{}, { 260.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));

    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "V1 reordering test managed RAW source should connect");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "V1 reordering test managed RAW decode should connect");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "V1 reordering test managed RAW tone should connect");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "V1 reordering test managed RAW view should connect downstream");

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe(sourcePayload.sourcePath, sourcePayload.label);
    recipe.source.relativePathKey = "Day 1/IMG_0202.dng";
    recipe.source.fingerprint = "fingerprint-0202";
    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-reorder",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);
    Require(Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe).valid,
        "V1 reordering test baseline managed RAW graph should validate");

    Stack::RawWorkspace::ManagedRawSection reorderedMetadata = section;
    reorderedMetadata.orderedNodeIds = {
        rawSourceId,
        rawDecodeId,
        viewTransformId,
        toneCurveId
    };
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(graph, reorderedMetadata, recipe).valid,
        "V1 should reject flexible-stage metadata reordering until a round-trip contract exists");

    Graph reorderedGraph = graph;
    Require(reorderedGraph.RemoveLink(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "V1 reordering test should remove decode-to-tone");
    Require(reorderedGraph.RemoveLink(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "V1 reordering test should remove tone-to-view");
    Require(reorderedGraph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "V1 reordering test should connect decode directly to View Transform");
    Require(reorderedGraph.TryConnectSockets(viewTransformId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "V1 reordering test should connect View Transform into Tone Curve");
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(reorderedGraph, section, recipe).valid,
        "V1 should reject graph stage reordering instead of treating it as RAW-tab editable");
}

void TestManagedRawSectionBlocksUnsupportedRecipeAndDecodeFields() {
    using namespace EditorNodeGraph;

    Stack::RawRecipe::RawDevelopmentRecipe recipe =
        Stack::RawRecipe::MakeDefaultRecipe("D:/shoot/card/IMG_0102.dng", "IMG_0102.dng");
    std::string reason;
    Require(Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(recipe, &reason),
        "default RAW recipe should be representable as managed graph");

    Stack::RawRecipe::RawDevelopmentRecipe customTone = recipe;
    customTone.finishTone.layerJson = Stack::RawRecipe::DefaultFinishToneJson();
    customTone.finishTone.layerJson["points"] = nlohmann::json::array({
        { { "x", 0.0f }, { "y", 0.0f }, { "shape", 1 } },
        { { "x", 0.5f }, { "y", 0.65f }, { "shape", 1 } },
        { { "x", 1.0f }, { "y", 1.0f }, { "shape", 1 } }
    });
    customTone.viewTransform.layerJson = Stack::RawRecipe::DefaultViewTransformJson();
    customTone.viewTransform.layerJson["contrast"] = 1.20f;
    Require(Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(customTone, &reason),
        "custom finish tone and view transform should decompose into managed graph layers");

    Stack::RawRecipe::RawDevelopmentRecipe cropped = recipe;
    cropped.cropRotation.cropEnabled = true;
    Require(!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(cropped, &reason),
        "crop should block managed decomposition until a crop node mapping exists");

    Stack::RawRecipe::RawDevelopmentRecipe localExposure = recipe;
    localExposure.localExposure.enabled = true;
    localExposure.localExposure.shadowLiftEv = 1.0f;
    Require(!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(localExposure, &reason),
        "local exposure should block managed decomposition until a managed stage mapping exists");

    Stack::RawRecipe::RawDevelopmentRecipe localRange = recipe;
    localRange.localRange.enabled = true;
    localRange.localRange.points[1].deltaEv = 1.0f;
    Require(!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(localRange, &reason),
        "local range should block managed decomposition until a managed stage mapping exists");

    Stack::RawRecipe::RawDevelopmentRecipe temperature = recipe;
    temperature.whiteBalance.mode = Stack::RawRecipe::WhiteBalanceMode::CustomMultipliers;
    temperature.whiteBalance.hasTemperatureKelvin = true;
    temperature.whiteBalance.temperatureKelvin = 5500.0f;
    temperature.whiteBalance.hasMultipliers = true;
    Require(!Stack::RawWorkspace::IsRecipeRepresentableAsManagedGraph(temperature, &reason),
        "temperature/tint white balance should block managed decomposition");

    Graph graph;
    const int rawSourceId = NodeId(graph.AddRawSourceNode(RawSourcePayload{}, { 0.0f, 0.0f }));
    RawDecodePayload decodePayload;
    decodePayload.settings.highlightMode = Raw::HighlightReconstructionMode::Luminance;
    const int rawDecodeId = NodeId(graph.AddRawDecodeNode(decodePayload, { 260.0f, 0.0f }));
    const int toneCurveId = NodeId(graph.AddLayerNode(LayerType::ToneCurve, 0, { 520.0f, 0.0f }));
    const int viewTransformId = NodeId(graph.AddLayerNode(LayerType::ViewTransform, 1, { 780.0f, 0.0f }));
    const int outputId = NodeId(graph.AddOutputNode({ 1040.0f, 0.0f }, true));
    Require(graph.TryConnectSockets(rawSourceId, kRawOutputSocketId, rawDecodeId, kRawInputSocketId),
        "managed RAW source should connect for unsupported settings test");
    Require(graph.TryConnectSockets(rawDecodeId, kImageOutputSocketId, toneCurveId, kImageInputSocketId),
        "managed RAW Decode should connect for unsupported settings test");
    Require(graph.TryConnectSockets(toneCurveId, kImageOutputSocketId, viewTransformId, kImageInputSocketId),
        "managed tone should connect for unsupported settings test");
    Require(graph.TryConnectSockets(viewTransformId, kImageOutputSocketId, outputId, kImageInputSocketId),
        "managed view should connect for unsupported settings test");

    const Stack::RawWorkspace::ManagedRawSection section =
        Stack::RawWorkspace::BuildManagedRawSection(
            "managed-raw:test-unsupported-settings",
            "project-local-test",
            recipe.source.relativePathKey,
            recipe.source.fingerprint,
            -1,
            rawSourceId,
            rawDecodeId,
            toneCurveId,
            viewTransformId);
    Require(!Stack::RawWorkspace::ValidateManagedRawSection(graph, section, recipe).valid,
        "unsupported RAW Decode settings should fail managed validation");
}

} // namespace

namespace LayerRegistry {

std::shared_ptr<LayerBase> CreateLayer(LayerType type) {
    (void)type;
    return nullptr;
}

std::shared_ptr<LayerBase> CreateLayerFromTypeId(const std::string& typeId) {
    (void)typeId;
    return nullptr;
}

const LayerDescriptor* GetDescriptor(LayerType type) {
    for (const LayerDescriptor& descriptor : TestLayerDescriptors()) {
        if (descriptor.type == type) {
            return &descriptor;
        }
    }
    return nullptr;
}

const LayerDescriptor* FindDescriptorByTypeId(const std::string& typeId) {
    for (const LayerDescriptor& descriptor : TestLayerDescriptors()) {
        if (typeId == descriptor.typeId) {
            return &descriptor;
        }
    }
    return nullptr;
}

const std::vector<LayerDescriptor>& GetAllDescriptors() {
    return TestLayerDescriptors();
}

std::map<std::string, std::vector<const LayerDescriptor*>> GetDescriptorsByCategory() {
    std::map<std::string, std::vector<const LayerDescriptor*>> byCategory;
    for (const LayerDescriptor& descriptor : TestLayerDescriptors()) {
        byCategory[descriptor.categoryName].push_back(&descriptor);
    }
    return byCategory;
}

std::string GetDisplayNameFromTypeId(const std::string& typeId) {
    const LayerDescriptor* descriptor = FindDescriptorByTypeId(typeId);
    return descriptor ? descriptor->displayName : std::string();
}

std::string GetLibraryDisplayNameFromTypeId(const std::string& typeId) {
    const LayerDescriptor* descriptor = FindDescriptorByTypeId(typeId);
    return descriptor ? descriptor->libraryDisplayName : std::string();
}

const char* LifecycleStatusLabel(LayerLifecycleStatus status) {
    switch (status) {
        case LayerLifecycleStatus::Stable: return "Stable";
        case LayerLifecycleStatus::NeedsFix: return "Needs Fix";
        case LayerLifecycleStatus::Experimental: return "Experimental";
        case LayerLifecycleStatus::Deprecated: return "Deprecated";
        case LayerLifecycleStatus::Hidden: return "Hidden";
    }
    return "Unknown";
}

const char* ChannelPolicyLabel(LayerChannelPolicy policy) {
    switch (policy) {
        case LayerChannelPolicy::ChannelSafe: return "Channel Safe";
        case LayerChannelPolicy::ChannelUsefulWithWarning: return "Channel Useful With Warning";
        case LayerChannelPolicy::FullImagePreferred: return "Full Image Preferred";
        case LayerChannelPolicy::FullImageOnly: return "Full Image Only";
        case LayerChannelPolicy::ReworkBeforeExpose: return "Rework Before Expose";
    }
    return "Unknown";
}

bool ShouldShowInNodeBrowser(const LayerDescriptor& descriptor) {
    return descriptor.visibleInNodeBrowser &&
        descriptor.lifecycleStatus != LayerLifecycleStatus::Hidden &&
        descriptor.lifecycleStatus != LayerLifecycleStatus::Deprecated;
}

bool ValidateRegistry(std::vector<std::string>* errors) {
    if (errors) {
        errors->clear();
    }
    return true;
}

} // namespace LayerRegistry

int main() {
    TestScalarMaskCanUseLayerMath();
    TestFullImageStillCannotTargetScalarInput();
    TestOutputChannelNormalization();
    TestScalarCyclesAreRejected();
    TestCustomMaskConnections();
    TestCustomMaskThroughMaskCombineExclude();
    TestManualRawBaselineChainShape();
    TestRawWorkspaceFolderCatalogFoundation();
    TestRawWorkspaceThumbnailPipelineFoundation();
    TestRawWorkspaceLoadingCancellationModel();
    TestRawWorkspaceJsonReadersTolerateNullOptionalFields();
    TestRawWorkspaceGalleryPresentation();
    TestRawWorkspacePanelStateModel();
    TestRawWorkspaceProjectLifecycleModel();
    TestRawWorkspaceProjectReloadPreservesOwnershipModes();
    TestRawDevelopmentRecipeDefaultsAndRoundTrip();
    TestRawImageAnalysisPercentilesAndFallbackGuards();
    TestRawAutoBaseViewTransformFit();
    TestRawAutoBaseRecommendations();
    TestRawAutoBaseNoiseDetailRecommendations();
    TestRawAutoBaseLocalRangeSuggestions();
    TestRawPreviewProxyUsesCappedRawData();
    TestCompactRawDevelopmentNodeGraphContract();
    TestLegacyRawDevelopNodeStillSerializesRoundTrip();
    TestManagedRawSectionValidationAndSync();
    TestManagedRawSectionRejectsCustomGraphChanges();
    TestManagedRawSectionRepairsMissingRequiredLinksOnly();
    TestManagedRawSectionMutationWarnings();
    TestManagedRawSectionRejectsFlexibleReorderingInV1();
    TestManagedRawSectionBlocksUnsupportedRecipeAndDecodeFields();
    TestScalarThroughDataMathToPreviewAndScalarTargets();
    TestImageThroughDataMathToOutput();
    TestAverageNodeInputRules();
    TestImageAndScalarThroughDataMathStaysImage();
    TestFullImageDataMathRejectedByScalarOnlyInputs();
    TestLegacyMaskCombineRemainsScalar();
    TestLutNodeConnectionsAndScalarPropagation();
    TestViewportTilePlannerCoverage();
    TestViewportTilingModeDecisions();
    TestViewportTileSafeGraphClassification();
    TestLutImporterCubeVariants();
    TestLutCreatorRoundTripSidecar();
    TestMfsrValidationRejectsEmptyAndMissingReference();
    TestMfsrValidationRejectsMixedAndUnknownInputs();
    TestMfsrValidationAllowsSingleFamilyWithReference();
    TestMfsrCacheKeyFingerprintsReactToInputsAndSettings();
    TestMfsrNodeShellSocketsAndConnections();
    TestMfsrNodeShellRejectsScalarAndMixedFamilies();
    TestMfsrNodeShellSerializesRoundTrip();
    TestCompositeNodeSerializesRoundTrip();
    TestHdrMergeDeghostModeMediumRoundTrip();

    std::cout << "Stack graph behavior tests passed.\n";
    return 0;
}
