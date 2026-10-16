#include "Editor/NodeGraph/Definitions/OperationInventory.h"
#include "Editor/LayerRegistry.h"
#include <algorithm>
#include <set>

namespace EditorNodeGraphDefinitions {
using EditorNodeGraph::Node;
using EditorNodeGraph::NodeKind;
using Stack::NodeMath::LogicalValueType;

int OperationVariant(const Node& node) {
    switch (node.kind) {
        case NodeKind::Layer: return static_cast<int>(node.layerType);
        case NodeKind::Scope: return static_cast<int>(node.scopeKind);
        case NodeKind::MaskTexture:
        case NodeKind::MaskGenerator: return static_cast<int>(node.maskKind);
        case NodeKind::MaskCombine: return static_cast<int>(node.maskCombineMode);
        case NodeKind::MaskUtility: return static_cast<int>(node.maskUtilityKind);
        case NodeKind::ImageToMask: return static_cast<int>(node.imageToMaskKind);
        case NodeKind::ImageGenerator: return static_cast<int>(node.imageGeneratorKind);
        case NodeKind::DataMath: return static_cast<int>(node.dataMathMode);
        case NodeKind::Value: return static_cast<int>(node.value.value.logicalType);
        case NodeKind::RawOperation: return static_cast<int>(node.rawOperation.kind);
        case NodeKind::TechnicalImage: return static_cast<int>(node.technicalImageSettings.operation);
        default: return 0;
    }
}

Node BuildOperationPrototype(NodeKind kind, int variant) {
    Node node;
    node.kind = kind;
    switch (kind) {
        case NodeKind::Layer: node.layerType = static_cast<LayerType>(variant); break;
        case NodeKind::Scope: node.scopeKind = static_cast<EditorNodeGraph::ScopeKind>(variant); break;
        case NodeKind::MaskTexture:
        case NodeKind::MaskGenerator: node.maskKind = static_cast<EditorNodeGraph::MaskGeneratorKind>(variant); break;
        case NodeKind::MaskCombine: node.maskCombineMode = static_cast<EditorNodeGraph::MaskCombineMode>(variant); break;
        case NodeKind::MaskUtility: node.maskUtilityKind = static_cast<EditorNodeGraph::MaskUtilityKind>(variant); break;
        case NodeKind::ImageToMask: node.imageToMaskKind = static_cast<EditorNodeGraph::ImageToMaskKind>(variant); break;
        case NodeKind::ImageGenerator: node.imageGeneratorKind = static_cast<EditorNodeGraph::ImageGeneratorKind>(variant); break;
        case NodeKind::DataMath: node.dataMathMode = static_cast<EditorNodeGraph::DataMathMode>(variant); break;
        case NodeKind::Value:
            node.value.value = BuildDefaultFirstClassValue(static_cast<LogicalValueType>(variant));
            break;
        case NodeKind::RawOperation:
            node.rawOperation = Stack::RawRecipe::MakeGraphOperation(static_cast<Stack::RawRecipe::GraphOperationKind>(variant));
            break;
        case NodeKind::TechnicalImage:
            node.technicalImageSettings.operation = static_cast<Stack::NodeMath::TechnicalImageOperation>(variant);
            break;
        default: break;
    }
    return node;
}

const std::vector<OperationRegistration>& GetOperationInventory() {
    static const std::vector<OperationRegistration> operations = [] {
        std::vector<OperationRegistration> result;
        OperationDefinitions::AppendSourceOperations(result);
        OperationDefinitions::AppendImageEffectOperations(result);
        OperationDefinitions::AppendFieldOperations(result);
        return result;
    }();
    return operations;
}

OperationDisposition GetOperationDisposition(NodeKind kind) {
    if (kind == NodeKind::Compound) return OperationDisposition::EmbeddedCompound;
    return OperationDisposition::Builtin;
}

bool ValidateOperationInventoryCoverage(std::vector<std::string>& errors) {
    const auto initialErrorCount = errors.size();
    std::set<std::pair<NodeKind,int>> expected;
    const auto variants = [&](NodeKind kind, int first, int last) {
        for (int variant=first; variant<=last; ++variant) expected.emplace(kind,variant);
    };
    // These are constructor selector domains, not browser membership. A domain
    // such as Data Math Values/Spatial is authored settings within one variant.
    variants(NodeKind::Scope, 0, static_cast<int>(EditorNodeGraph::ScopeKind::RGBParade));
    variants(NodeKind::MaskGenerator, 0, static_cast<int>(EditorNodeGraph::MaskGeneratorKind::PaintedArea));
    for (auto shape : {EditorNodeGraph::MaskGeneratorKind::LinearGradient,
        EditorNodeGraph::MaskGeneratorKind::RadialGradient, EditorNodeGraph::MaskGeneratorKind::Square})
        expected.emplace(NodeKind::MaskTexture,static_cast<int>(shape));
    variants(NodeKind::MaskCombine, 0, static_cast<int>(EditorNodeGraph::MaskCombineMode::Exclude));
    variants(NodeKind::MaskUtility, 0, static_cast<int>(EditorNodeGraph::MaskUtilityKind::Threshold));
    variants(NodeKind::ImageToMask, 0, static_cast<int>(EditorNodeGraph::ImageToMaskKind::SampledRange));
    variants(NodeKind::ImageGenerator, 0, static_cast<int>(EditorNodeGraph::ImageGeneratorKind::Text));
    variants(NodeKind::DataMath, 0, static_cast<int>(EditorNodeGraph::DataMathMode::ImageAverage));
    variants(NodeKind::Value, static_cast<int>(LogicalValueType::Boolean), static_cast<int>(LogicalValueType::Failure)-1);
    variants(NodeKind::TechnicalImage, 0, static_cast<int>(Stack::NodeMath::TechnicalImageOperation::Unpremultiply));
    variants(NodeKind::RawOperation, 0, static_cast<int>(Stack::RawRecipe::GraphOperationKind::Count)-1);
    for (const auto& descriptor : LayerRegistry::GetAllDescriptors())
        expected.emplace(NodeKind::Layer,static_cast<int>(descriptor.type));
    for (int i=0; i<static_cast<int>(NodeKind::Count); ++i) {
        const auto kind=static_cast<NodeKind>(i);
        if (GetOperationDisposition(kind)!=OperationDisposition::Builtin) continue;
        if (std::none_of(expected.begin(),expected.end(),[&](const auto& key){return key.first==kind;}))
            expected.emplace(kind,0);
    }
    for (const auto& registration : GetOperationInventory()) {
        const auto& definition=registration.definition;
        const auto key=std::make_pair(definition.kind,definition.variant);
        if (!expected.erase(key)) errors.push_back("Unexpected or repeated operation variant: " + definition.identity.id);
        if (OperationVariant(BuildOperationPrototype(definition.kind,definition.variant)) != definition.variant)
            errors.push_back("Operation prototype loses its variant: " + definition.identity.id);
        if (definition.kind==NodeKind::Value && !Stack::NodeMath::ValidateFirstClassValue(
            BuildOperationPrototype(definition.kind,definition.variant).value.value).empty())
            errors.push_back("Typed value has an invalid default: " + definition.identity.id);
    }
    for (const auto& key : expected) errors.push_back("Missing operation kind/variant: " +
        std::to_string(static_cast<int>(key.first)) + "/" + std::to_string(key.second));
    return errors.size()==initialErrorCount;
}

} // namespace EditorNodeGraphDefinitions
