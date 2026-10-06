#include "BracketingGallery.h"
#include <algorithm>

namespace Stack::Editor {
void DrawBracketingGalleryStacks(const std::vector<RawWorkspace::RawGallerySimilarityStack>& stacks,
    ImVec2 size,float gap,const std::function<const RawWorkspace::SourceRecord*(const std::string&)>& find,
    const std::function<void(const RawWorkspace::SourceRecord&,const std::string&,const std::filesystem::path&,
        const std::vector<std::string>*,std::size_t)>& draw) {
    const int columns=std::max(1,int((ImGui::GetContentRegionAvail().x+gap)/(size.x+gap)));
    int column=0;
    const auto beginTile=[&] {if(column)ImGui::SameLine(0,gap);ImGui::BeginGroup();};
    const auto endTile=[&] {ImGui::EndGroup();column=(column+1)%columns;};
    for(const auto& stack:stacks) {
        if(stack.sourceKeys.empty())continue;
        const auto* first=find(stack.sourceKeys.front());if(!first)continue;
        const auto key=stack.resultProjectId.empty()?stack.sourceKeys.front():stack.resultProjectId;
        ImGui::PushID(key.c_str());
        const auto expandedKey=ImGui::GetID("expanded");
        bool expanded=ImGui::GetStateStorage()->GetBool(expandedKey,false);
        beginTile();
        auto card=*first;
        if(!stack.resultProjectPath.empty()) {
            card.relativePathKey="project-overlay:"+stack.resultProjectId;
            card.fileName=stack.resultProjectName;
            if(!stack.resultCoverPath.empty()){card.thumbnail.absolutePath=stack.resultCoverPath;card.thumbnail.status=RawWorkspace::ThumbnailStatus::Ready;}
        }
        draw(card,card.fileName,stack.resultProjectPath,&stack.sourceKeys,stack.sourceKeys.size());
        if(stack.sourceKeys.size()>1||!stack.resultProjectPath.empty()) {
            if(ImGui::SmallButton(expanded?"Collapse originals":"Expand originals")) {
                expanded=!expanded;ImGui::GetStateStorage()->SetBool(expandedKey,expanded);
            }
        } else ImGui::Dummy({0,ImGui::GetFrameHeight()});
        endTile();
        if(expanded)for(const auto& member:stack.sourceKeys)if(const auto* source=find(member)) {
            ImGui::PushID(member.c_str());beginTile();
            draw(*source,source->fileName,{},nullptr,1);
            ImGui::TextDisabled("Original capture");endTile();ImGui::PopID();
        }
        ImGui::PopID();
    }
}
}
