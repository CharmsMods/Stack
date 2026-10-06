#include "Project/GraphImageContracts.h"
#include "Graph/GraphImageRules.h"
#include "NodeMath/DescriptorSerialization.h"
#include "Raw/RawZoneArea.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace Stack::Project {
namespace {
using namespace NodeMath;
using Matrix = std::array<double, 9>;
Matrix Multiply(const Matrix& a, const Matrix& b) {
    Matrix result{};
    for (int row=0; row<3; ++row) for (int col=0; col<3; ++col)
        for (int k=0; k<3; ++k) result[row*3+col] += a[row*3+k]*b[k*3+col];
    return result;
}
}
void BindGraphImageContracts(RenderGraphSnapshot& graph) {
    using K = RenderGraphNodeKind;
    std::unordered_map<int, RenderGraphNode*> nodes;
    std::unordered_map<int, std::vector<RenderGraphLink*>> incoming;
    for (auto& node : graph.nodes) nodes[node.nodeId] = &node;
    for (auto& link : graph.links) incoming[link.toNodeId].push_back(&link);
    std::unordered_map<std::string, ValueDescriptor> outputs;
    std::unordered_set<std::string> visiting;
    std::function<ValueDescriptor(int,const std::string&)> describe;
    describe = [&](int id, const std::string& port) -> ValueDescriptor {
        const auto key = std::to_string(id) + "/" + port;
        if (const auto found = outputs.find(key); found != outputs.end()) return found->second;
        if (!nodes.count(id) || !visiting.insert(key).second) return {};
        auto& node = *nodes.at(id);
        auto value = node.semanticDescriptor;
        const auto input = [&](const std::string& socket) -> std::optional<ValueDescriptor> {
            for (const auto* link : incoming[id]) if (link->toSocketId == socket)
                return describe(link->fromNodeId, link->fromSocketId);
            return std::nullopt;
        };
        auto source = input(node.kind == K::MaskGenerator ? "matchExtent" : "imageIn");
        if (node.kind == K::RawOperation && port == "measurementImageOut")
            if (auto reference = input("referenceIn")) source = reference;
        if (node.kind == K::Mix) {
            source = input("imageA");
            const auto second = input("imageB");
            if (!source) source = second;
            std::string error;
            if (source && second && !GraphModel::ValidateImageCombination(*source,*second,error))
                graph.unavailableOutputs[id] = std::move(error);
        }
        if (!source) {
            for (const auto* link : incoming[id]) {
                if (!GraphModel::OutputDependsOnInput(node.outputDependencies,port,link->toSocketId) ||
                    link->toSocketId == "maskIn" || link->toSocketId == "factor" || link->toSocketId.rfind("param:",0) == 0) continue;
                auto candidate = describe(link->fromNodeId,link->fromSocketId);
                if (candidate.spatial.state == KnowledgeState::Known) { source = std::move(candidate); break; }
            }
        }
        if (source) {
            if (node.kind == K::Output || node.kind == K::RawOperation || node.kind == K::Mix) value = *source;
            if (node.kind == K::Mix) if (const auto second = input("imageB")) {
                if (value.alpha != second->alpha) value.alpha = SemanticField<AlphaMode>::Unknown();
                if (value.color != second->color) value.color = SemanticField<ColorIdentity>::Unknown();
                if (value.transfer != second->transfer) value.transfer = SemanticField<TransferDescriptor>::Unknown();
                if (value.reference != second->reference) value.reference = SemanticField<ReferenceState>::Unknown();
            }
            if (node.kind != K::Output && node.kind != K::RawOperation && node.kind != K::Mix) {
                value.spatial = source->spatial;
                if (node.kind == K::Layer && node.layerJson.value("type", std::string{}) != "ViewTransform") {
                    value.color = source->color; value.transfer = source->transfer;
                    value.reference = source->reference; value.alpha = source->alpha;
                }
            }
        }
        if (node.kind == K::RawSource && Raw::DisplayWidth(node.rawSource.metadata) > 0) {
            SpatialDescriptor spatial;
            spatial.kind = SpatialExtentKind::Finite;
            spatial.nativeWidth = Raw::DisplayWidth(node.rawSource.metadata);
            spatial.nativeHeight = Raw::DisplayHeight(node.rawSource.metadata);
            spatial.fullWindow = spatial.dataWindow = {0,0,spatial.nativeWidth,spatial.nativeHeight};
            value.spatial = SemanticField<SpatialDescriptor>::Known(spatial);
        }
        if ((node.kind == K::RawDevelopment || node.kind == K::RawProjectSourceSet) && node.rawDevelopment.embeddedRawData) {
            const auto& metadata = node.rawDevelopment.embeddedRawData->metadata;
            auto spatial = SpatialDescriptor{};
            spatial.kind = SpatialExtentKind::Finite;
            spatial.nativeWidth = Raw::DisplayWidth(metadata); spatial.nativeHeight = Raw::DisplayHeight(metadata);
            const auto& placement = node.rawDevelopment.recipe.cropRotation;
            if (std::abs(placement.rotationDegrees) % 180 == 90) std::swap(spatial.nativeWidth, spatial.nativeHeight);
            spatial.fullWindow = spatial.dataWindow = {0,0,spatial.nativeWidth,spatial.nativeHeight};
            const auto sourcePoint = [&](float u, float v) {
                auto p = RawRecipe::ZoneAreaSourcePoint(u,1-v,placement,false);
                p.v = 1-p.v;
                return p;
            };
            const auto origin = sourcePoint(0,0);
            const auto x = sourcePoint(1,0);
            const auto y = sourcePoint(0,1);
            spatial.sourceTransform = {x.u-origin.u,y.u-origin.u,origin.u, x.v-origin.v,y.v-origin.v,origin.v, 0,0,1};
            value.spatial = SemanticField<SpatialDescriptor>::Known(spatial);
        } else if (node.kind == K::Image && node.image.width > 0 && node.image.height > 0) {
            auto spatial = value.spatial.state == KnowledgeState::Known ? value.spatial.value : SpatialDescriptor{};
            spatial.kind = SpatialExtentKind::Finite;
            spatial.nativeWidth = node.image.width; spatial.nativeHeight = node.image.height;
            spatial.fullWindow = spatial.dataWindow = {0,0,node.image.width,node.image.height};
            value.spatial = SemanticField<SpatialDescriptor>::Known(spatial);
        } else if (node.kind == K::Reformat && value.spatial.state == KnowledgeState::Known) {
            value.spatial.value.fullWindow = value.spatial.value.dataWindow = {0,0,node.reformatSettings.width,node.reformatSettings.height};
        } else if (node.kind == K::Layer && value.spatial.state == KnowledgeState::Known) {
            const auto type = node.layerJson.value("type", std::string{});
            if (type == "Rotate" || type == "Flip") {
                const double angle = node.layerJson.value("rotation",0.0)*3.14159265358979323846/180;
                const double c = std::cos(angle), s = std::sin(angle);
                const Matrix rotate{c,-s,.5-.5*c+.5*s, s,c,.5-.5*s-.5*c, 0,0,1};
                const bool h = node.layerJson.value("flipH",false), v = node.layerJson.value("flipV",false);
                const Matrix flip{h?-1.:1.,0,h?1.:0., 0,v?-1.:1.,v?1.:0., 0,0,1};
                value.spatial.value.sourceTransform = Multiply(value.spatial.value.sourceTransform, Multiply(rotate,flip));
            }
        }
        if ((node.kind == K::RawOperation || node.kind == K::MaskGenerator) && source) {
            if (node.kind == K::RawOperation && ((value.transfer.state == KnowledgeState::Known && value.transfer.value.kind != TransferKind::Linear) ||
                (value.reference.state == KnowledgeState::Known && value.reference.value != ReferenceState::Scene)))
                graph.unavailableOutputs[id] = "This photo operation needs scene-linear RGB. Add an explicit conversion.";
            if (source->color.state == KnowledgeState::Known) {
                if (source->color.value.identity == "srgb-d65") node.rawWorkingSpace = Raw::RawWorkingSpace::LinearSrgbD65;
                else if (source->color.value.identity == "rec2020-d65") node.rawWorkingSpace = Raw::RawWorkingSpace::LinearRec2020D65;
            }
        }
        if (value.spatial.state == KnowledgeState::Known && (port == "imageOut" || port == "maskOut")) {
            node.nativeWidth = value.spatial.value.nativeWidth;
            node.nativeHeight = value.spatial.value.nativeHeight;
        }
        visiting.erase(key); outputs[key] = value;
        return value;
    };
    for (auto& node : graph.nodes) {
        const auto port = node.kind == K::MaskGenerator ? "maskOut" : "imageOut";
        node.semanticDescriptor = describe(node.nodeId, port);
        node.semanticDescriptorIdentity = DescriptorContentIdentity(node.semanticDescriptor);
    }
    for (auto& link : graph.links) {
        link.semanticDescriptor = describe(link.fromNodeId,link.fromSocketId);
        link.semanticDescriptorIdentity = DescriptorContentIdentity(link.semanticDescriptor);
    }
    graph.outputDescriptor = describe(graph.outputNodeId,graph.outputSocketId.empty()?"imageOut":graph.outputSocketId);
    graph.outputDescriptorIdentity = DescriptorContentIdentity(graph.outputDescriptor);
}
}
