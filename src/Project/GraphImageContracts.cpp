#include "Editor/NodeGraph/NodeGraphTypes.h"
#include "NodeMath/ImageOperationInput.h"
#include "NodeMath/StylizeInput.h"
#include "NodeMath/ToneCurveParameters.h"
#include "Raw/RawGraphParameters.h"
#include "Project/GraphImageContracts.h"
#include "Graph/GraphImageRules.h"
#include "NodeMath/DescriptorSerialization.h"
#include "Raw/RawZoneArea.h"
#include "Raw/RawDevelopmentRecipe.h"
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
void ApplyOutputCrop(SpatialDescriptor& spatial, const RawRecipe::RawCropRotationRecipe& crop) {
    if (!crop.cropEnabled || spatial.fullWindow.width <= 0 || spatial.fullWindow.height <= 0) return;
    const auto width = spatial.fullWindow.width, height = spatial.fullWindow.height;
    const auto left = std::clamp<std::int64_t>(std::int64_t(std::floor(crop.cropX * width)), 0, width - 1);
    const auto top = std::clamp<std::int64_t>(std::int64_t(std::floor(crop.cropY * height)), 0, height - 1);
    const auto right = std::clamp<std::int64_t>(std::int64_t(std::ceil((crop.cropX + crop.cropWidth) * width)), left + 1, width);
    const auto bottom = std::clamp<std::int64_t>(std::int64_t(std::ceil((crop.cropY + crop.cropHeight) * height)), top + 1, height);
    const Matrix cropTransform{double(right-left)/width,0,double(left)/width,
        0,double(bottom-top)/height,double(height-bottom)/height, 0,0,1};
    spatial.sourceTransform = Multiply(spatial.sourceTransform, cropTransform);
    spatial.nativeWidth = static_cast<int>(right-left); spatial.nativeHeight = static_cast<int>(bottom-top);
    spatial.fullWindow = spatial.dataWindow = {0,0,right-left,bottom-top};
}
}
std::optional<Raw::RawWorkingSpace> RawOperationInputWorkingSpace(
    const NodeMath::ValueDescriptor& input) {
    if (input.color.state != NodeMath::KnowledgeState::Known) return std::nullopt;
    if ((input.color.value.identity == "srgb-d65" || input.color.value.identity == "linear-srgb-d65")) return Raw::RawWorkingSpace::LinearSrgbD65;
    if ((input.color.value.identity == "rec2020-d65" || input.color.value.identity == "linear-rec2020-d65")) return Raw::RawWorkingSpace::LinearRec2020D65;
    return std::nullopt;
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
        if ((node.kind == K::MaskGenerator || node.kind == K::CustomMask) && node.maskCanvasSourceNodeId > 0)
            source = describe(node.maskCanvasSourceNodeId,"imageOut");
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
            if (node.kind==K::RawOperation && port!="inputImageOut" && node.rawOperation.enabled) {
                value.range=SemanticField<NumericRange>::Unknown();
                if (node.rawOperation.kind==RawRecipe::GraphOperationKind::ToneCurve || node.rawOperation.kind==RawRecipe::GraphOperationKind::Exposure)
                    value.precision=SemanticField<LogicalPrecision>::Known(LogicalPrecision::Float32);
            }
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
        const auto* sourceMetadata = node.rawDevelopment.embeddedRawData
            ? &node.rawDevelopment.embeddedRawData->metadata : node.rawDevelopment.sourceMetadata.get();
        if ((node.kind == K::RawDevelopment || node.kind == K::RawProjectSourceSet) &&
            sourceMetadata && sourceMetadata->error.empty()) {
            const auto& metadata = *sourceMetadata;
            // RAW preparation produces scene-linear RGB in its authored working
            // basis, with a separate straight coverage/alpha channel. This is a
            // technical boundary fact, never inferred from a pixel average.
            value.logicalType=LogicalValueType::ColorImage;
            value.presentImageComponents=SemanticField<ImageComponentSet>::Known(
                MakeImageComponentSet({ImageComponent::Red,ImageComponent::Green,ImageComponent::Blue,ImageComponent::Alpha}));
            value.channels=SemanticField<ChannelDescriptor>::Known(MakeImageChannelDescriptor(value.presentImageComponents.value));
            value.color=SemanticField<ColorIdentity>::Known({node.rawDevelopment.recipe.technical.workingSpace==Raw::RawWorkingSpace::LinearRec2020D65 ? "rec2020-d65" : "srgb-d65",{},ColorRelation::Standard});
            value.transfer=SemanticField<TransferDescriptor>::Known({TransferKind::Linear,0,{}});
            value.reference=SemanticField<ReferenceState>::Known(ReferenceState::Scene);
            value.alpha=SemanticField<AlphaMode>::Known(AlphaMode::Straight);
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
            ApplyOutputCrop(spatial, placement);
            value.spatial = SemanticField<SpatialDescriptor>::Known(spatial);
        } else if (node.kind == K::Image && node.image.width > 0 && node.image.height > 0) {
            auto spatial = value.spatial.state == KnowledgeState::Known ? value.spatial.value : SpatialDescriptor{};
            spatial.kind = SpatialExtentKind::Finite;
            spatial.nativeWidth = node.image.width; spatial.nativeHeight = node.image.height;
            spatial.fullWindow = spatial.dataWindow = {0,0,node.image.width,node.image.height};
            value.spatial = SemanticField<SpatialDescriptor>::Known(spatial);
        } else if (node.kind == K::Reformat && value.spatial.state == KnowledgeState::Known) {
            value.spatial.value = Stack::NodeMath::ReformatOutputSpatial(value.spatial.value, node.reformatSettings);
        } else if (node.kind == K::Layer && value.spatial.state == KnowledgeState::Known) {
            const auto type = node.layerJson.value("type", std::string{});
            if (type == "RawOutputCrop" && node.layerJson.value("enabled", true))
                ApplyOutputCrop(value.spatial.value,
                    RawRecipe::DeserializeRecipe(node.layerJson.at("recipe")).cropRotation);
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
            const bool curve=node.kind==K::RawOperation && node.rawOperation.kind==RawRecipe::GraphOperationKind::ToneCurve;
            const bool componentMath=curve || (node.kind==K::RawOperation && node.rawOperation.kind==RawRecipe::GraphOperationKind::Exposure);
            if (curve) {
                const auto capability=DescribeImageOperationInput(*source);
                if (node.rawOperation.parameters.value("curveMode",std::string("components"))!="components" && !capability.colorRelationships)
                    graph.unavailableOutputs[id]=capability.colorUnavailableReason;
            } else if (node.kind==K::RawOperation && !componentMath &&
                ((value.transfer.state==KnowledgeState::Known && value.transfer.value.kind!=TransferKind::Linear) ||
                 (value.reference.state==KnowledgeState::Known && value.reference.value!=ReferenceState::Scene)))
                graph.unavailableOutputs[id]="This photo operation needs scene-linear RGB. Add an explicit conversion.";
            if (const auto space=RawOperationInputWorkingSpace(*source)) node.rawWorkingSpace=*space;
            else if (node.kind==K::RawOperation && !componentMath)
                graph.unavailableOutputs[id]="This photo operation needs a declared scene-linear sRGB or Rec.2020 input. Add an explicit color conversion.";
        }
        if (node.kind==K::Layer && source) {
            const auto type=node.layerJson.value("type",std::string{});
            if (type=="CellShading" || type=="PaletteReconstructor" || type=="Halftoning") {
                std::string reason;
                if (!ValidateStylizeInput(type,node.layerJson,DescribeImageOperationInput(*source),reason)) graph.unavailableOutputs[id]=reason;
                value.range=SemanticField<NumericRange>::Unknown();
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
    // Ports remain authored when modes or components change. Only controls
    // participating in this evaluation add uniform producer dependencies.
    for (auto& node:graph.nodes) if (node.kind==K::RawOperation &&
        node.rawOperation.kind==RawRecipe::GraphOperationKind::ToneCurve) {
        const auto capability=DescribeImageOperationInput(node.semanticDescriptor);
        node.uniformParameters.clear();
        for (const auto& parameter:RawRecipe::GraphParameters(node.rawOperation)) {
            bool active=IsToneCurveParameterActive(node.rawOperation.parameters,parameter.id);
            if (active && node.rawOperation.parameters.value("curveMode",std::string{})=="components")
                for (int channel=1;channel<4;++channel)
                    if (parameter.id==std::string("amount-")+RawRecipe::RawPointCurveChannelKey(
                        static_cast<RawRecipe::RawPointCurveChannel>(channel)) &&
                        (capability.scalar || (node.semanticDescriptor.presentImageComponents.state==KnowledgeState::Known &&
                         !HasImageComponent(capability.components,static_cast<ImageComponent>(1u<<(channel-1)))))) active=false;
            if (active) {
                node.uniformParameters.push_back({parameter.id,parameter.label,parameter.path,parameter.minimum,parameter.maximum,true});
                for (auto& dependency:node.outputDependencies) if (dependency.output=="imageOut") {
                    const auto port=EditorNodeGraph::ParameterInputSocketId(parameter.id);
                    if (std::find(dependency.inputs.begin(),dependency.inputs.end(),port)==dependency.inputs.end()) dependency.inputs.push_back(port);
                }
                continue;
            }
            node.uniformParameters.erase(std::remove_if(node.uniformParameters.begin(),node.uniformParameters.end(),
                [&](const auto& value){return value.id==parameter.id;}),node.uniformParameters.end());
            const auto port=EditorNodeGraph::ParameterInputSocketId(parameter.id);
            for (auto& dependency:node.outputDependencies)
                dependency.inputs.erase(std::remove(dependency.inputs.begin(),dependency.inputs.end(),port),dependency.inputs.end());
        }
    }
    graph.outputDescriptor = describe(graph.outputNodeId,graph.outputSocketId.empty()?"imageOut":graph.outputSocketId);
    graph.outputDescriptorIdentity = DescriptorContentIdentity(graph.outputDescriptor);
}
}
