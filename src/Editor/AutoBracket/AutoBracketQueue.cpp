#include "AutoBracketQueue.h"
#include "NodeMath/ContractTypes.h"
#include "Raw/Internal/RawWorkspaceStorageIO.h"
#include <algorithm>
#include <set>
#include <unordered_map>

namespace Stack::AutoBracket {
const char* StateName(State state) {
    switch(state) {
    case State::Pending:return "Waiting";case State::Running:return "Processing";
    case State::Completed:return "Completed";case State::Attention:return "Needs attention";
    case State::Failed:return "Failed";case State::Excluded:return "Excluded";
    }return "Waiting";
}
std::string SourceIdentity(const std::vector<Source>& sources) {
    std::set<std::string> identities;
    for(const auto& source:sources) {
        if(source.fingerprint.empty())return {};
        identities.insert(source.fingerprint+":"+std::to_string(source.bytes));
    }
    std::string text="stack.auto-bracket.sources.v1\n";
    for(const auto& identity:identities)text+=identity+"\n";
    return identities.size()<2?std::string():NodeMath::Sha256ContentIdentity(text);
}
std::vector<Candidate> CollectCandidates(const std::vector<RawWorkspace::SourceRecord>& sources,
    const std::vector<RawWorkspace::RawGallerySimilarityStack>& stacks) {
    std::unordered_map<std::string,const RawWorkspace::SourceRecord*> records;
    for(const auto& source:sources)records.emplace(source.relativePathKey,&source);
    std::vector<Candidate> candidates;
    for(const auto& stack:stacks) {
        Candidate candidate;std::set<std::string> seen;
        for(const auto& key:stack.sourceKeys) {
            const auto found=records.find(key);if(found==records.end())continue;
            const auto& source=*found->second;
            if(source.fingerprint.empty()||!RawWorkspace::DefaultRawPathPredicate(source.absolutePath)||
                !seen.insert(source.fingerprint).second)continue;
            candidate.sources.push_back({source.absolutePath,source.fingerprint,source.fileSizeBytes});
            if(candidate.name.empty())candidate.name=source.stem+" Bracket";
        }
        candidate.identity=SourceIdentity(candidate.sources);
        if(!candidate.identity.empty())candidates.push_back(std::move(candidate));
    }
    return candidates;
}
bool LoadQueue(const std::filesystem::path& root,Queue& queue,std::string& error) {
    queue={};queue.root=root;
    const auto path=RawWorkspace::BuildManagedLayout(root).catalogDirectory/"brackets.json";
    std::error_code ec;if(!std::filesystem::exists(path,ec))return !ec;
    try {
        Project::json data;
        if(!RawWorkspace::StorageIO::ReadJsonFile(path,data)||data.at("version")!=1)
            throw std::runtime_error("The saved bracket queue could not be read.");
        queue.paused=data.value("paused",false);
        queue.resumeWhenIdle=queue.paused&&data.value("resumeWhenIdle",false);
        std::set<std::string> seen;
        for(const auto& value:data.at("items")) {
            Item item;item.candidate.identity=value.at("identity");item.candidate.name=value.at("name");
            if(!seen.insert(item.candidate.identity).second)continue;
            const int state=value.at("state");
            if(state<0||state>static_cast<int>(State::Excluded))throw std::runtime_error("Unknown bracket queue status.");
            item.state=static_cast<State>(state);if(item.state==State::Running)item.state=State::Pending;
            item.projectId=value.value("projectId",std::string());
            const auto relative=std::filesystem::u8path(value.value("projectPath",std::string()));
            if(relative.is_absolute()||std::find(relative.begin(),relative.end(),"..")!=relative.end())
                throw std::runtime_error("Invalid bracket project location.");
            if(!relative.empty())item.projectPath=root/relative;
            item.error=value.value("error",std::string());item.available=false;
            queue.items.push_back(std::move(item));
        }
        return true;
    }catch(const std::exception& exception){queue.paused=true;error=exception.what();return false;}
}
bool SaveQueue(const Queue& queue,std::string& error) {
    Project::json data={{"version",1},{"paused",queue.paused},{"resumeWhenIdle",queue.resumeWhenIdle},{"items",Project::json::array()}};
    for(const auto& item:queue.items) {
        data["items"].push_back({{"identity",item.candidate.identity},{"name",item.candidate.name},
            {"state",static_cast<int>(item.state)},{"projectId",item.projectId},
            {"projectPath",item.projectPath.empty()?std::string():item.projectPath.lexically_relative(queue.root).generic_u8string()},
            {"error",item.error}});
    }
    return RawWorkspace::StorageIO::WriteJsonFile(
        RawWorkspace::BuildManagedLayout(queue.root).catalogDirectory/"brackets.json",data,&error);
}
void Reconcile(Queue& queue,const std::vector<Candidate>& candidates,
    const std::map<std::string,std::pair<std::string,std::filesystem::path>>& represented) {
    for(auto& item:queue.items)item.available=false;
    for(const auto& candidate:candidates) {
        if(candidate.identity.empty())continue;
        auto found=std::find_if(queue.items.begin(),queue.items.end(),[&](const auto& item){return item.candidate.identity==candidate.identity;});
        if(found==queue.items.end()){queue.items.emplace_back();found=std::prev(queue.items.end());}
        auto& item=*found;item.candidate=candidate;item.available=true;
        const auto existing=represented.find(candidate.identity);
        if(existing!=represented.end()&&(item.projectId.empty()||item.projectId!=existing->second.first)) {
            item.projectId=existing->second.first;item.projectPath=existing->second.second;
            item.state=State::Completed;item.error.clear();
        }
        if(item.state==State::Completed&&!item.projectPath.empty()) {
            std::error_code ec;
            if(!std::filesystem::exists(item.projectPath,ec)&&!ec)item.state=State::Excluded;
        }
    }
}
}
