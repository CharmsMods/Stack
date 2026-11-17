#include "Renderer/ViewportSectionStore.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <limits>
#include <stdexcept>
#include <climits>
#include <mutex>

namespace Stack::Renderer {
namespace {
// Shared across projects and both views. Temporary transfer buffers are bounded
// by one section per executing worker, independently of this retained allowance.
std::atomic<std::uint64_t> retainedBytes{0};
constexpr std::uint64_t RamLimit = 256ull * 1024 * 1024;
bool Reserve(std::uint64_t bytes) {
    auto used = retainedBytes.load();
    while (bytes <= RamLimit && used <= RamLimit-bytes)
        if (retainedBytes.compare_exchange_weak(used,used+bytes)) return true;
    return false;
}
bool Intersects(const ImageRegion& a,const ImageRegion& b) {
    return a.x < b.x+b.width && b.x < a.x+a.width && a.y < b.y+b.height && b.y < a.y+a.height;
}
}
struct ViewportSectionStore::Impl {
    struct Entry {
        std::vector<float> pixels,hdr;
        std::uint64_t offset=0,bytes=0;
        // Published once, after both renditions have been stored successfully.
        // UI coverage queries must never take the payload/file-position lock.
        std::atomic<bool> ready{false};
        bool onDisk=false,hasHdr=false;
    };
    int width,height,columns,rows;
    mutable std::mutex mutex;
    std::vector<Entry> entries;
    HANDLE file=INVALID_HANDLE_VALUE;
    std::atomic<std::uint64_t> diskBytes{0};
    std::uint64_t ramBytes=0;
    std::atomic<int> completed{0};
    std::uint64_t ramAllowance=RamLimit;
    std::wstring directory;
    Impl(int w,int h):width(w),height(h),columns((w+Edge-1)/Edge),rows((h+Edge-1)/Edge),entries(std::size_t(columns)*rows) {}
    ~Impl() { if(file!=INVALID_HANDLE_VALUE) CloseHandle(file); retainedBytes.fetch_sub(ramBytes); }
    bool Open(std::string& error) {
        if(file!=INVALID_HANDLE_VALUE) return true;
        wchar_t directory[MAX_PATH+1]{},path[MAX_PATH+1]{};
        const auto length=GetTempPathW(MAX_PATH,directory);
        if(!length || length>=MAX_PATH || !GetTempFileNameW(directory,L"stk",0,path)) {
            error="Cannot create temporary native-image cache."; return false;
        }
        this->directory=directory;
        file=CreateFileW(path,GENERIC_READ|GENERIC_WRITE|DELETE,0,nullptr,OPEN_EXISTING,
            FILE_ATTRIBUTE_TEMPORARY|FILE_FLAG_DELETE_ON_CLOSE,nullptr);
        if(file==INVALID_HANDLE_VALUE) { DeleteFileW(path); error="Cannot open temporary native-image cache."; return false; }
        return true;
    }
    bool Transfer(bool write,std::uint64_t offset,void* data,std::uint64_t bytes,std::string& error) const {
        LARGE_INTEGER position; position.QuadPart=offset;
        if(!SetFilePointerEx(file,position,nullptr,FILE_BEGIN)) { error="Cannot seek native-image cache."; return false; }
        auto* cursor=static_cast<unsigned char*>(data);
        while(bytes) {
            DWORD count=DWORD(std::min<std::uint64_t>(bytes,16u*1024u*1024u)),done=0;
            const bool ok=write ? WriteFile(file,cursor,count,&done,nullptr) : ReadFile(file,cursor,count,&done,nullptr);
            if(!ok || done!=count) { error=write ? "Cannot write native-image cache (check free disk space)." : "Cannot read native-image cache."; return false; }
            cursor+=done; bytes-=done;
        }
        return true;
    }
};
ViewportSectionStore::ViewportSectionStore(int width,int height,std::uint64_t ramAllowance) {
    if(width<=0 || height<=0 || width>1048576 || height>1048576 || std::uint64_t(width)*height>std::uint64_t(INT_MAX))
        throw std::invalid_argument("Invalid native section extent");
    m=std::make_unique<Impl>(width,height);
    m->ramAllowance=std::min(ramAllowance,RamLimit);
}
ViewportSectionStore::~ViewportSectionStore()=default;
int ViewportSectionStore::Count() const { return int(m->entries.size()); }
ImageRegion ViewportSectionStore::Bounds(int index) const {
    if(index<0 || index>=Count()) return {};
    const int x=(index%m->columns)*Edge,y=(index/m->columns)*Edge;
    return {m->width,m->height,x,y,std::min(Edge,m->width-x),std::min(Edge,m->height-y)};
}
ImageRegion ViewportSectionStore::Raster(int index) const {
    auto r=Bounds(index); if(!r.Valid()) return r;
    const int right=std::min(m->width,r.x+r.width+4),bottom=std::min(m->height,r.y+r.height+4);
    r.x=std::max(0,r.x-4);r.y=std::max(0,r.y-4);r.width=right-r.x;r.height=bottom-r.y;return r;
}
bool ViewportSectionStore::Has(int index) const {
    return index>=0 && index<Count() && m->entries[index].ready.load(std::memory_order_acquire);
}
bool ViewportSectionStore::Complete() const {
    return m->completed.load(std::memory_order_acquire)==Count();
}
bool ViewportSectionStore::Covers(const ImageRegion& visible) const {
    if(!visible.Valid() || visible.fullWidth!=m->width || visible.fullHeight!=m->height) return false;
    for(int i=0;i<Count();++i) if(Intersects(Bounds(i),visible) && !Has(i)) return false;
    return true;
}
int ViewportSectionStore::NextMissing(const ImageRegion& visible,bool whole) const {
    int background=-1;
    for(int i=0;i<Count();++i) if(!Has(i)) {
        if(Intersects(Bounds(i),visible)) return i;
        if(background<0) background=i;
    }
    return whole ? background : -1;
}
bool ViewportSectionStore::Store(int index,std::vector<float> pixels,std::vector<float> hdr,std::string& error) {
    const auto r=Raster(index);const std::size_t values=std::size_t(r.width)*r.height*4;
    if(!r.Valid() || pixels.size()!=values || (!hdr.empty() && hdr.size()!=values)) { error="Invalid native section payload."; return false; }
    std::lock_guard lock(m->mutex); auto& entry=m->entries[index];if(entry.ready.load(std::memory_order_acquire))return true;
    const std::uint64_t bytes=(pixels.size()+hdr.size())*sizeof(float);
    entry.hasHdr=!hdr.empty();entry.bytes=bytes;
    if(bytes<=m->ramAllowance && m->ramBytes<=m->ramAllowance-bytes && Reserve(bytes)) {
        entry.pixels=std::move(pixels);entry.hdr=std::move(hdr);m->ramBytes+=bytes;
    } else {
        if(!m->Open(error))return false;
        ULARGE_INTEGER freeBytes{};
        if(!GetDiskFreeSpaceExW(m->directory.c_str(),&freeBytes,nullptr,nullptr) ||
            freeBytes.QuadPart < bytes+256ull*1024*1024) {
            error="Insufficient free disk space for native image cache.";return false;
        }
        entry.offset=m->diskBytes.load(std::memory_order_relaxed);
        if(!m->Transfer(true,entry.offset,pixels.data(),pixels.size()*sizeof(float),error) ||
            (!hdr.empty() && !m->Transfer(true,entry.offset+pixels.size()*sizeof(float),hdr.data(),hdr.size()*sizeof(float),error)))return false;
        entry.onDisk=true;m->diskBytes.fetch_add(bytes,std::memory_order_relaxed);
    }
    entry.ready.store(true,std::memory_order_release);
    m->completed.fetch_add(1,std::memory_order_release);
    return true;
}
bool ViewportSectionStore::Load(int index,std::vector<float>& pixels,std::vector<float>& hdr,std::string& error) const {
    std::lock_guard lock(m->mutex);
    if(!Has(index))return false;
    const auto& entry=m->entries[index];
    if(!entry.onDisk) {pixels=entry.pixels;hdr=entry.hdr;return true;}
    const auto r=Raster(index);const std::size_t values=std::size_t(r.width)*r.height*4;
    pixels.resize(values);hdr.resize(entry.hasHdr ? values : 0);
    return m->Transfer(false,entry.offset,pixels.data(),values*sizeof(float),error) &&
        (!entry.hasHdr || m->Transfer(false,entry.offset+values*sizeof(float),hdr.data(),values*sizeof(float),error));
}
std::uint64_t ViewportSectionStore::DiskBytes() const {
    return m->diskBytes.load(std::memory_order_relaxed);
}
}
