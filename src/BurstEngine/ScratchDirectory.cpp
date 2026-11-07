#include "Storage/ScratchBudget.h"
#include "ScratchDirectory.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace Stack::BurstEngine {
namespace {
constexpr wchar_t Marker[]=L".stack-scratch-owner";
constexpr char Signature[]="Stack bracket scratch v1";
const char* Bucket(ScratchKind kind) { return kind==ScratchKind::Worker?"BurstSessions":"Bracketing"; }
bool Plain(const std::filesystem::path& path) {
    const auto attributes=GetFileAttributesW(path.c_str());
    return attributes!=INVALID_FILE_ATTRIBUTES && !(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
}
bool SessionName(const std::filesystem::path& path) {
    const auto name=path.filename().wstring();
    return name.size()==32 && std::all_of(name.begin(),name.end(),[](wchar_t c) {
        return (c>=L'0' && c<=L'9') || (c>=L'a' && c<=L'f');
    });
}
bool RemoveEntry(const std::filesystem::path& path) {
    // Never traverse directory junctions/symlinks into assets or other storage.
    if(!Plain(path))return false;
    std::error_code ec;
    if(std::filesystem::is_directory(path,ec)) {
        for(std::filesystem::directory_iterator it(path,ec),end;!ec && it!=end;it.increment(ec))
            if(!RemoveEntry(it->path()))return false;
        if(ec)return false;
    }
    return std::filesystem::remove(path,ec) && !ec;
}
bool RemoveOwned(const std::filesystem::path& path,Handle& lock) {
    if(!lock || !Plain(path) || !SessionName(path))return false;
    std::error_code ec;
    for(std::filesystem::directory_iterator it(path,ec),end;!ec && it!=end;it.increment(ec)) {
        if(it->path().filename()==Marker)continue;
        if(!RemoveEntry(it->path()))return false;
    }
    if(ec)return false;
    // Retain the marker if removal fails, so next startup can retry.
    lock.Reset();
    if(!std::filesystem::remove(path/Marker,ec) || ec)return false;
    return std::filesystem::remove(path,ec) && !ec;
}
Handle OpenOwner(const std::filesystem::path& path,DWORD disposition,bool inherit) {
    SECURITY_ATTRIBUTES security{sizeof(security),nullptr,inherit?TRUE:FALSE};
    return Handle(CreateFileW((path/Marker).c_str(),GENERIC_READ|GENERIC_WRITE,0,
        &security,disposition,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
}
}

ScratchDirectory::ScratchDirectory(const std::filesystem::path& cacheRoot,ScratchKind kind) {
    cleanupOnRelease_=kind==ScratchKind::Sources;
    const auto bucket=cacheRoot/Bucket(kind);
    std::filesystem::create_directories(bucket);
    if(!Plain(bucket))throw std::runtime_error("Bracket scratch storage cannot be a linked directory.");
    std::error_code spaceError;
    const auto space=std::filesystem::space(bucket,spaceError);
    if(spaceError || space.available<2*Storage::ScratchBudget::GiB ||
        Storage::ScratchBudget::Total(cacheRoot)>=Storage::ScratchBudget::Limit(cacheRoot))
        throw std::runtime_error(Storage::ScratchBudget::Failure);
    path_=bucket/UniqueId();
    if(!std::filesystem::create_directory(path_))throw std::runtime_error("Could not create a unique bracket scratch folder.");
    lock_=OpenOwner(path_,CREATE_NEW,kind==ScratchKind::Worker);
    DWORD written=0;
    if(!lock_ || !WriteFile(lock_.Get(),Signature,sizeof(Signature),&written,nullptr) || written!=sizeof(Signature)) {
        lock_.Reset();std::error_code ec;std::filesystem::remove(path_/Marker,ec);std::filesystem::remove(path_,ec);
        throw std::runtime_error("Could not lock bracket scratch storage. Check free disk space and write access.");
    }
}
ScratchDirectory::~ScratchDirectory() {
    if(cleanupOnRelease_)try { Cleanup(); } catch(...) { /* Startup recovery retries. */ }
}
void ScratchDirectory::Cleanup() {
    lock_.Reset();
    auto cleanup=OpenOwner(path_,OPEN_EXISTING,false);
    RemoveOwned(path_,cleanup);
}
std::size_t RecoverAbandonedScratch(const std::filesystem::path& cacheRoot) {
    std::size_t removed=0;
    for(auto kind:{ScratchKind::Worker,ScratchKind::Sources}) {
        const auto bucket=cacheRoot/Bucket(kind);
        if(!Plain(bucket))continue;
        std::error_code ec;
        for(std::filesystem::directory_iterator it(bucket,ec),end;!ec && it!=end;it.increment(ec)) {
            const auto path=it->path();
            if(!SessionName(path) || !Plain(path) || !Plain(path/Marker))continue;
            auto lock=OpenOwner(path,OPEN_EXISTING,false);
            if(!lock)continue; // A parent, worker or another sweep owns it.
            char signature[sizeof(Signature)]{};DWORD read=0;
            if(!ReadFile(lock.Get(),signature,sizeof(signature),&read,nullptr) || read!=sizeof(signature) ||
                std::memcmp(signature,Signature,sizeof(signature))!=0)continue;
            if(RemoveOwned(path,lock))++removed;
        }
    }
    return removed;
}
}
