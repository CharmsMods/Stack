#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

class TagManager {
public:
    static TagManager& Get() {
        static TagManager instance;
        return instance;
    }

    void SetLibraryPath(const std::filesystem::path& libraryPath);
    void Load();
    void Save();

    std::vector<std::string> GetTags(const std::string& fileName) const;
    void SetTags(const std::string& fileName, const std::vector<std::string>& tags);
    void AddTag(const std::string& fileName, const std::string& tag);
    void RemoveTag(const std::string& fileName, const std::string& tag);
    bool HasTag(const std::string& fileName, const std::string& tag) const;
    
    std::vector<std::string> GetAllKnownTags() const;
    void PruneOrphanedEntries(const std::vector<std::string>& validFileNames);

private:
    TagManager() = default;
    std::filesystem::path m_TagFilePath;
    std::unordered_map<std::string, std::vector<std::string>> m_Tags;
};
