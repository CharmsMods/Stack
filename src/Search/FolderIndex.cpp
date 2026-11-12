#include "FolderIndexInternal.h"
#include <algorithm>
#include <stdexcept>

namespace Stack::Search {
namespace Detail {
std::wstring Fold(std::wstring value) {
    if(value.empty())return value;
    std::wstring folded(value.size(),L'\0');
    const int count=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,value.data(),
        static_cast<int>(value.size()),folded.data(),static_cast<int>(folded.size()),nullptr,nullptr,0);
    return count?folded:value;
}
std::wstring PathKey(const std::filesystem::path& path) {
    auto value=path.lexically_normal().wstring();
    std::replace(value.begin(),value.end(),L'/',L'\\');
    while(value.size()>3 && value.back()==L'\\')value.pop_back();
    return Fold(std::move(value));
}
bool Within(const std::wstring& path,const std::wstring& root) {
    return !root.empty() && path.compare(0,root.size(),root)==0 &&
        (path.size()==root.size() || root.back()==L'\\' || path[root.size()]==L'\\');
}
}
FolderIndex::Impl::Impl(std::filesystem::path directory):cache(std::move(directory)) {
    wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!wake)throw std::runtime_error("Could not create folder index event");
    try {
        monitor=std::thread([this]{Monitor();});
        queryWorker=std::thread([this]{Query();});
    } catch(...) {
        stopping.store(true);SetEvent(wake);queryWake.notify_all();
        if(monitor.joinable())monitor.join();CloseHandle(wake);throw;
    }
}
FolderIndex::Impl::~Impl() {
    stopping.store(true);++generation;++querySerial;SetEvent(wake);queryWake.notify_all();
    CancelSynchronousIo(monitor.native_handle());
    monitor.join();queryWorker.join();CloseHandle(wake);
}
FolderIndex::FolderIndex(std::filesystem::path cache):impl_(std::make_unique<Impl>(std::move(cache))) {}
FolderIndex::~FolderIndex()=default;
void FolderIndex::SetScope(const std::filesystem::path& folder) {
    if(folder.empty()){Suspend();return;}
    auto path=folder.lexically_normal();auto key=Detail::PathKey(path);
    auto& s=*impl_;bool restart=false;
    {
        std::lock_guard lock(s.mutex);
        if(s.active && s.scope==path)return;
        const bool reuse=Detail::Within(key,s.rootKey) &&
            !(s.snapshot && s.snapshot->partial && key!=s.rootKey);
        if(!reuse){s.root=path;s.rootKey=key;if(s.snapshot)s.retired.push_back(std::move(s.snapshot));}
        if(!s.active || !reuse){++s.generation;s.refreshing=true;restart=true;}
        s.active=true;s.scope=std::move(path);s.query.clear();++s.querySerial;s.ready.store(false);
    }
    if(restart)SetEvent(s.wake);
    s.queryWake.notify_one();
}
std::uint64_t FolderIndex::Search(const std::wstring& name,const std::vector<std::wstring>& extensions) {
    auto& s=*impl_;std::uint64_t id;
    {std::lock_guard lock(s.mutex);s.query=Detail::Fold(name);s.extensions=extensions;id=++s.querySerial;s.ready.store(false);}
    s.queryWake.notify_one();return id;
}
void FolderIndex::Refresh() {
    auto& s=*impl_;
    {std::lock_guard lock(s.mutex);if(!s.active)return;++s.generation;s.refreshing=true;++s.revision;}
    SetEvent(s.wake);s.queryWake.notify_one();
}
void FolderIndex::Suspend() {
    auto& s=*impl_;
    {std::lock_guard lock(s.mutex);if(!s.active)return;s.active=false;++s.generation;++s.querySerial;s.ready.store(false);}
    CancelSynchronousIo(s.monitor.native_handle());SetEvent(s.wake);s.queryWake.notify_one();
}
bool FolderIndex::TryTakeResult(Result& value) {
    auto& s=*impl_;if(!s.ready.load())return false;
    std::lock_guard lock(s.mutex);
    if(!s.ready.exchange(false) || !s.active || s.completion.request!=s.querySerial.load())return false;
    value=std::move(s.completion);return true;
}
void FolderIndex::Impl::Publish(std::uint64_t epoch,std::shared_ptr<const Detail::Snapshot> value,bool busy) {
    {std::lock_guard lock(mutex);if(!Current(epoch) || !active)return;
        snapshot.swap(value);refreshing=busy;++revision;}
    queryWake.notify_one();
}
void FolderIndex::Impl::Query() {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    std::uint64_t seenQuery=0,seenRevision=0;
    while(!stopping.load()) {
        std::unique_lock lock(mutex);
        queryWake.wait(lock,[&]{return stopping.load() || querySerial.load()!=seenQuery || revision!=seenRevision;});
        if(stopping.load())break;
        seenQuery=querySerial.load();seenRevision=revision;
        if(!active || query.empty())continue;
        const auto id=seenQuery,version=seenRevision;
        const auto text=query,scopeKey=Detail::PathKey(scope);
        const auto data=snapshot;const auto allowed=extensions;const bool busy=refreshing;
        const auto prepared=Detail::PrepareName(text);
        lock.unlock();
        Result result;result.request=id;result.refreshing=busy;
        if(data) {
            result.partial=data->partial;result.fromCache=data->fromCache;result.status=data->status;
            struct Match {int score;const Detail::Entry* entry;};
            std::vector<Match> matches;
            const auto rootKey=Detail::PathKey(data->root);
            const auto prefix=scopeKey==rootKey?std::wstring{}:scopeKey.substr(rootKey.size()+(rootKey.back()==L'\\'?0:1))+L"\\";
            for(const auto& entry:data->entries) {
                if(stopping.load() || querySerial.load()!=id)break;
                if(!prefix.empty() && entry.relativeKey.compare(0,prefix.size(),prefix)!=0)continue;
                if(!entry.directory && !allowed.empty()) {
                    const auto dot=entry.name.rfind(L'.');
                    if(dot==std::wstring::npos || std::find(allowed.begin(),allowed.end(),entry.name.substr(dot))==allowed.end())continue;
                }
                const int score=Detail::MatchName(entry.name,entry.prepared,text,prepared);
                if(score>=0)matches.push_back({score,&entry});
            }
            const auto count=std::min<std::size_t>(1000,matches.size());
            std::partial_sort(matches.begin(),matches.begin()+count,matches.end(),[](const auto& a,const auto& b){
                if(a.score!=b.score)return a.score<b.score;
                if(a.entry->name!=b.entry->name)return a.entry->name<b.entry->name;
                return a.entry->relative<b.entry->relative;
            });
            result.partial|=matches.size()>count;
            for(std::size_t i=0;i<count;++i)result.items.push_back({data->root/matches[i].entry->relative,matches[i].entry->directory});
        }
        Result previous;
        lock.lock();
        if(active && !stopping.load() && querySerial.load()==id && revision==version) {
            previous=std::move(completion);
            completion=std::move(result);ready.store(true);
        }
        lock.unlock(); // large retired snapshots/results are released off the UI lock
    }
}
}
