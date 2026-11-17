#include "Renderer/ViewportSectionStore.h"
#include "Renderer/ViewportSectionIndex.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <limits>
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
std::uint64_t RetainedBytes(const std::vector<float>& pixels, const std::vector<float>& hdr) {
    // Moved vectors retain their allocations, not just their populated values.
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (pixels.capacity() > maximum/sizeof(float) || hdr.capacity() > maximum/sizeof(float)) return maximum;
    const auto a = std::uint64_t(pixels.capacity())*sizeof(float);
    const auto b = std::uint64_t(hdr.capacity())*sizeof(float);
    return a > maximum-b ? maximum : a+b;
}
}
struct ViewportSectionStore::Impl {
    struct Entry { std::vector<float> pixels,hdr; std::uint64_t offset=0; bool onDisk=false,hasHdr=false; };
    ViewportSectionIndex index;
    // Only workers take this lock. It protects the shared file position, disk
    // append offset and write admission, never UI readiness/coverage queries.
    mutable std::mutex ioMutex;
    std::vector<Entry> entries;
    HANDLE file=INVALID_HANDLE_VALUE;
    std::atomic<std::uint64_t> diskBytes{0};
    std::uint64_t ramBytes=0;
    std::uint64_t ramAllowance=RamLimit;
    std::wstring directory;
    Impl(int w,int h):index(w,h),entries(index.Count()) {}
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
ViewportSectionStore::ViewportSectionStore(int width,int height,std::uint64_t ramAllowance)
    :m(std::make_unique<Impl>(width,height)) {
    static_assert(Edge == ViewportSectionIndex::Edge);
    m->ramAllowance=std::min(ramAllowance,RamLimit);
}
ViewportSectionStore::~ViewportSectionStore()=default;
int ViewportSectionStore::Count() const { return m->index.Count(); }
ImageRegion ViewportSectionStore::Bounds(int index) const { return m->index.Bounds(index); }
ImageRegion ViewportSectionStore::Raster(int index) const { return m->index.Raster(index); }
bool ViewportSectionStore::Has(int index) const { return m->index.Has(index); }
bool ViewportSectionStore::Complete() const { return m->index.Complete(); }
bool ViewportSectionStore::Covers(const ImageRegion& visible) const { return m->index.Covers(visible); }
int ViewportSectionStore::NextMissing(const ImageRegion& visible,bool whole) const { return m->index.NextMissing(visible,whole); }
bool ViewportSectionStore::Prefer(int candidate,int current,const ImageRegion& visible) const {
    return m->index.Prefer(candidate,current,visible);
}
bool ViewportSectionStore::Store(int index,std::vector<float> pixels,std::vector<float> hdr,std::string& error) {
    const auto r=Raster(index);const std::size_t values=std::size_t(r.width)*r.height*4;
    if(!r.Valid() || pixels.size()!=values || (!hdr.empty() && hdr.size()!=values)) { error="Invalid native section payload."; return false; }
    std::lock_guard lock(m->ioMutex); auto& entry=m->entries[index];if(m->index.Has(index))return true;
    const std::uint64_t bytes=(pixels.size()+hdr.size())*sizeof(float);
    const std::uint64_t retained=RetainedBytes(pixels,hdr);
    entry.hasHdr=!hdr.empty();
    if(retained<=m->ramAllowance && m->ramBytes<=m->ramAllowance-retained && Reserve(retained)) {
        entry.pixels=std::move(pixels);entry.hdr=std::move(hdr);m->ramBytes+=retained;
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
        entry.onDisk=true;
        m->diskBytes.store(entry.offset+bytes,std::memory_order_release);
    }
    // Publish only after both SDR/HDR payloads are complete. All entry fields
    // are immutable from here until destruction, including RAM vector storage.
    m->index.Publish(index);return true;
}
bool ViewportSectionStore::Load(int index,std::vector<float>& pixels,std::vector<float>& hdr,std::string& error) const {
    if(!m->index.Has(index))return false;
    const auto& entry=m->entries[index];
    if(!entry.onDisk) {pixels=entry.pixels;hdr=entry.hdr;return true;}
    const auto r=Raster(index);const std::size_t values=std::size_t(r.width)*r.height*4;
    pixels.resize(values);hdr.resize(entry.hasHdr ? values : 0);
    std::lock_guard lock(m->ioMutex);
    return m->Transfer(false,entry.offset,pixels.data(),values*sizeof(float),error) &&
        (!entry.hasHdr || m->Transfer(false,entry.offset+values*sizeof(float),hdr.data(),values*sizeof(float),error));
}
std::uint64_t ViewportSectionStore::DiskBytes() const {return m->diskBytes.load(std::memory_order_acquire);}
}
