#include "Internal.h"
#include "../AlignmentProposalInput.h"
#include "NodeMath/ContractTypes.h"
#include <algorithm>
#include <fstream>

namespace Raw::Bracketing::LearnedAlignment {
bool Generate(ProcessingRequest& request,PreparedDataset& data,std::vector<std::string>& diagnostics,std::string& error) {
    if(!request.recipe.aiAlignment)return true;
    const auto& reference=data.sources[data.origin];
    if(std::none_of(data.sources.begin(),data.sources.end(),[&](const auto& s){return s.enabled&&s.id!=reference.id;}))return true;
    ProcessingTimer timer(data.stageSeconds,"ai_alignment_proposals");
    // Inference ends before native pyramids/search and combining allocate their
    // working sets. Keep proxy memory bounded, independently of source count.
    if(request.memoryBudgetBytes<2ull*1024*1024*1024){error="AI alignment needs at least 2 GiB of processing memory. Turn AI alignment off for this job.";return false;}
    ProxyGeometry geometry;geometry.stride=2;
    do {
        geometry.width=data.width/geometry.stride;geometry.height=data.height/geometry.stride;
        if(std::max(geometry.width,geometry.height)<=1152&&std::uint64_t(geometry.width)*geometry.height<=884736)break;
        geometry.stride+=2;
    }while(geometry.stride<std::min(data.width,data.height));
    if(std::min(geometry.width,geometry.height)<64){error="This image shape is too narrow for AI alignment. Turn AI alignment off.";return false;}
    const auto directory=data.directory/"sea-raft";std::filesystem::create_directories(directory);
    nlohmann::json job={{"version",1},{"profile",Profile},{"modelSha256",ModelSha256},
        {"width",geometry.width},{"height",geometry.height},{"rawWidth",data.width},{"rawHeight",data.height},
        {"originRaw",(geometry.stride-1)*.5},{"stepRaw",geometry.stride},
        {"reference",data.sources[data.origin].id},{"sources",nlohmann::json::array()},
        {"memoryBudgetBytes",request.memoryBudgetBytes}};
    for(std::size_t i=0;i<data.sources.size();++i) {
        const auto& source=data.sources[i];if(!source.enabled&&i!=data.origin)continue;
        if(request.reportProgress)request.reportProgress(.10,"Preparing AI alignment guides");
        const auto path=directory/("capture-"+std::to_string(i)+".rgb");
        if(!WriteProxy(request,data,source,geometry,path,error))return false;
        const auto input=std::find_if(request.sources.begin(),request.sources.end(),[&](const auto& s){return s.frameId==source.id;});
        if(input==request.sources.end()){error="AI alignment source identity is missing.";return false;}
        job["sources"].push_back({{"frameId",source.id},{"sha256",input->sha256},{"proxy",path.filename().u8string()}});
    }
    const auto path=directory/"job.json";
    {std::ofstream stream(path,std::ios::binary);stream<<job.dump();stream.close();if(!stream){error="Could not write the AI alignment request.";return false;}}
    if(request.reportProgress)request.reportProgress(.10,"Estimating motion with SEA-RAFT");
    if(!RunModel(request,path,error))return false;
    const auto response=directory/"result.json";
    if(!std::filesystem::exists(response)||std::filesystem::file_size(response)>1024*1024){error="AI alignment returned no bounded result.";return false;}
    std::ifstream stream(response);const auto result=nlohmann::json::parse(stream);
    if(!result.value("success",false)){error="AI alignment: "+result.value("error",std::string("model execution failed"));return false;}
    if(result.at("profile")!=Profile||result.at("modelSha256")!=ModelSha256){error="AI alignment model identity differs.";return false;}
    if(result.value("device",std::string())!="cuda") {
        error="AI alignment requires CUDA inference; repair the optional SEA-RAFT runtime.";return false;
    }
    const auto& proposals=result.at("proposals");
    if(!proposals.is_array()||proposals.size()+1!=job["sources"].size()){error="AI alignment returned incomplete capture pairs.";return false;}
    for(const auto& value:proposals) {
        AlignmentProposal p;
        p.referenceFrameId=value.at("referenceFrameId");p.sourceFrameId=value.at("sourceFrameId");
        p.referenceSha256=value.at("referenceSha256");p.sourceSha256=value.at("sourceSha256");
        p.modelIdentity=Profile;p.width=geometry.width;p.height=geometry.height;p.rawWidth=data.width;p.rawHeight=data.height;
        p.originRaw=(geometry.stride-1)*.5;p.stepRaw=geometry.stride;
        // The adapter returns filenames only; assets stay in this job's cache.
        const auto forward=std::filesystem::u8path(value.at("forwardFile").get<std::string>());
        const auto reverse=std::filesystem::u8path(value.at("reverseFile").get<std::string>());
        if(forward.has_parent_path()||reverse.has_parent_path()||forward.empty()||reverse.empty()){error="Invalid AI flow location.";return false;}
        p.forwardPath=directory/forward;p.reversePath=directory/reverse;
        p.forwardSha256=value.at("forwardSha256");p.reverseSha256=value.at("reverseSha256");
        request.alignmentProposals.push_back(std::move(p));
    }
    if(!ValidateAlignmentProposals(request,error))return false;
    nlohmann::json identities=nlohmann::json::array();
    for(const auto& p:request.alignmentProposals)identities.push_back(AlignmentProposalIdentity(p));
    data.motionIdentity=Stack::NodeMath::Sha256ContentIdentity(identities.dump());
    diagnostics.push_back("AI alignment: SEA-RAFT-M, "+result.value("device",std::string("unknown"))+", "+
        std::to_string(request.alignmentProposals.size())+" bidirectional proposals; native RAW verification retained.");
    return true;
}
}
