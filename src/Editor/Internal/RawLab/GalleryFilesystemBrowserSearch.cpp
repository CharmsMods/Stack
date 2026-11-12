#include "GalleryFilesystemBrowserState.h"
#include "GalleryBrowserImageTypes.h"

namespace Stack::Editor::RawLabInternal {
void GalleryFilesystemBrowser::State::SearchChanged() {
    if(searchIndex)searchRequest=searchIndex->Search(L"");
    searchKey=std::filesystem::u8path(search).wstring();
    searchResults.clear();selection.reset();searchLimited=false;searchPending=false;
    searchRefreshing=false;searchStatus.clear();
    searchScheduled=!searchKey.empty();searchAt=ImGui::GetTime()+.12;resetGridScroll=true;
    if(current.empty()) {
        const auto folded=Key(std::filesystem::u8path(search));
        for(const auto& drive:drives)if(drive->sortKey.find(folded)!=std::wstring::npos)searchResults.push_back(drive);
        searchScheduled=false;
    }
}
void GalleryFilesystemBrowser::State::PumpSearch(const std::shared_ptr<State>&) {
    if(closed.load() || !searchIndex)return;
    if(searchScheduled && ImGui::GetTime()>=searchAt) {
        searchScheduled=false;searchPending=true;searchRequest=searchIndex->Search(searchKey,BrowserImageExtensions());
    }
    Search::FolderIndex::Result result;
    if(!searchIndex->TryTakeResult(result) || result.request!=searchRequest || searchKey.empty())return;
    std::vector<std::shared_ptr<Node>> found;found.reserve(result.items.size());
    for(const auto& item:result.items)found.push_back(Make(item.path,{},item.directory?Kind::Folder:Kind::Image));
    // Search-only nodes must not accumulate in the tree's persistent listing map.
    // Keep unchanged hits (and their previews) when the index refreshes.
    std::unordered_map<std::wstring,std::shared_ptr<Node>> previous;
    for(const auto& node:searchResults)previous.emplace(node->key,node);
    for(auto& node:found)if(const auto old=previous.find(node->key);old!=previous.end())node=old->second;
    searchResults=std::move(found);searchLimited=result.partial;searchRefreshing=result.refreshing;
    searchStatus=std::move(result.status);searchPending=false;
}
}
