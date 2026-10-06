#include "Graph/GraphDocumentRules.h"
#include "Graph/GraphImageRules.h"
#include "Editor/NodeGraph/UnifiedNodeDefinitionRegistry.h"
#include "Editor/NodeGraph/EditorCompoundDefinitions.h"

#include <algorithm>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace Stack::GraphModel {
namespace {
std::string Key(const Endpoint& endpoint) {
    return endpoint.graphId + "\n" + endpoint.nodeUuid + "\n" + endpoint.portId;
}
}

GraphAnalysis AnalyzeDocument(const DocumentView& document) {
    GraphAnalysis result;
    // Inspect the actual internal branches as well as declared public ports.
    // A reference inside a compound can depend on another layer even when no
    // public image input carries that dependency.
    DocumentView expandedDocument = document;
    std::vector<EditorNodeGraph::Graph> expandedGraphs;
    expandedGraphs.reserve(document.graphs.size());
    bool expandedAny = false;
    for (std::size_t i = 0; i < document.graphs.size(); ++i) {
        const auto& view = document.graphs[i];
        if (!view.graph || std::none_of(view.graph->GetNodes().begin(),view.graph->GetNodes().end(),
                [](const auto& node) { return node.kind == EditorNodeGraph::NodeKind::Compound; })) continue;
        expandedGraphs.emplace_back();
        EditorNodeGraph::CompoundExpansionResult expansion;
        if (!view.graph->ExpandAllCompoundNodes(expandedGraphs.back(),&expansion)) {
            result.valid = false;
            result.errors.push_back(view.id + ": " + expansion.error);
            return result;
        }
        expandedAny = true;
        expandedDocument.graphs[i].graph = &expandedGraphs.back();
        for (const auto& binding : expansion.outputBindings) {
            const auto* authored = view.graph->FindNode(binding.authoredNodeId);
            const auto* lowered = expandedGraphs.back().FindNode(binding.expandedNodeId);
            if (!authored || !lowered) continue;
            DependencyEdge alias{{view.id,lowered->instanceUuid,binding.expandedSocketId},
                {view.id,authored->instanceUuid,binding.authoredSocketId}};
            expandedDocument.dependencies.push_back(alias);
            expandedDocument.valueBindings.push_back(std::move(alias));
        }
    }
    if (expandedAny) return AnalyzeDocument(expandedDocument);
    std::unordered_map<std::string, std::size_t> indices;
    std::vector<Endpoint> endpoints;
    std::vector<std::vector<std::size_t>> outgoing;
    std::vector<std::size_t> indegree;
    const auto index = [&](const Endpoint& endpoint) {
        const auto inserted = indices.emplace(Key(endpoint), endpoints.size());
        if (inserted.second) {
            endpoints.push_back(endpoint); outgoing.emplace_back(); indegree.push_back(0);
        }
        return inserted.first->second;
    };
    const auto edge = [&](const Endpoint& source, const Endpoint& destination) {
        const auto from = index(source), to = index(destination);
        outgoing[from].push_back(to); ++indegree[to];
    };
    std::unordered_set<std::string> graphIds;
    for (const auto& view : document.graphs) {
        if (!view.graph || view.id.empty() || !graphIds.insert(view.id).second) {
            result.errors.push_back("Graph identities must be distinct and nonempty.");
            continue;
        }
        const auto validation = view.graph->Validate();
        if (!validation.valid)
            result.errors.insert(result.errors.end(), validation.messages.begin(), validation.messages.end());
        for (const auto& node : view.graph->GetNodes()) {
            const auto sockets = view.graph->GetSockets(node, false);
            if (node.kind == EditorNodeGraph::NodeKind::Output)
                edge({view.id,node.instanceUuid,"imageIn"},{view.id,node.instanceUuid,"imageOut"});
            if (node.role == NodeRole::Reference && !node.reference.Empty()) {
                for (const auto& socket : sockets)
                    if (socket.direction == EditorNodeGraph::SocketDirection::Output)
                        edge(node.reference, {view.id, node.instanceUuid, socket.id});
            }
            for (const auto& input : sockets) {
                if (input.direction != EditorNodeGraph::SocketDirection::Input || input.optional) continue;
                const bool connected = std::any_of(view.graph->GetLinks().begin(), view.graph->GetLinks().end(), [&](const auto& link) {
                    return link.toNodeId == node.id && link.toSocketId == input.id;
                });
                if (!connected) result.drafts.push_back({{view.id, node.instanceUuid, input.id},
                    node.title + ": connect " + input.label + "."});
            }
            for (const auto& output : sockets) {
                if (output.direction != EditorNodeGraph::SocketDirection::Output) continue;
                for (const auto& input : sockets) {
                    if (input.direction == EditorNodeGraph::SocketDirection::Input &&
                        EditorNodeGraphDefinitions::OutputDependsOnInput(*view.graph, node, output.id, input.id))
                        edge({view.id, node.instanceUuid, input.id}, {view.id, node.instanceUuid, output.id});
                }
            }
        }
        for (const auto& link : view.graph->GetLinks()) {
            const auto* from = view.graph->FindNode(link.fromNodeId);
            const auto* to = view.graph->FindNode(link.toNodeId);
            if (from && to) edge({view.id, from->instanceUuid, link.fromSocketId},
                {view.id, to->instanceUuid, link.toSocketId});
        }
    }
    for (const auto& dependency : document.dependencies) edge(dependency.source, dependency.destination);
    std::vector<std::size_t> ready;
    for (std::size_t i = 0; i < indegree.size(); ++i) if (!indegree[i]) ready.push_back(i);
    for (std::size_t i = 0; i < ready.size(); ++i)
        for (auto next : outgoing[ready[i]]) if (--indegree[next] == 0) ready.push_back(next);
    if (ready.size() != endpoints.size()) {
        // Follow unresolved predecessors until a vertex repeats. This reports
        // a real cycle, rather than a list of nodes merely downstream of one.
        std::vector<std::vector<std::size_t>> incoming(endpoints.size());
        for (std::size_t i = 0; i < outgoing.size(); ++i)
            if (indegree[i]) for (auto next : outgoing[i]) if (indegree[next]) incoming[next].push_back(i);
        std::size_t current = 0;
        while (!indegree[current]) ++current;
        std::vector<std::size_t> walk;
        std::unordered_map<std::size_t, std::size_t> seen;
        while (seen.emplace(current, walk.size()).second) {
            walk.push_back(current); current = incoming[current].front();
        }
        for (std::size_t i = seen.at(current); i < walk.size(); ++i) result.cycle.push_back(endpoints[walk[i]]);
        result.cycle.push_back(endpoints[current]);
        std::reverse(result.cycle.begin(), result.cycle.end());
        std::string message = "Connection creates a dependency loop: ";
        for (const auto& endpoint : result.cycle) {
            if (message.back() != ' ') message += " -> ";
            message += endpoint.graphId + "/" + endpoint.nodeUuid + "/" + endpoint.portId;
        }
        result.errors.push_back(std::move(message));
    }
    if (result.errors.empty()) {
        using Description = EditorNodeGraph::GraphOutputDescription;
        std::unordered_map<std::string, const EditorNodeGraph::Graph*> graphs;
        std::unordered_map<std::string, Endpoint> aliases;
        std::unordered_map<std::string, Description> descriptions;
        std::unordered_set<std::string> resolving;
        for (const auto& view : document.graphs) graphs.emplace(view.id, view.graph);
        for (const auto& binding : document.valueBindings) aliases[Key(binding.destination)] = binding.source;
        for (const auto& source : document.sources) descriptions[Key(source.first)].descriptor = source.second;
        std::function<Description(const Endpoint&)> describe;
        describe = [&](const Endpoint& endpoint) -> Description {
            const auto key = Key(endpoint);
            if (const auto found = descriptions.find(key); found != descriptions.end()) return found->second;
            if (!resolving.insert(key).second) return {};
            Description value;
            if (const auto alias = aliases.find(key); alias != aliases.end()) value = describe(alias->second);
            else if (const auto graph = graphs.find(endpoint.graphId); graph != graphs.end()) {
                const auto& nodes = graph->second->GetNodes();
                const auto node = std::find_if(nodes.begin(), nodes.end(), [&](const auto& n) { return n.instanceUuid == endpoint.nodeUuid; });
                if (node != nodes.end()) {
                    if (node->role == NodeRole::Reference) value = describe(node->reference);
                    else {
                        EditorNodeGraph::SocketDefinition socket;
                        if (graph->second->FindSocket(node->id, endpoint.portId, &socket) && socket.direction == EditorNodeGraph::SocketDirection::Input) {
                            if (const auto* link = graph->second->FindInputLink(node->id,endpoint.portId))
                                if (const auto* producer = graph->second->FindNode(link->fromNodeId))
                                    value = describe({endpoint.graphId,producer->instanceUuid,link->fromSocketId});
                        } else {
                            EditorNodeGraph::GraphOutputContext context;
                            context.resolveOutput = [&](const auto& n, const auto& port) -> std::optional<Description> {
                                const Endpoint output{endpoint.graphId,n.instanceUuid,port};
                                if (n.role == NodeRole::Reference) return describe(n.reference);
                                if (aliases.count(Key(output)) || descriptions.count(Key(output))) return describe(output);
                                return std::nullopt;
                            };
                            value = EditorNodeGraph::DescribeGraphOutput(*graph->second,node->id,endpoint.portId,context);
                        }
                    }
                }
            }
            resolving.erase(key); descriptions[key] = value; return value;
        };
        for (const auto& view : document.graphs) {
            for (const auto& node : view.graph->GetNodes()) {
                const bool reference = node.role == NodeRole::Reference;
                const auto declared = reference ? node.referenceType : node.outputSettings.publishedType;
                if ((!reference && node.kind != EditorNodeGraph::NodeKind::Output) ||
                    declared == Stack::NodeMath::LogicalValueType::Invalid) continue;
                const auto value = describe({view.id,node.instanceUuid,
                    reference && node.kind == EditorNodeGraph::NodeKind::MaskGenerator ? "maskOut" : "imageOut"});
                const auto actual = value.descriptor.logicalType;
                if (actual != Stack::NodeMath::LogicalValueType::Invalid && actual != declared &&
                    !(EditorNodeGraph::IsSingleChannelValue(actual) && EditorNodeGraph::IsSingleChannelValue(declared)))
                    result.errors.push_back(view.id + "/" + node.title + ": the published value type does not match its declared port. Add an explicit conversion or publish a new output.");
            }
            for (const auto& node : view.graph->GetNodes()) if (node.kind == EditorNodeGraph::NodeKind::Mix) {
                const auto a = describe({view.id,node.instanceUuid,"imageA"});
                const auto b = describe({view.id,node.instanceUuid,"imageB"});
                std::string error;
                if (!ValidateImageCombination(a.descriptor,b.descriptor,error))
                    result.errors.push_back(view.id + "/" + node.title + ": " + error);
            }
            for (const auto& link : view.graph->GetLinks()) {
                const auto* from = view.graph->FindNode(link.fromNodeId);
                const auto* to = view.graph->FindNode(link.toNodeId);
                if (!from || !to) continue;
                const auto value = describe({view.id,from->instanceUuid,link.fromSocketId});
                std::string error;
                if (!EditorNodeGraphDefinitions::ValidateInputDescriptor(*view.graph,*to,link.toSocketId,value.descriptor,error))
                    result.errors.push_back(view.id + "/" + to->title + "/" + link.toSocketId + ": " + error);
                if (from->role == NodeRole::Reference && value.descriptor.logicalType == Stack::NodeMath::LogicalValueType::Invalid)
                    result.drafts.push_back({{view.id,from->instanceUuid,link.fromSocketId},"The published source is unresolved or unfinished."});
            }
        }
    }
    result.valid = result.errors.empty();
    return result;
}

EditProposal ProposeEdit(const EditorNodeGraph::Graph& source, std::uint64_t revision,
    const std::function<void(EditorNodeGraph::Graph&)>& edit) {
    EditProposal proposal;
    proposal.sourceRevision = revision;
    proposal.candidate = source;
    try { edit(proposal.candidate); }
    catch (const std::exception& exception) {
        proposal.analysis.valid = false;
        proposal.analysis.errors.push_back(exception.what());
        return proposal;
    }
    proposal.analysis = AnalyzeDocument({{{"graph", &proposal.candidate}}, {}});
    for (const auto& node : proposal.candidate.GetNodes())
        if (node.kind == EditorNodeGraph::NodeKind::Output)
            proposal.affectedOutputs.push_back({"graph", node.instanceUuid, "imageOut"});
    return proposal;
}

EditProposal ProposeInsertion(const EditorNodeGraph::Graph& source, std::uint64_t revision,
    int nodeId, const EditorNodeGraph::Link& link) {
    const auto* node = source.FindNode(nodeId);
    if (node && link.ownership != EditorNodeGraph::Link::Ownership::ManagedSourceBinding) {
        const auto sockets = source.GetSockets(*node, true);
        for (const auto& input : sockets) for (const auto& output : sockets) {
            if (input.direction != EditorNodeGraph::SocketDirection::Input || output.direction != EditorNodeGraph::SocketDirection::Output) continue;
            auto proposal = ProposeEdit(source, revision, [&](auto& graph) {
                std::string error;
                if (!graph.TryConnectSockets(link.fromNodeId,link.fromSocketId,nodeId,input.id,&error) ||
                    !graph.TryConnectSockets(nodeId,output.id,link.toNodeId,link.toSocketId,&error)) throw std::runtime_error(error);
            });
            if (proposal.analysis.valid) return proposal;
        }
    }
    auto proposal = ProposeEdit(source,revision,[](auto&) {});
    proposal.analysis.valid = false;
    proposal.analysis.errors.push_back("This operation cannot be inserted on that connection. Connect its inputs and outputs explicitly.");
    return proposal;
}

EditProposal ProposeSerialMove(const EditorNodeGraph::Graph& source, std::uint64_t revision, int nodeId, bool forward) {
    return ProposeEdit(source,revision,[&](auto& graph) {
        const auto output = [&](int id) -> EditorNodeGraph::Link {
            std::vector<EditorNodeGraph::Link> links;
            for (const auto& link : source.GetLinks()) if (link.fromNodeId == id) links.push_back(link);
            if (links.size() != 1 || links.front().fromSocketId != "imageOut" || links.front().toSocketId != "imageIn")
                throw std::runtime_error("This operation branches. Reconnect its wires explicitly to change the order.");
            return links.front();
        };
        const auto* input = source.FindInputLink(nodeId,"imageIn");
        if (!input) throw std::runtime_error("Connect this operation's image input before reordering it.");
        int first = forward ? nodeId : input->fromNodeId;
        const auto middle = output(first);
        int second = middle.toNodeId;
        const auto* a = source.FindNode(first);
        const auto* b = source.FindNode(second);
        const auto movable = [](const auto* n) { return n && n->role == NodeRole::Ordinary &&
            (n->kind == EditorNodeGraph::NodeKind::RawOperation || n->kind == EditorNodeGraph::NodeKind::Layer); };
        if (!movable(a) || !movable(b)) throw std::runtime_error("The adjacent node is a source, result or branch boundary. Reconnect explicitly.");
        const auto* before = source.FindInputLink(first,"imageIn");
        if (!before) throw std::runtime_error("The preceding image input is unfinished.");
        const auto after = output(second);
        graph.RemoveLink(before->fromNodeId,before->fromSocketId,first,"imageIn");
        graph.RemoveLink(first,"imageOut",second,"imageIn");
        graph.RemoveLink(second,"imageOut",after.toNodeId,after.toSocketId);
        std::string error;
        if (!graph.TryConnectSockets(before->fromNodeId,before->fromSocketId,second,"imageIn",&error) ||
            !graph.TryConnectSockets(second,"imageOut",first,"imageIn",&error) ||
            !graph.TryConnectSockets(first,"imageOut",after.toNodeId,after.toSocketId,&error)) throw std::runtime_error(error);
        std::swap(graph.FindNode(first)->position,graph.FindNode(second)->position);
    });
}

void RetainImageStorage(EditorNodeGraph::Graph& candidate, EditorNodeGraph::Graph& previous) {
    std::unordered_map<std::string, EditorNodeGraph::ImagePayload*> images;
    for (auto& node : previous.GetNodes())
        if (node.kind == EditorNodeGraph::NodeKind::Image) images.emplace(node.instanceUuid, &node.image);
    for (auto& node : candidate.GetNodes()) {
        if (node.kind != EditorNodeGraph::NodeKind::Image) continue;
        const auto found = images.find(node.instanceUuid);
        if (found == images.end()) continue;
        auto& old = *found->second;
        auto& image = node.image;
        if (image.width == old.width && image.height == old.height && image.channels == old.channels && image.pixels == old.pixels) {
            image.pixels.swap(old.pixels);
            image.sharedPixels = old.sharedPixels;
            image.pixelsFingerprint = old.pixelsFingerprint;
        }
        if (image.pngBytes == old.pngBytes) image.pngBytes.swap(old.pngBytes);
        if (image.previewPixels == old.previewPixels) image.previewPixels.swap(old.previewPixels);
    }
}

bool ApplyEdit(EditorNodeGraph::Graph& graph, std::uint64_t revision,
    EditProposal proposal, std::string& error) {
    if (!proposal.analysis.valid) {
        error = proposal.analysis.errors.empty() ? "The proposed edit is invalid." : proposal.analysis.errors.front();
        return false;
    }
    if (proposal.sourceRevision != revision) {
        error = "The graph changed while this edit was prepared. Try the edit again.";
        return false;
    }
    proposal.analysis = AnalyzeDocument({{{"graph", &proposal.candidate}}, {}});
    if (!proposal.analysis.valid) { error = proposal.analysis.errors.front(); return false; }
    RetainImageStorage(proposal.candidate, graph);
    graph = std::move(proposal.candidate);
    error.clear();
    return true;
}

std::vector<EditorNodeGraphDefinitions::NodeCatalogEntry> QueryAvailableNodes(
    const EditorNodeGraph::Graph& graph,
    const std::optional<std::pair<int, std::string>>& dragged, EditorNodeGraphDefinitions::LiveGraphRole role,
    const std::function<bool(const EditorNodeGraph::Graph&)>& accepts) {
    auto entries = EditorNodeGraphDefinitions::BuildRegisteredNodeCatalogEntries();
    entries.erase(std::remove_if(entries.begin(), entries.end(), [role](const auto& entry) {
        if (entry.kind == EditorNodeGraph::NodeKind::Compound)
            return EditorNodeGraphDefinitions::FindShippedCompoundTemplate(static_cast<std::size_t>(entry.value)) == nullptr;
        const auto* definition = EditorNodeGraphDefinitions::FindLiveNodeDefinition(
            EditorNodeGraphDefinitions::BuildPrototypeNode(entry));
        return !definition || !EditorNodeGraphDefinitions::DefinitionSupportsGraphRole(*definition, role);
    }), entries.end());
    if (!dragged) return entries;
    EditorNodeGraph::SocketDefinition endpoint;
    if (!graph.FindSocket(dragged->first, dragged->second, &endpoint)) return {};
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const auto& entry) {
        auto candidate = graph.CloneForAnalysis();
        int id = 0;
        if (entry.kind == EditorNodeGraph::NodeKind::Compound) {
            const auto* definition = EditorNodeGraphDefinitions::FindShippedCompoundTemplate(static_cast<std::size_t>(entry.value));
            if (!definition || !candidate.AddCompoundDefinition(*definition)) return true;
            const auto* node = candidate.AddCompoundNode(definition->identity, {});
            if (!node) return true;
            id = node->id;
        } else {
            auto node = EditorNodeGraphDefinitions::BuildPrototypeNode(entry);
            node.id = candidate.GetNextNodeId();
            id = node.id;
            if (node.kind == EditorNodeGraph::NodeKind::Layer) {
                node.layerIndex = 0;
                for (const auto& existing : graph.GetNodes())
                    if (existing.kind == EditorNodeGraph::NodeKind::Layer) node.layerIndex = std::max(node.layerIndex,existing.layerIndex+1);
            }
            candidate.EditNodes().push_back(std::move(node));
            candidate.SetNextNodeId(id+1);
        }
        for (const auto& socket : candidate.GetSockets(*candidate.FindNode(id), false)) {
            if (socket.direction == endpoint.direction) continue;
            auto attempt = candidate;
            const bool connected = endpoint.direction == EditorNodeGraph::SocketDirection::Output
                ? attempt.TryConnectSockets(dragged->first, dragged->second, id, socket.id)
                : attempt.TryConnectSockets(id, socket.id, dragged->first, dragged->second);
            if (connected && (!accepts || accepts(attempt))) return false;
        }
        return true;
    }), entries.end());
    return entries;
}
} // namespace Stack::GraphModel
