#include "GalleryFilesystemBrowserState.h"
#include "Async/TaskSystem.h"
#include <algorithm>
#include <cwctype>
#include <cstdio>
#include <map>
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>

namespace Stack::Editor::RawLabInternal {
std::wstring GalleryFilesystemBrowser::State::Key(const std::filesystem::path& path) {
    auto key=path.lexically_normal().wstring();
    std::replace(key.begin(),key.end(),L'/',L'\\');
    while(key.size()>3 && key.back()==L'\\')key.pop_back();
    std::transform(key.begin(),key.end(),key.begin(),::towlower);
    return key;
}
std::shared_ptr<GalleryFilesystemBrowser::State::Node> GalleryFilesystemBrowser::State::Make(
    const std::filesystem::path& path,const std::string& name,Kind kind) {
    auto node=std::make_shared<Node>();node->path=path;node->key=Key(path);node->kind=kind;
    node->label=name.empty()?path.filename().u8string():name;
    if(node->label.empty())node->label=path.u8string();
    node->sortKey=Key(std::filesystem::u8path(node->label));
    unsigned hash=2166136261u;
    for(const auto ch:node->key)hash=(hash^static_cast<unsigned>(ch))*16777619u;
    node->branch=hash%6;
    return node;
}
void GalleryFilesystemBrowser::State::Merge(std::vector<std::shared_ptr<Node>>& existing,
    std::vector<std::shared_ptr<Node>> incoming) {
    std::map<std::wstring,std::shared_ptr<Node>> retained;
    for(auto& node:existing)retained.emplace(node->key,node);
    for(auto& node:incoming)if(const auto found=retained.find(node->key);found!=retained.end()) {
        // Keep identity, expansion, cached children and pending requests stable.
        found->second->label=std::move(node->label);found->second->kind=node->kind;
        node=found->second;
    }
    existing=std::move(incoming);
}
void GalleryFilesystemBrowser::State::Request(const std::shared_ptr<State>& self,const std::shared_ptr<Node>& node) {
    if(node->pending)return;
    node->pending=true;node->error.clear();dirty=true;
    // Capture immutable inputs, not UI-owned fields that a refresh may change.
    const auto path=node->path;const auto branch=node->branch;const bool inherit=node->kind==Kind::Folder;
    if(!Async::TaskSystem::Get().Submit("Listing folders",[self,node,path,branch,inherit] {
        Result result;result.node=node;
        try {
            std::error_code error;
            std::filesystem::directory_iterator it(path,error),end;
            while(!error && it!=end) {
                std::error_code itemError;
                if(it->is_directory(itemError)) {
                    auto child=Make(it->path());if(inherit)child->branch=branch;
                    result.children.push_back(std::move(child));
                }
                it.increment(error);
            }
            if(error)result.error="Folder unavailable or access denied. Right-click to retry.";
            std::sort(result.children.begin(),result.children.end(),[](const auto& a,const auto& b) {
                return a->sortKey<b->sortKey;
            });
        } catch(const std::exception&) {result.error="Could not read this folder. Right-click to retry.";}
        {std::lock_guard<std::mutex> lock(self->mutex);self->results.push_back(std::move(result));}
        self->ready.store(true);
    })) {node->pending=false;node->error="Could not list folders. Right-click to retry.";}
}
void GalleryFilesystemBrowser::State::Start(const std::shared_ptr<State>& self) {
    if(started || rootsPending)return;
    started=true;rootsPending=true;locationsError.clear();
    if(!Async::TaskSystem::Get().Submit("Finding computer folders",[self] {
        Result result;
        try {
            for(const auto& item:{std::pair{&FOLDERID_Desktop,"Desktop"},std::pair{&FOLDERID_Pictures,"Pictures"}}) {
                PWSTR path=nullptr;
                if(SUCCEEDED(SHGetKnownFolderPath(*item.first,KF_FLAG_DONT_VERIFY,nullptr,&path))) {
                    const std::filesystem::path location(path);CoTaskMemFree(path);
                    result.children.push_back(Make(location,item.second,
                        item.first==&FOLDERID_Desktop?Kind::Desktop:Kind::Pictures));
                }
            }
            const DWORD drives=GetLogicalDrives();
            for(int index=0;index<26;++index)if(drives&(1u<<index)) {
                const std::wstring path={wchar_t(L'A'+index),L':',L'\\'};
                const auto type=GetDriveTypeW(path.c_str());
                result.children.push_back(Make(path,{},type==DRIVE_REMOTE?Kind::Network:
                    type==DRIVE_REMOVABLE?Kind::Removable:Kind::Drive));
            }
        } catch(const std::exception&) {result.error="Could not load locations. Use Refresh locations to retry.";}
        {std::lock_guard<std::mutex> lock(self->mutex);self->results.push_back(std::move(result));}
        self->ready.store(true);
    })) {rootsPending=false;locationsError="Could not load locations. Use Refresh locations to retry.";}
}
void GalleryFilesystemBrowser::State::Reveal(const std::shared_ptr<State>& self) {
    bool waiting=rootsPending;
    const auto walk=[&](const auto& recurse,const std::vector<std::shared_ptr<Node>>& nodes)->void {
        for(const auto& node:nodes) {
            const auto& path=node->key;
            if(currentKey.size()<=path.size() || currentKey.compare(0,path.size(),path)!=0)continue;
            if(path.back()!=L'\\' && currentKey[path.size()]!=L'\\')continue;
            if(!node->expanded){node->expanded=true;dirty=true;}
            if(!node->loaded && node->error.empty())Request(self,node);
            if(node->pending)waiting=true;
            else recurse(recurse,node->children);
        }
    };
    // Prefer the closest location alias (Pictures rather than also expanding
    // C:/Users/...). Do not unfold two copies of the same ancestry.
    std::shared_ptr<Node> closest;
    for(const auto& node:roots) {
        const auto& path=node->key;
        const bool contains=currentKey==path || (currentKey.size()>path.size() &&
            currentKey.compare(0,path.size(),path)==0 && (path.back()==L'\\' || currentKey[path.size()]==L'\\'));
        if(contains && (!closest || path.size()>closest->key.size()))closest=node;
    }
    if(closest)walk(walk,std::vector<std::shared_ptr<Node>>{closest});
    revealRequested=waiting;
}
void GalleryFilesystemBrowser::State::Pump(const std::shared_ptr<State>& self) {
    if(ready.exchange(false)) {
        std::vector<Result> completed;
        {std::lock_guard<std::mutex> lock(mutex);completed.swap(results);}
        for(auto& result:completed) {
            if(!result.node) {
                rootsPending=false;
                if(result.error.empty())Merge(roots,std::move(result.children));
                locationsError=std::move(result.error);
            } else {
                auto& n=*result.node;
                // A failed refresh must not replace a useful cached subtree.
                if(result.error.empty())Merge(n.children,std::move(result.children));
                n.error=std::move(result.error);n.loaded=true;n.pending=false;
            }
        }
        dirty=true;
    }
    // Completion may continue an explicit reveal, but must never start one.
    if(revealRequested && dirty)Reveal(self);
    if(!dirty)return;dirty=false;rows.clear();
    const auto match=[&](const auto& recurse,const std::vector<std::shared_ptr<Node>>& nodes)->bool {
        bool any=false;
        for(const auto& node:nodes) {
            const bool descendant=recurse(recurse,node->children);
            node->matches=filterKey.empty() || node->sortKey.find(filterKey)!=std::wstring::npos || descendant;
            any|=node->matches;
        }
        return any;
    };
    if(!filterKey.empty())match(match,roots);
    const auto append=[&](const auto& recurse,const std::vector<std::shared_ptr<Node>>& nodes,int depth)->void {
        for(const auto& node:nodes) {
            if(!filterKey.empty() && !node->matches)continue;
            rows.push_back({node,depth});
            if(node->expanded || !filterKey.empty()) {
                if(node->pending || !node->error.empty() || (node->loaded && node->children.empty()))
                    rows.push_back({node,depth+1,true});
                recurse(recurse,node->children,depth+1);
            }
        }
    };
    append(append,roots,0);
    restoreRow.reset();
    for(std::size_t index=0;index<rows.size();++index) {
        if(scrollToCurrent && !rows[index].message && rows[index].node->key==currentKey) {
            restoreRow=static_cast<float>(index);scrollToCurrent=false;break;
        }
        if(rows[index].node==scrollAnchor && rows[index].message==anchorMessage)restoreRow=static_cast<float>(index)+anchorOffset;
    }
    if(!revealRequested)scrollToCurrent=false;
}
void GalleryFilesystemBrowser::State::Navigate(const std::filesystem::path& path) {
    current=path;currentKey=Key(path);revealRequested=true;scrollToCurrent=true;dirty=true;
    // Clicking a visible row must keep it under the pointer, not align it to
    // the top of the tree. External/back navigation still reveals its target.
    for(int index=visibleFirst;index<=visibleLast && index<static_cast<int>(rows.size());++index)
        if(!rows[index].message && rows[index].node->key==currentKey) {
            revealRequested=false;scrollToCurrent=false;break;
        }
    if(requestedHistory && *requestedHistory<history.size() && Key(history[*requestedHistory])==currentKey)
        historyIndex=*requestedHistory;
    requestedHistory.reset();
    if(!path.empty() && (history.empty() || Key(history[historyIndex])!=currentKey)) {
        if(!history.empty())history.resize(historyIndex+1);
        history.push_back(path);historyIndex=history.size()-1;
    }
    const auto text=path.u8string();std::snprintf(address,sizeof(address),"%s",text.c_str());
    ancestors.clear();ancestorLabels.clear();
    for(auto parent=path;!parent.empty();) {
        ancestors.push_back(parent);
        ancestorLabels.push_back((parent.filename().empty()?parent:parent.filename()).u8string());
        const auto next=parent.parent_path();if(next==parent)break;parent=next;
    }
    std::reverse(ancestors.begin(),ancestors.end());std::reverse(ancestorLabels.begin(),ancestorLabels.end());
}
void GalleryFilesystemBrowser::State::FilterChanged() {
    filterKey=Key(std::filesystem::u8path(filter));dirty=true;
}
void GalleryFilesystemBrowser::State::Collapse() {
    const auto collapse=[&](const auto& recurse,const std::vector<std::shared_ptr<Node>>& nodes)->void {
        for(const auto& node:nodes){node->expanded=false;recurse(recurse,node->children);}
    };
    collapse(collapse,roots);filter[0]=0;FilterChanged();revealRequested=false;scrollToCurrent=false;dirty=true;
}
}
