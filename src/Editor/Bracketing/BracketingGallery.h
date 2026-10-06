#pragma once
#include "Raw/RawWorkspace.h"
#include "Raw/RawGallerySimilarity.h"
#include <functional>
#include <imgui.h>
namespace Stack::Editor {
void AddBracketingResultStacks(std::vector<RawWorkspace::RawGallerySimilarityStack>&,
    const RawWorkspace::WorkspaceState&);
void UpdateBracketingGalleryProject(RawWorkspace::WorkspaceState&,const Project::RawProjectSnapshot&,
    const std::filesystem::path&);
void DrawBracketingGalleryStacks(const std::vector<RawWorkspace::RawGallerySimilarityStack>&,
    ImVec2 tileSize,float gap,
    const std::function<const RawWorkspace::SourceRecord*(const std::string&)>&,
    const std::function<void(const RawWorkspace::SourceRecord&,const std::string&,const std::filesystem::path&,
        const std::vector<std::string>*,std::size_t)>&);
}
