#pragma once
#include "Persistence/RawProjectModel.h"
#include "Raw/RawWorkspace.h"
#include "Raw/RawGallerySimilarity.h"
#include <filesystem>
#include <map>

namespace Stack::AutoBracket {
enum class State { Pending, Running, Completed, Attention, Failed, Excluded };
const char* StateName(State);
struct Source {
    std::filesystem::path path;
    std::string fingerprint;
    std::uint64_t bytes=0;
};
struct Candidate {
    std::string identity,name;
    std::vector<Source> sources;
};
struct Item {
    Candidate candidate;
    State state=State::Pending;
    std::string projectId,error;
    std::filesystem::path projectPath;
    bool available=true;
};
struct Queue {
    std::filesystem::path root;
    bool paused=false;
    bool resumeWhenIdle=false;
    std::vector<Item> items;
};
std::string SourceIdentity(const std::vector<Source>&);
std::vector<Candidate> CollectCandidates(const std::vector<RawWorkspace::SourceRecord>&,
    const std::vector<RawWorkspace::RawGallerySimilarityStack>&);
bool LoadQueue(const std::filesystem::path& root,Queue&,std::string& error);
bool SaveQueue(const Queue&,std::string& error);
// Existing projects are authoritative; a match must never create a second project.
void Reconcile(Queue&,const std::vector<Candidate>&,
    const std::map<std::string,std::pair<std::string,std::filesystem::path>>& represented);
}
