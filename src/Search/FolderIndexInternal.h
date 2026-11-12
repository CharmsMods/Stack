#pragma once
#include "FolderIndex.h"
#include "NameMatch.h"
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#define NOMINMAX
#include <windows.h>

namespace Stack::Search {
namespace Detail {
constexpr std::size_t MaxEntries=100000, MaxTextUnits=16*1024*1024;
struct Entry { std::wstring relative, name; bool directory=true; PreparedName prepared; std::wstring relativeKey; };
struct Snapshot {
    std::filesystem::path root;
    std::vector<Entry> entries;
    bool partial=false, fromCache=false;
    std::string status;
};
std::wstring Fold(std::wstring value);
std::wstring PathKey(const std::filesystem::path& path);
bool Within(const std::wstring& path,const std::wstring& root);
std::shared_ptr<Snapshot> Load(const std::filesystem::path& cache,const std::filesystem::path& root);
void Save(const std::filesystem::path& cache,const Snapshot& snapshot);
}

struct FolderIndex::Impl {
    explicit Impl(std::filesystem::path cacheDirectory);
    ~Impl();
    std::filesystem::path cache;
    std::mutex mutex;
    std::condition_variable queryWake;
    HANDLE wake=nullptr;
    std::thread monitor,queryWorker;
    std::atomic<bool> stopping{false},ready{false};
    std::atomic<std::uint64_t> generation{0},querySerial{0};
    bool active=false,refreshing=false;
    std::uint64_t revision=0;
    std::filesystem::path root,scope;
    std::wstring rootKey,query;
    std::vector<std::wstring> extensions;
    std::shared_ptr<const Detail::Snapshot> snapshot;
    std::vector<std::shared_ptr<const Detail::Snapshot>> retired;
    Result completion;
    bool Current(std::uint64_t epoch) const {return !stopping.load() && generation.load()==epoch;}
    void Publish(std::uint64_t epoch,std::shared_ptr<const Detail::Snapshot> value,bool refreshing);
    void Monitor();
    void Query();
    std::shared_ptr<Detail::Snapshot> Scan(const std::filesystem::path& path,std::uint64_t epoch);
};
}
