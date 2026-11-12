#include "FolderIndexInternal.h"
#include <algorithm>
#include <array>

namespace Stack::Search {
namespace {
// The monitor alone owns the overlapped buffer/handle. Cancellation drains before
// either is destroyed; UI suspension only changes generation and signals wake.
class Watch {
public:
    HANDLE handle=INVALID_HANDLE_VALUE,event=nullptr;
    OVERLAPPED operation{};
    alignas(DWORD) std::array<unsigned char,32768> buffer{};
    bool pending=false;
    explicit Watch(const std::filesystem::path& root) {
        event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        handle=CreateFileW(root.c_str(),FILE_LIST_DIRECTORY,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS|FILE_FLAG_OVERLAPPED,nullptr);
        operation.hEvent=event;
    }
    bool Arm() {
        if(handle==INVALID_HANDLE_VALUE || !event)return false;
        ResetEvent(event);
        pending=ReadDirectoryChangesW(handle,buffer.data(),static_cast<DWORD>(buffer.size()),TRUE,
            FILE_NOTIFY_CHANGE_DIR_NAME|FILE_NOTIFY_CHANGE_FILE_NAME,nullptr,&operation,nullptr)!=FALSE;
        return pending;
    }
    bool Complete(const std::filesystem::path& root,const std::filesystem::path& cache) {
        DWORD bytes=0;const bool ok=GetOverlappedResult(handle,&operation,&bytes,FALSE)!=FALSE;
        pending=false;
        if(!ok || !bytes)return true; // lost/overflowed notifications require reconciliation
        const auto excluded=Detail::PathKey(cache);
        for(DWORD offset=0;offset<bytes;) {
            if(bytes-offset<offsetof(FILE_NOTIFY_INFORMATION,FileName))return true;
            const auto* info=reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data()+offset);
            if(info->FileNameLength>bytes-offset- offsetof(FILE_NOTIFY_INFORMATION,FileName))return true;
            const auto path=root/std::wstring(info->FileName,info->FileNameLength/sizeof(wchar_t));
            if(!Detail::Within(Detail::PathKey(path),excluded))return true;
            if(!info->NextEntryOffset)break;
            if(info->NextEntryOffset>bytes-offset || info->NextEntryOffset<offsetof(FILE_NOTIFY_INFORMATION,FileName))return true;
            offset+=info->NextEntryOffset;
        }
        return false; // publishing our own disposable cache must not invalidate it
    }
    ~Watch() {
        if(pending){CancelIoEx(handle,&operation);DWORD bytes=0;GetOverlappedResult(handle,&operation,&bytes,TRUE);}
        if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);
        if(event)CloseHandle(event);
    }
};
struct FindHandle {HANDLE value;~FindHandle(){if(value!=INVALID_HANDLE_VALUE)FindClose(value);}};
}
std::shared_ptr<Detail::Snapshot> FolderIndex::Impl::Scan(const std::filesystem::path& path,std::uint64_t epoch) {
    auto data=std::make_shared<Detail::Snapshot>();data->root=path;
    std::vector<std::filesystem::path> pending{std::filesystem::path{}};
    std::size_t inspected=0,textUnits=path.native().size();
    const auto cacheKey=Detail::PathKey(cache);
    while(!pending.empty() && Current(epoch)) {
        auto relative=std::move(pending.back());pending.pop_back();
        WIN32_FIND_DATAW entry{};
        const auto pattern=path/relative/L"*";
        FindHandle find{FindFirstFileExW(pattern.c_str(),FindExInfoBasic,&entry,FindExSearchNameMatch,nullptr,FIND_FIRST_EX_LARGE_FETCH)};
        if(find.value==INVALID_HANDLE_VALUE) {
            if(GetLastError()!=ERROR_FILE_NOT_FOUND){data->partial=true;data->status="Some folders are unavailable or cannot be read.";}
            continue;
        }
        do {
            if(!Current(epoch))return {};
            if(++inspected>2000000){data->partial=true;data->status="Index limit reached. Browse a smaller folder to search more precisely.";pending.clear();break;}
            if(!wcscmp(entry.cFileName,L".") || !wcscmp(entry.cFileName,L".."))continue;
            auto child=relative/entry.cFileName;
            if((entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) && Detail::PathKey(path/child)==cacheKey)continue;
            textUnits+=child.native().size();
            if(data->entries.size()>=Detail::MaxEntries || textUnits>Detail::MaxTextUnits || child.native().size()>32768) {
                data->partial=true;data->status="Index limit reached. Browse a smaller folder to search more precisely.";pending.clear();break;
            }
            const bool directory=(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0;
            auto name=Detail::Fold(entry.cFileName);
            data->entries.push_back({child.wstring(),name,directory,Detail::PrepareName(name),Detail::Fold(child.wstring())});
            // Show junctions/placeholders themselves, but never follow loops or hydrate them.
            if(directory && !(entry.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_OFFLINE)))pending.push_back(std::move(child));
        } while(FindNextFileW(find.value,&entry));
        const auto error=GetLastError();
        if(error!=ERROR_NO_MORE_FILES && error!=ERROR_SUCCESS && !data->partial) {
            data->partial=true;data->status="Some folders could not be completely indexed.";
        }
    }
    if(!Current(epoch))return {};
    std::sort(data->entries.begin(),data->entries.end(),[](const auto& a,const auto& b){return a.name==b.name?a.relative<b.relative:a.name<b.name;});
    return data;
}
void FolderIndex::Impl::Monitor() {
    SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    DWORD previousMode=0;SetThreadErrorMode(SEM_FAILCRITICALERRORS|SEM_NOOPENFILEERRORBOX,&previousMode);
    while(!stopping.load()) {
        WaitForSingleObject(wake,INFINITE);
        if(stopping.load())break;
        std::filesystem::path path;std::uint64_t epoch;
        std::vector<std::shared_ptr<const Detail::Snapshot>> garbage;
        {std::lock_guard lock(mutex);garbage.swap(retired);if(!active)continue;path=root;epoch=generation.load();}
        garbage.clear();
        try {
            Watch watch(path);bool watching=watch.Arm();
            std::shared_ptr<const Detail::Snapshot> cached;
            {std::lock_guard lock(mutex);cached=snapshot;}
            if(!cached)cached=Detail::Load(cache,path);
            if(cached && Current(epoch))Publish(epoch,cached,true);
            bool scan=true;
            while(Current(epoch)) {
                if(scan) {
                    auto data=Scan(path,epoch);if(!data)break;
                    if(!watching){data->partial=true;data->status="Live updates unavailable. Refresh this folder to update search.";}
                    // A notification during enumeration means this snapshot is provisional.
                    const bool changed=watching && WaitForSingleObject(watch.event,0)==WAIT_OBJECT_0;
                    Publish(epoch,data,changed);
                    if(!changed && Current(epoch))Detail::Save(cache,*data);
                    scan=false;
                }
                HANDLE events[]={wake,watch.event};
                const auto result=WaitForMultipleObjects(watching?2:1,events,FALSE,INFINITE);
                if(result==WAIT_OBJECT_0) {
                    // A scope/refresh/suspend signal is consumed here: arrange the
                    // next outer iteration without losing an already newer request.
                    if(!Current(epoch)){SetEvent(wake);break;}
                    continue;
                }
                if(result==WAIT_OBJECT_0+1 && watching) {
                    const bool changed=watch.Complete(path,cache);watching=watch.Arm();
                    if(!changed && watching)continue;
                    {std::lock_guard lock(mutex);if(Current(epoch)){refreshing=true;++revision;}}
                    queryWake.notify_one();
                    // Coalesce rename/create bursts. Queries continue on the immutable
                    // snapshot; no repeated scans occur when the filesystem is quiet.
                    if(WaitForSingleObject(wake,400)==WAIT_OBJECT_0 && !Current(epoch)){SetEvent(wake);break;}
                    scan=true;
                }
            }
            if(!Current(epoch) && !stopping.load())SetEvent(wake);
        } catch(const std::exception&) {
            auto failed=std::make_shared<Detail::Snapshot>();failed->root=path;failed->partial=true;
            failed->status="Search index unavailable. Refresh to retry.";Publish(epoch,failed,false);
        }
    }
    SetThreadErrorMode(previousMode,nullptr);
}
}
