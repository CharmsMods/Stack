#include "Project/RawLayerStack.h"

#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/Timeline/TimelinePersistence.h"

#include <stdexcept>

namespace Stack::Project {
namespace {
using Json = nlohmann::json;
// Project manifests use text JSON. Preserve binary fields in nested mask
// graphs explicitly, including image and painted-mask payloads.
void EncodeGraphBytes(Json& value) {
    if (value.is_binary()) {
        const auto& bytes = value.get_binary();
        value = {{"$rawLayerBytes", std::vector<unsigned char>(bytes.begin(), bytes.end())}};
    } else if (value.is_structured()) {
        for (auto& child : value) EncodeGraphBytes(child);
    }
}
void DecodeGraphBytes(Json& value) {
    if (value.is_object() && value.contains("$rawLayerBytes")) {
        const auto& encoded = value.at("$rawLayerBytes");
        if (value.size() != 1 || !encoded.is_array())
            throw std::runtime_error("Invalid mask graph byte payload.");
        std::vector<unsigned char> bytes;
        bytes.reserve(encoded.size());
        for (const auto& byte : encoded) {
            if (!byte.is_number_integer() || byte.get<std::int64_t>() < 0 || byte.get<std::uint64_t>() > 255)
                throw std::runtime_error("Invalid mask graph byte value.");
            bytes.push_back(byte.get<unsigned char>());
        }
        value = Json::binary(std::move(bytes));
    } else if (value.is_structured()) {
        for (auto& child : value) DecodeGraphBytes(child);
    }
}
Json ReferenceJson(const std::optional<RawMaskReference>& reference) {
    if (!reference) return nullptr;
    return {{"layer", reference->layerId}, {"output", reference->outputId}};
}
std::optional<RawMaskReference> ReadReference(const Json& value) {
    if (value.is_null()) return std::nullopt;
    return RawMaskReference{value.at("layer").get<std::string>(), value.at("output").get<std::string>()};
}

Json EndpointJson(const std::optional<GraphModel::Endpoint>& endpoint) {
    if (!endpoint) return nullptr;
    return {{"graph",endpoint->graphId},{"node",endpoint->nodeUuid},{"port",endpoint->portId}};
}
std::optional<GraphModel::Endpoint> ReadEndpoint(const Json& value) {
    if (value.is_null()) return std::nullopt;
    return GraphModel::Endpoint{value.at("graph").get<std::string>(),
        value.at("node").get<std::string>(),value.at("port").get<std::string>()};
}

} // namespace

namespace {
nlohmann::json SerializeLayerStack(const RawLayerStackState& state, bool includeAssets, bool includeMaskRaster) {
    const auto serializeLayer = [includeMaskRaster](const RawAdjustmentLayer& layer) {
        auto graph = EditorNodeGraph::SerializeGraphPayload(nlohmann::json::array(), layer.graph, includeMaskRaster);
        EncodeGraphBytes(graph);
        graph["nodeGraph"].erase("selectedNodeId");
        Timeline::TimelineDocumentState timeline;
        timeline.animation = layer.animation;
        timeline.durationFrames = std::max(120, Timeline::FindLastTimelineKeyframeFrame(layer.animation) + 1);
        Json outputs = Json::array();
        for (const auto& output : layer.maskOutputs)
            outputs.push_back({{"id", output.id}, {"name", output.name}, {"nodeUuid", output.nodeUuid}});
        Json result{{"id", layer.id}, {"name", layer.name}, {"enabled", layer.enabled},
            {"opacity", layer.opacity}, {"layerMask", ReferenceJson(layer.layerMask)},
            {"outputs", std::move(outputs)}, {"graph", std::move(graph)}, {"animation", Timeline::SerializeTimelineDocument(timeline)},
            {"imageInput",EndpointJson(layer.imageInput)}};
        if (layer.canvasOrigin) result["canvasOrigin"] = {layer.canvasOrigin->x,layer.canvasOrigin->y};
        return result;
    };
    Json layers = Json::array();
    for (const auto& layer : state.layers) layers.push_back(serializeLayer(layer));
    Json result = {{"version", kRawLayerStackSchemaVersion}, {"nextId", state.nextId}, {"background", serializeLayer(state.background)}, {"layers", std::move(layers)},
        {"explicitRouting",state.explicitRouting},{"imageOutput",EndpointJson(state.imageOutput)}};
    if (includeAssets) result["createdAssets"] = Assets::SerializeLibrary(state.assets);
    return result;
}
} // namespace

nlohmann::json SerializeRawLayerStack(const RawLayerStackState& state, bool includeAssets) {
    return SerializeLayerStack(state, includeAssets, true);
}

bool SameRawLayerStackState(const RawLayerStackState& a, const RawLayerStackState& b) {
    if (a.layers.size() != b.layers.size() || !Assets::SameLibrary(a.assets,b.assets)) return false;
    const auto sameRasters = [](const RawAdjustmentLayer& left, const RawAdjustmentLayer& right) {
        const auto& ln = left.graph.GetNodes(); const auto& rn = right.graph.GetNodes();
        if (ln.size() != rn.size()) return false;
        for (std::size_t i=0;i<ln.size();++i)
            if (ln[i].kind == EditorNodeGraph::NodeKind::CustomMask &&
                rn[i].kind == EditorNodeGraph::NodeKind::CustomMask &&
                !(ln[i].customMask.rasterLayer == rn[i].customMask.rasterLayer)) return false;
        return true;
    };
    if (!sameRasters(a.background,b.background)) return false;
    for (std::size_t i=0;i<a.layers.size();++i) if (!sameRasters(a.layers[i],b.layers[i])) return false;
    // Comparing unchanged masks must not turn a large binary raster into a JSON
    // integer per byte. The exact pixel comparison above is usually pointer-only.
    return SerializeLayerStack(a,false,false) == SerializeLayerStack(b,false,false);
}

bool DeserializeRawLayerStack(const nlohmann::json& value, RawLayerStackState& result, std::string& error) {
    error.clear();
    try {
        const int version = value.at("version").get<int>();
        if (version != kRawLayerStackSchemaVersion) throw std::runtime_error("Unsupported layer graph document version.");
        const auto readLayer = [](const Json& item) {
            RawAdjustmentLayer layer;
            layer.id = item.at("id").get<std::string>(); layer.name = item.at("name").get<std::string>();
            if (item.contains("imageInput")) layer.imageInput = ReadEndpoint(item.at("imageInput"));
            if (item.contains("canvasOrigin")) {
                const auto& origin = item.at("canvasOrigin");
                if (!origin.is_array() || origin.size() != 2) throw std::runtime_error("Invalid layer canvas origin.");
                layer.canvasOrigin = EditorNodeGraph::Vec2{origin.at(0).get<float>(),origin.at(1).get<float>()};
            }
            layer.enabled = item.at("enabled").get<bool>(); layer.opacity = item.at("opacity").get<float>();
            layer.layerMask = ReadReference(item.at("layerMask"));
            if (item.at("animation").at("schemaVersion") != 2) throw std::runtime_error("Unsupported layer animation version.");
            layer.animation = Timeline::DeserializeTimelineDocument(item.at("animation")).animation;
            auto graph = item.at("graph");
            DecodeGraphBytes(graph);
            if (!EditorNodeGraph::IsCurrentGraphPayload(graph)) throw std::runtime_error("Unsupported layer graph version.");
            EditorNodeGraph::DeserializeGraphPayload(graph, layer.graph,
                static_cast<int>(EditorNodeGraph::ExtractLayerArray(graph).size()));
            const auto& saved = graph.at("nodeGraph");
            if (saved.at("nodes").size() != layer.graph.GetNodes().size() || saved.at("links").size() != layer.graph.GetLinks().size())
                throw std::runtime_error("A layer graph could not be restored without dropping authored data.");
            for (const auto& link : saved.at("links"))
                if (!layer.graph.HasLink(link.at("fromNodeId").get<int>(), link.at("fromSocket").get<std::string>(),
                        link.at("toNodeId").get<int>(), link.at("toSocket").get<std::string>()))
                    throw std::runtime_error("A saved connection could not be restored.");
            for (const auto& savedNode : saved.at("nodes")) {
                if (savedNode.value("kind", std::string()) != "Image" || savedNode.value("width", 0) <= 0) continue;
                const auto* node = layer.graph.FindNode(savedNode.at("id").get<int>());
                if (!node || node->image.width != savedNode.at("width").get<int>() ||
                    node->image.height != savedNode.at("height").get<int>() || node->image.pixels.empty())
                    throw std::runtime_error("A saved graph image could not be restored.");
            }
            for (const auto& output : item.at("outputs")) {
                RawMaskOutput publication{output.at("id").get<std::string>(), output.at("name").get<std::string>(), 0,
                    output.at("nodeUuid").get<std::string>()};
                if (const auto* producer = FindRawPublishedNode(layer, publication)) publication.nodeId = producer->id;
                layer.maskOutputs.push_back(std::move(publication));
            }
            return layer;
        };
        RawLayerStackState candidate;
        candidate.explicitRouting = value.value("explicitRouting",false);
        if (candidate.explicitRouting && value.contains("imageOutput")) candidate.imageOutput = ReadEndpoint(value.at("imageOutput"));
        candidate.nextId = value.at("nextId").get<std::uint64_t>();
        if (value.contains("createdAssets") && !Assets::DeserializeLibrary(value.at("createdAssets"), candidate.assets, error)) return false;
        candidate.background = readLayer(value.at("background"));
        if (!value.at("layers").is_array()) throw std::runtime_error("RAW layers must be an array.");
        for (const auto& item : value.at("layers")) candidate.layers.push_back(readLayer(item));
        if (!ValidateRawLayerStack(candidate, error)) return false;
        result = std::move(candidate);
        return true;
    } catch (const std::exception& exception) {
        error = std::string("Cannot read RAW layer graphs: ") + exception.what();
        return false;
    }
}

bool ReadRawLayersFromPipeline(const nlohmann::json& pipeline, RawLayerStackState& result, std::string& error, bool required) {
    if (!pipeline.is_object()) { error = "Invalid project pipeline document."; return false; }
    const auto field = pipeline.find("rawLayerStack");
    if (field == pipeline.end()) {
        if (required) { error = "This RAW project has no unified layer graph document."; return false; }
        result = {}; error.clear(); return true;
    }
    RawLayerStackState candidate;
    if (!DeserializeRawLayerStack(*field,candidate,error)) return false;
    if (required && (!pipeline.contains("nodeGraph") || pipeline.value("rawLayerSourceNodeUuid",std::string{}).empty())) {
        error = "This RAW project has no stable layer source binding.";
        return false;
    }
    if (pipeline.contains("nodeGraph")) {
        try {
            if (!EditorNodeGraph::IsCurrentGraphPayload(pipeline)) throw std::runtime_error("Unsupported project graph version.");
            const auto settings = EditorNodeGraph::ExtractLayerArray(pipeline);
            if (!settings.is_array()) throw std::runtime_error("Invalid project operation settings.");
            EditorNodeGraph::Graph composition;
            EditorNodeGraph::DeserializeGraphPayload(pipeline,composition,static_cast<int>(settings.size()));
            auto saved = pipeline.at("nodeGraph");
            if (saved.at("nodes").size() != composition.GetNodes().size() || saved.at("links").size() != composition.GetLinks().size())
                throw std::runtime_error("The project graph could not be restored without dropping authored data.");
            for (const auto& link : saved.at("links"))
                if (!composition.HasLink(link.at("fromNodeId").get<int>(),link.at("fromSocket").get<std::string>(),
                    link.at("toNodeId").get<int>(),link.at("toSocket").get<std::string>()))
                    throw std::runtime_error("A saved project connection could not be restored.");
            int sourceId = 0;
            const auto sourceUuid = pipeline.value("rawLayerSourceNodeUuid",std::string{});
            for (const auto& node : composition.GetNodes()) if (node.instanceUuid == sourceUuid) sourceId = node.id;
            if (!sourceUuid.empty() && !sourceId) throw std::runtime_error("The RAW layer source binding is unresolved.");
            if (sourceId) {
                const auto kind = composition.FindNode(sourceId)->kind;
                if (kind != EditorNodeGraph::NodeKind::RawDevelopment && kind != EditorNodeGraph::NodeKind::RawProjectSourceSet &&
                    kind != EditorNodeGraph::NodeKind::MultiFrameDenoise && kind != EditorNodeGraph::NodeKind::MultiFrameHdr)
                    throw std::runtime_error("The RAW layer source binding does not identify a developed RAW source.");
            }
            if (!ValidateRawLayerStack(candidate,error,&composition,sourceId)) return false;
        } catch (const std::exception& exception) { error = exception.what(); return false; }
    }
    result = std::move(candidate);
    return true;
}
} // namespace Stack::Project
