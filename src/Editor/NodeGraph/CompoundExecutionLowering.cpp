#include "Editor/NodeGraph/EditorNodeGraph.h"
#include "Editor/NodeGraph/CompoundInstantiation.h"
#include "Editor/NodeGraph/EditorNodeGraphSerializer.h"
#include "Editor/NodeGraph/GraphOutputSemantics.h"
#include "Editor/NodeGraph/ChannelOutputs.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Graph/LayerOperationParameters.h"

#include <algorithm>
#include <deque>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace EditorNodeGraph {
namespace {

// Limits describe actual expansion work, independently of the number of
// siblings. They bound malformed/unreasonable definitions before allocation.
constexpr std::size_t kMaximumDepth = 64;
constexpr std::size_t kMaximumNodes = 100000;
constexpr std::size_t kMaximumLinks = 400000;
constexpr std::size_t kPlanCacheEntries = 64;
constexpr std::size_t kPlanCacheBytes = 32 * 1024 * 1024;
constexpr std::size_t kPlanCacheNodes = 8192;

std::string DefinitionKey(const Stack::NodeMath::DefinitionReference& reference) {
    return reference.id + "/" + Stack::NodeMath::ToString(reference.version) + "/" + reference.contentHash;
}

struct DefinitionPlan {
    Graph canonical;
    std::unordered_map<std::string, std::size_t> nodesByUuid;
    std::size_t bytes = 0;
};

struct PlanCache {
    struct Entry { std::string key; std::shared_ptr<const DefinitionPlan> plan; };
    std::mutex mutex;
    std::deque<Entry> entries;
    std::size_t bytes = 0;
    std::size_t nodes = 0;
};

std::shared_ptr<const DefinitionPlan> PrepareDefinition(const Stack::NodeMath::CompoundDefinition& definition) {
    // Only exact immutable definition data is shared. Instances, overrides,
    // document identity, selection and output resources never enter this cache.
    static PlanCache cache;
    const auto key = DefinitionKey(definition.identity);
    {
        std::lock_guard lock(cache.mutex);
        const auto found = std::find_if(cache.entries.begin(), cache.entries.end(),
            [&](const auto& entry) { return entry.key == key; });
        if (found != cache.entries.end()) {
            auto entry = *found;
            cache.entries.erase(found);
            cache.entries.push_back(entry);
            return entry.plan;
        }
    }
    auto plan = std::make_shared<DefinitionPlan>();
    DeserializeGraphPayload(definition.canonicalGraph, plan->canonical, 0);
    if (plan->canonical.GetNodes().empty()) throw std::runtime_error("Compound canonical graph is empty.");
    if (plan->canonical.GetNodes().size() > kMaximumNodes || plan->canonical.GetLinks().size() > kMaximumLinks)
        throw std::runtime_error("Compound canonical graph exceeds the execution node/link budget.");
    // Materialize immutable shared image storage before publishing the plan;
    // readers must never lazily mutate a cached image's sharedPixels field.
    for (auto& node : plan->canonical.EditNodes()) node = CompoundInternal::CloneExecutionNode(node);
    for (std::size_t i = 0; i < plan->canonical.GetNodes().size(); ++i)
        if (!plan->nodesByUuid.emplace(plan->canonical.GetNodes()[i].instanceUuid, i).second)
            throw std::runtime_error("Compound canonical graph has duplicate instance identities.");
    plan->bytes = definition.canonicalGraph.dump().size() +
        plan->canonical.GetNodes().size() * sizeof(Node) + plan->canonical.GetLinks().size() * sizeof(Link);
    for (const auto& node : plan->canonical.GetNodes())
        if (node.kind == NodeKind::Image && node.image.sharedPixels) plan->bytes += node.image.sharedPixels->size();
    if (plan->bytes <= kPlanCacheBytes && plan->canonical.GetNodes().size() <= kPlanCacheNodes) {
        std::lock_guard lock(cache.mutex);
        // Another worker may have prepared this immutable definition meanwhile.
        for (const auto& entry : cache.entries) if (entry.key == key) return entry.plan;
        while (!cache.entries.empty() && (cache.entries.size() >= kPlanCacheEntries ||
            cache.bytes + plan->bytes > kPlanCacheBytes ||
            cache.nodes + plan->canonical.GetNodes().size() > kPlanCacheNodes)) {
            cache.bytes -= cache.entries.front().plan->bytes;
            cache.nodes -= cache.entries.front().plan->canonical.GetNodes().size();
            cache.entries.pop_front();
        }
        cache.bytes += plan->bytes;
        cache.nodes += plan->canonical.GetNodes().size();
        cache.entries.push_back({key, plan});
    }
    return plan;
}

struct Endpoint {
    int node = 0;
    std::string socket;
};

std::string EndpointKey(const Endpoint& endpoint) {
    return std::to_string(endpoint.node) + ":" + endpoint.socket;
}

std::string ExecutionUuid(const std::string& parent, const std::string& child) {
    const auto hash = Stack::NodeMath::Sha256ContentIdentity(parent + "/" + child).substr(7);
    return hash.substr(0,8) + "-" + hash.substr(8,4) + "-4" + hash.substr(13,3) +
        "-8" + hash.substr(17,3) + "-" + hash.substr(20,12);
}

class ExecutionLowering {
public:
    ExecutionLowering(const Graph& source, int nextId) : source(source), nextId(nextId) {}

    void Append(Node node, std::size_t depth = 0) {
        if (++instantiatedNodes > kMaximumNodes)
            throw std::runtime_error("Compound expansion exceeds the 100000 instantiated-node budget.");
        std::unordered_map<std::string,int> extractors;
        for(const auto& channel:node.channelOutputs) {
            const auto group=channel.sourceSocket+(channel.missingAlphaIsOpaque?"/opaque":"/exact");
            auto found=extractors.find(group);
            if(found==extractors.end()) {
                if(nextId<=0 || nextId==std::numeric_limits<int>::max())throw std::runtime_error("No component extractor IDs remain.");
                Node split;split.id=nextId++;split.kind=NodeKind::ChannelSplit;split.title="Component output";
                split.instanceUuid=ExecutionUuid(node.instanceUuid,group);
                split.rawLayerOwnerId=node.rawLayerOwnerId;split.splitMissingAlphaIsOpaque=channel.missingAlphaIsOpaque;
                EditorNodeGraphDefinitions::ApplyLiveDefinitionIdentity(split);
                const int id=split.id;
                result.expandedNodeIds.push_back(id);
                AppendLink({node.id,channel.sourceSocket,id,kImageInputSocketId});
                Append(std::move(split),depth);
                found=extractors.emplace(group,id).first;
            }
            outputs.emplace(EndpointKey({node.id,channel.id}),Endpoint{found->second,channel.component});
            result.outputBindings.push_back({node.id,channel.id,found->second,channel.component});
        }
        node.channelOutputs.clear();
        if (node.kind != NodeKind::Compound) {
            if (!leafIds.insert(node.id).second) throw std::runtime_error("Compound expansion produced duplicate node IDs.");
            nodes.push_back(std::move(node));
            return;
        }
        if (depth >= kMaximumDepth) throw std::runtime_error("Compound nesting exceeds the 64-level execution depth budget.");
        const auto key = DefinitionKey(node.compound.instance.definition);
        if (!activeDefinitions.insert(key).second) throw std::runtime_error("Recursive compound definition in execution lowering.");
        if (node.compound.instance.resolution != Stack::NodeMath::CompoundResolutionStatus::Exact)
            throw std::runtime_error(node.compound.instance.resolutionError.empty()
                ? "Exact compound definition is unresolved." : node.compound.instance.resolutionError);
        const auto* definition = source.FindCompoundDefinition(node.compound.instance.definition);
        if (!definition) throw std::runtime_error("Exact compound definition is missing from the document catalog.");
        if (definition->definitionClass == Stack::NodeMath::CompoundDefinitionClass::OpaqueSpecialized)
            throw std::runtime_error("Opaque compound definition has no executable canonical graph.");
        if (checkedDefinitions.insert(key).second &&
            Stack::NodeMath::ComputeCompoundDefinitionContentHash(*definition) != definition->identity.contentHash)
            throw std::runtime_error("Compound definition content does not match its exact identity.");
        auto [prepared, inserted] = plans.try_emplace(key);
        if (inserted) prepared->second = PrepareDefinition(*definition);
        const auto& plan = *prepared->second;
        if (plan.canonical.GetNodes().size() > kMaximumNodes - instantiatedNodes)
            throw std::runtime_error("Compound expansion exceeds the 100000 instantiated-node budget.");
        std::vector<Node> children;
        children.reserve(plan.canonical.GetNodes().size());
        std::unordered_map<int, int> ids;
        for (const auto& canonical : plan.canonical.GetNodes()) {
            if (nextId <= 0 || nextId == std::numeric_limits<int>::max())
                throw std::runtime_error("Compound execution cannot allocate additional node IDs.");
            auto clone = CompoundInternal::CloneExecutionNode(canonical);
            clone.id = nextId++;
            clone.instanceUuid = ExecutionUuid(node.instanceUuid, canonical.instanceUuid);
            if (!node.rawLayerOwnerId.empty()) clone.rawLayerOwnerId = node.rawLayerOwnerId;
            if (clone.kind == NodeKind::Compound) clone.compound.instance.instanceUuid = clone.instanceUuid;
            ids.emplace(canonical.id, clone.id);
            result.expandedNodeIds.push_back(clone.id);
            children.push_back(std::move(clone));
        }
        for (const auto& parameter : definition->parameters) {
            const auto target = plan.nodesByUuid.find(parameter.internalInstanceUuid);
            if (target == plan.nodesByUuid.end() || !CompoundInternal::BindPromotedParameter(children[target->second],
                parameter.internalParameterId, Stack::NodeMath::ResolveCompoundParameterValue(node.compound.instance, parameter)))
                throw std::runtime_error("Promoted compound parameter could not bind to its internal node.");
        }
        for (const auto& port : definition->ports) {
            const auto target = plan.nodesByUuid.find(port.internalInstanceUuid);
            if (target == plan.nodesByUuid.end()) throw std::runtime_error("Compound port references a missing internal instance.");
            const Endpoint endpoint{children[target->second].id, port.internalSocketId};
            auto& aliases = port.direction == Stack::NodeMath::PortDirection::Input ? inputs : outputs;
            if (!aliases.emplace(EndpointKey({node.id, port.id}), endpoint).second)
                throw std::runtime_error("Compound has duplicate public port identities.");
            if (port.direction == Stack::NodeMath::PortDirection::Output)
                result.outputBindings.push_back({node.id, port.id, endpoint.node, endpoint.socket});
        }
        result.authoredCompoundNodeIds.push_back(node.id);
        for (const auto& link : plan.canonical.GetLinks())
            AppendLink({ids.at(link.fromNodeId), link.fromSocketId, ids.at(link.toNodeId), link.toSocketId});
        for (auto& child : children) Append(std::move(child), depth + 1);
        activeDefinitions.erase(key);
    }

    void AppendLink(Link link) {
        if (links.size() >= kMaximumLinks) throw std::runtime_error("Compound expansion exceeds the 400000-link budget.");
        links.push_back(std::move(link));
    }

    void Resolve() {
        for (auto& link : links) {
            const auto from = ResolveEndpoint({link.fromNodeId, link.fromSocketId}, outputs);
            const auto to = ResolveEndpoint({link.toNodeId, link.toSocketId}, inputs);
            link = {from.node, from.socket, to.node, to.socket};
        }
        for (auto& binding : result.outputBindings) {
            const auto endpoint = ResolveEndpoint({binding.expandedNodeId, binding.expandedSocketId}, outputs);
            binding.expandedNodeId = endpoint.node;
            binding.expandedSocketId = endpoint.socket;
        }
    }

    Endpoint ResolveEndpoint(Endpoint endpoint, const std::unordered_map<std::string, Endpoint>& aliases) const {
        for (std::size_t depth = 0; depth <= kMaximumDepth; ++depth) {
            const auto alias = aliases.find(EndpointKey(endpoint));
            if (alias == aliases.end()) {
                if (!leafIds.count(endpoint.node)) throw std::runtime_error("Compound connection references a missing public port or node.");
                return endpoint;
            }
            endpoint = alias->second;
        }
        throw std::runtime_error("Compound port mapping exceeds the execution depth budget.");
    }

    const Graph& source;
    int nextId;
    std::size_t instantiatedNodes = 0;
    std::vector<Node> nodes;
    std::vector<Link> links;
    CompoundExpansionResult result;
    std::unordered_set<int> leafIds;
    std::unordered_set<std::string> activeDefinitions, checkedDefinitions;
    std::unordered_map<std::string, std::shared_ptr<const DefinitionPlan>> plans;
    std::unordered_map<std::string, Endpoint> inputs, outputs;
};

} // namespace

bool Graph::ExpandAllCompoundNodes(Graph& expanded, CompoundExpansionResult* result) const {
    ScopedCompoundLoweringDescription descriptionScope;
    CompoundExpansionResult failure;
    try {
        std::vector<Stack::NodeMath::DefinitionReference> roots;
        for (const auto& node : m_Nodes)
            if (node.kind == NodeKind::Compound) roots.push_back(node.compound.instance.definition);
        if (!roots.empty()) {
            std::string dependencyError;
            const auto closure = Stack::NodeMath::CollectCompoundDependencyClosure(m_CompoundDefinitions, roots, &dependencyError);
            if (closure.empty()) throw std::runtime_error(dependencyError.empty()
                ? "Compound definition dependency closure is unavailable." : dependencyError);
        }
        ExecutionLowering lowering(*this, m_NextNodeId);
        for (const auto& node : m_Nodes) lowering.Append(CompoundInternal::CloneExecutionNode(node));
        for (const auto& link : m_Links) lowering.AppendLink(link);
        lowering.Resolve();
        Graph candidate;
        candidate.m_Nodes = std::move(lowering.nodes);
        candidate.m_Links = std::move(lowering.links);
        candidate.m_CompoundDefinitions = m_CompoundDefinitions;
        candidate.m_NextNodeId = lowering.nextId;
        candidate.m_OutputNodeId = m_OutputNodeId;
        candidate.m_ActiveImageNodeId = m_ActiveImageNodeId;
        candidate.m_AllowNoOutput = m_AllowNoOutput;
        candidate.m_StructureRevision = m_StructureRevision;
        Stack::GraphModel::ReindexLayerParameterRecords(candidate);
        candidate.TouchStructure();
        lowering.result.success = true;
        expanded = std::move(candidate);
        if (result) *result = std::move(lowering.result);
        return true;
    } catch (const std::exception& exception) {
        failure.error = exception.what();
    }
    if (result) *result = std::move(failure);
    return false;
}

} // namespace EditorNodeGraph
