#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Stack::Search {
// A recursive file/folder-name catalog, independent of Gallery and image decoding.
// Public calls are nonblocking (except destruction, which drains owned workers).
// One caller owns navigation/query intent; results are latest-only and revision checked.
class FolderIndex {
public:
    struct Result {
        struct Item { std::filesystem::path path; bool directory=true; };
        std::uint64_t request=0;
        std::vector<Item> items;
        bool refreshing=false, partial=false, fromCache=false;
        std::string status;
    };
    explicit FolderIndex(std::filesystem::path cacheDirectory);
    ~FolderIndex();
    FolderIndex(const FolderIndex&)=delete;
    FolderIndex& operator=(const FolderIndex&)=delete;
    // Descendant scopes reuse their ancestor catalog. Empty scope suspends work.
    void SetScope(const std::filesystem::path& folder);
    // Empty extensions includes all files; otherwise filter files before ranking/capping.
    std::uint64_t Search(const std::wstring& name,const std::vector<std::wstring>& extensions={});
    void Refresh();
    void Suspend();
    // Atomic fast path when nothing changed; no catalog traversal or disk access.
    bool TryTakeResult(Result& result);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
