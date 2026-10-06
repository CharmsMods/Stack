#pragma once
#include "Persistence/BracketingProject.h"
#include "Raw/RawLoader.h"
#include "Raw/RawTechnicalEvidence.h"
#include <algorithm>
#include <set>

namespace Stack::Validation {
// Read-only folder diagnostics use the product's grouping rules, without
// requiring the user to save a project or copying their original captures.
inline bool LoadBracketingDiagnosticFolder(const std::filesystem::path& directory,
    Raw::Bracketing::ProcessingRequest& request,std::string& error) {
    Project::RawProjectSnapshot snapshot;Project::MultiFrameSourceSet set;
    std::vector<std::filesystem::path> paths;
    for(const auto& entry:std::filesystem::directory_iterator(directory))
        if(entry.is_regular_file()&&Raw::RawLoader::IsRawPath(entry.path().string())) paths.push_back(entry.path());
    std::sort(paths.begin(),paths.end());std::set<std::string> identities;
    for(const auto& path:paths) {
        const auto identity=RawEvidence::ComputeSourceIdentity(path);
        if(!identity.valid) {error=identity.reason;return false;}
        if(!identities.insert(identity.sha256).second) continue;
        Raw::RawMetadata metadata;
        if(!Raw::RawLoader::LoadMetadata(path.string(),metadata)) {error="Could not read metadata: "+path.filename().string();return false;}
        Project::EmbeddedAssetRecord asset;asset.assetId=identity.sha256;
        asset.captureMetadataSummary=Project::SerializeRawCaptureCompatibilitySummary(
            Project::BuildRawCaptureCompatibilitySummary(metadata));
        snapshot.embeddedAssets.push_back(asset);
        Project::SourceSetFrame frame;frame.frameId=path.filename().string();frame.assetId=asset.assetId;
        set.frames.push_back(frame);
        request.sources.push_back({frame.frameId,identity.sha256,identity.byteSize,path});
    }
    if(request.sources.empty()) {error="The diagnostic folder contains no RAW captures.";return false;}
    request.recipe=Project::SuggestBracketingGroups(snapshot,set);
    for(std::size_t i=0;i<request.recipe.groups.size();++i)
        request.recipe.groups[i].id="diagnostic-group-"+std::to_string(i);
    return true;
}
}
