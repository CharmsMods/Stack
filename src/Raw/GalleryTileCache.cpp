#include "GalleryTileCache.h"
#include "App/AppPaths.h"
#include "NodeMath/ContractTypes.h"
#include "ThirdParty/stb_image.h"
#include "ThirdParty/stb_image_write.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace Stack::GalleryTileCache {
namespace {
constexpr std::uint64_t MiB = 1024ull*1024, Reserve = 2ull*1024*MiB;
constexpr std::size_t MaxEntries = 30000;
struct Entry { std::uint64_t bytes; std::filesystem::file_time_type used; };
struct Cache {
    std::mutex mutex;
    std::filesystem::path root;
    std::unordered_map<std::string,Entry> index;
    std::atomic<std::uint64_t> bytes{0},limit{1024*MiB},hits{0},misses{0};
    std::atomic<std::size_t> count{0};
    std::atomic<bool> ready{false},writable{false};
    HANDLE ownership = INVALID_HANDLE_VALUE;
    ~Cache() { if(ownership!=INVALID_HANDLE_VALUE) CloseHandle(ownership); }
};
Cache& State() { static Cache cache; return cache; }
bool Plain(const std::filesystem::path& path) {
    const auto a=GetFileAttributesW(path.c_str());
    return a!=INVALID_FILE_ATTRIBUTES && !(a&FILE_ATTRIBUTE_REPARSE_POINT);
}
bool KeyValid(const std::string& key) {
    return key.size()==64 && std::all_of(key.begin(),key.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
bool Remove(Cache& cache, const std::string& key) {
    const auto found=cache.index.find(key);
    if(found==cache.index.end())return true;
    std::error_code ec;
    const auto path=cache.root/(key+".png");
    const bool exists=std::filesystem::exists(path,ec);
    if(ec || (exists && (!Plain(path) || !std::filesystem::remove(path,ec))) || ec)return false;
    cache.bytes-=found->second.bytes;
    cache.index.erase(found);cache.count=cache.index.size();return true;
}
bool Trim(Cache& cache,std::uint64_t incoming=0) {
    while(cache.bytes+incoming>cache.limit || cache.index.size()>=MaxEntries) {
        auto oldest=cache.index.end();
        for(auto it=cache.index.begin();it!=cache.index.end();++it)
            if(oldest==cache.index.end()||it->second.used<oldest->second.used)oldest=it;
        if(oldest==cache.index.end() || !Remove(cache,oldest->first))return false;
    }
    return true;
}
void InitializeLocked(Cache& cache) {
    if(cache.ready)return;
    cache.root=AppPaths::GetCacheDirectory()/"GalleryTiles-v1";
    std::error_code ec;
    std::filesystem::create_directories(cache.root,ec);
    if(ec || !Plain(cache.root)) {cache.ready=true;return;}
    // A second app instance bypasses the cache rather than racing its budget.
    cache.ownership=CreateFileW((cache.root/L"owner.lock").c_str(),GENERIC_READ|GENERIC_WRITE,
        0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(cache.ownership==INVALID_HANDLE_VALUE) {cache.ready=true;return;}
    unsigned mib=1024;
    std::ifstream settings(cache.root/"limit.txt");
    if(settings>>mib)cache.limit=std::min(mib,2048u)*MiB;
    for(std::filesystem::directory_iterator it(cache.root,ec),end;!ec&&it!=end;it.increment(ec)) {
        const auto path=it->path();
        if(!Plain(path)||!it->is_regular_file(ec))continue;
        if(path.extension()==".tmp" && KeyValid(path.stem().string())) {std::filesystem::remove(path,ec);ec.clear();continue;}
        if(path.extension()!=".png"||!KeyValid(path.stem().string()))continue;
        auto bytes=it->file_size(ec);if(ec){ec.clear();continue;}
        auto time=it->last_write_time(ec);if(ec){ec.clear();continue;}
        cache.index.emplace(path.stem().string(),Entry{bytes,time});cache.bytes+=bytes;
    }
    cache.count=cache.index.size();cache.writable=!ec;
    Trim(cache);cache.ready=true;
}
}
Snapshot Status() {
    auto& c=State();return {c.bytes.load(),c.limit.load(),c.hits.load(),c.misses.load(),c.count.load(),c.ready.load(),c.writable.load()};
}
void Initialize() {auto& c=State();std::lock_guard lock(c.mutex);InitializeLocked(c);}
void SetLimitMiB(unsigned mib) {
    auto& c=State();std::lock_guard lock(c.mutex);InitializeLocked(c);
    if(!c.writable)return;
    c.limit=std::min(mib,2048u)*MiB;Trim(c);
    std::ofstream(c.root/"limit.txt",std::ios::trunc)<<std::min(mib,2048u);
}
void Clear() {
    auto& c=State();std::lock_guard lock(c.mutex);InitializeLocked(c);if(!c.writable)return;
    std::vector<std::string> keys;keys.reserve(c.index.size());for(const auto& entry:c.index)keys.push_back(entry.first);
    for(const auto& key:keys)Remove(c,key);
}
std::string Key(const std::filesystem::path& source,std::uint64_t bytes,std::int64_t modified,int edge) {
    // Browse identity is deliberately metadata-based. Authoritative RAW loads
    // still verify full source content; cache pixels never establish identity.
    return NodeMath::Sha256ContentIdentity(source.lexically_normal().generic_u8string()+"|"+
        std::to_string(bytes)+"|"+std::to_string(modified)+"|"+std::to_string(edge)+"|tile-v1").substr(7);
}
bool Read(const std::string& key,std::vector<unsigned char>& pixels,int& width,int& height) try {
    auto& c=State();std::vector<unsigned char> encoded;
    {
        std::lock_guard lock(c.mutex);InitializeLocked(c);
        auto found=c.index.find(key);
        if(!c.writable||found==c.index.end()||found->second.bytes>4*MiB){++c.misses;return false;}
        const auto path=c.root/(key+".png");std::ifstream input(path,std::ios::binary);
        encoded.resize(static_cast<std::size_t>(found->second.bytes));
        if(!input.read(reinterpret_cast<char*>(encoded.data()),encoded.size())) {input.close();Remove(c,key);++c.misses;return false;}
        const auto now=std::filesystem::file_time_type::clock::now();
        if(now-found->second.used>std::chrono::hours(1)){std::error_code ec;std::filesystem::last_write_time(path,now,ec);}
        found->second.used=now;
    }
    const auto corrupt=[&]{std::lock_guard lock(c.mutex);Remove(c,key);++c.misses;return false;};
    int w=0,h=0,channels=0;stbi_set_flip_vertically_on_load_thread(0);
    if(!stbi_info_from_memory(encoded.data(),int(encoded.size()),&w,&h,&channels)||w<=0||h<=0||w>512||h>512)return corrupt();
    auto* decoded=stbi_load_from_memory(encoded.data(),int(encoded.size()),&w,&h,&channels,4);
    if(!decoded)return corrupt();
    pixels.assign(decoded,decoded+std::size_t(w)*h*4);stbi_image_free(decoded);width=w;height=h;++c.hits;return true;
} catch (...) { return false; }
void Write(const std::string& key,const std::vector<unsigned char>& pixels,int width,int height) try {
    if(!KeyValid(key)||width<=0||height<=0||width>512||height>512||pixels.size()!=std::size_t(width)*height*4)return;
    auto& c=State();if(!c.limit)return;
    std::vector<unsigned char> encoded;
    const auto append=[](void* context,void* data,int size){auto& out=*static_cast<std::vector<unsigned char>*>(context);auto* p=static_cast<unsigned char*>(data);out.insert(out.end(),p,p+size);};
    if(!stbi_write_png_to_func(append,&encoded,width,height,4,pixels.data(),width*4)||encoded.size()>4*MiB)return;
    std::lock_guard lock(c.mutex);InitializeLocked(c);
    if(!c.writable||!c.limit||c.index.count(key))return;
    std::error_code ec;const auto space=std::filesystem::space(c.root,ec);
    if(ec||space.available<Reserve+encoded.size()||!Trim(c,encoded.size()))return;
    const auto temporary=c.root/(key+".tmp"),target=c.root/(key+".png");
    std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
    out.write(reinterpret_cast<const char*>(encoded.data()),encoded.size());out.close();
    if(!out){std::filesystem::remove(temporary,ec);return;}
    std::filesystem::rename(temporary,target,ec);
    if(ec){std::filesystem::remove(temporary,ec);return;}
    c.index.emplace(key,Entry{encoded.size(),std::filesystem::file_time_type::clock::now()});
    c.bytes+=encoded.size();c.count=c.index.size();
} catch (...) {}
}
