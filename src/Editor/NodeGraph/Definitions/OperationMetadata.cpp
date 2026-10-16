#include "Editor/NodeGraph/Definitions/OperationInventory.h"
#include "Editor/LayerRegistry.h"
#include <cctype>
#include <utility>

namespace EditorNodeGraphDefinitions {
using EditorNodeGraph::NodeKind;
namespace {
std::string StableToken(std::string value) {
    std::string result;
    result.reserve(value.size());
    bool separator = false;
    for (unsigned char ch : value) {
        if (std::isalnum(ch)) {
            if (separator && !result.empty() && result.back() != '/') result.push_back('-');
            result.push_back(static_cast<char>(std::tolower(ch)));
            separator = false;
        } else if (ch == ':' || ch == '/') {
            while (!result.empty() && (result.back() == '-' || result.back() == '/')) result.pop_back();
            if (!result.empty()) result.push_back('/');
            separator = false;
        } else {
            separator = true;
        }
    }
    while (!result.empty() && (result.back() == '-' || result.back() == '/')) result.pop_back();
    return result.empty() ? "node" : result;
}
} // namespace
namespace OperationDefinitions {
LiveNodeDefinition MakeOperation(NodeKind kind, int variant,
    std::string label, std::string category, std::string operationKey,
    NodeCatalogPreviewStrategy strategy, std::uint32_t previewRecipeVersion,
    bool visibleInBrowser) {
    LiveNodeDefinition definition;
    definition.kind = kind;
    definition.variant = variant;
    if (kind == NodeKind::RawOperation && variant==static_cast<int>(Stack::RawRecipe::GraphOperationKind::ToneCurve))
        definition.identity.id="stack:tone/curve";
    else if (kind == NodeKind::FieldMean) definition.identity.id = "stack:analysis/field-mean";
    else if (kind == NodeKind::Reformat) definition.identity.id = "stack:geometry/reformat";
    else if (kind == NodeKind::Layer) {
        const auto* descriptor = LayerRegistry::GetDescriptor(static_cast<LayerType>(variant));
        definition.identity.id = "stack:layer/" + StableToken(descriptor && descriptor->typeId ? descriptor->typeId : label);
    } else definition.identity.id = "stack:graph/" + StableToken(operationKey);
    const bool v2 = kind == NodeKind::Reformat || kind == NodeKind::Output || kind == NodeKind::ChannelCombine || kind == NodeKind::RawOperation || kind == NodeKind::Layer;
    definition.identity.version = kind==NodeKind::RawOperation && variant==static_cast<int>(Stack::RawRecipe::GraphOperationKind::ToneCurve)
        ? Stack::NodeMath::SemanticVersion{3,0,0}
        : v2 ? Stack::NodeMath::SemanticVersion{2,0,0} : Stack::NodeMath::SemanticVersion{1,0,0};
    definition.label = std::move(label);
    definition.category = std::move(category);
    definition.previewKey = std::move(operationKey);
    definition.previewRecipeVersion = previewRecipeVersion;
    definition.previewStrategy = strategy;
    definition.visibleInBrowser = visibleInBrowser;
    return definition;
}
} // namespace OperationDefinitions
} // namespace EditorNodeGraphDefinitions
