#include "TagManager.h"
#include "ThirdParty/json.hpp"
#include <fstream>
#include <iostream>
#include <algorithm>

using json = nlohmann::json;

void TagManager::SetLibraryPath(const std::filesystem::path& libraryPath) {
    m_TagFilePath = libraryPath / "tags.json";
}

void TagManager::Load() {
    m_Tags.clear();
    if (m_TagFilePath.empty() || !std::filesystem::exists(m_TagFilePath)) return;

    try {
        std::ifstream file(m_TagFilePath);
        if (!file.is_open()) return;
        json j = json::parse(file, nullptr, false);
        if (j.is_discarded() || !j.is_object()) return;

        for (auto& [key, val] : j.items()) {
            if (val.is_array()) {
                std::vector<std::string> tags;
                for (auto& t : val) {
                    if (t.is_string()) tags.push_back(t.get<std::string>());
                }
                if (!tags.empty()) m_Tags[key] = tags;
            }
        }
    } catch (...) {
        std::cerr << "[TagManager] Failed to load tags.json\n";
    }
}

void TagManager::Save() {
    if (m_TagFilePath.empty()) return;
    try {
        json j = json::object();
        for (auto& [key, tags] : m_Tags) {
            if (!tags.empty()) j[key] = tags;
        }
        std::ofstream file(m_TagFilePath);
        file << j.dump(2);
    } catch (...) {
        std::cerr << "[TagManager] Failed to save tags.json\n";
    }
}

std::vector<std::string> TagManager::GetTags(const std::string& fileName) const {
    auto it = m_Tags.find(fileName);
    if (it != m_Tags.end()) return it->second;
    return {};
}

void TagManager::SetTags(const std::string& fileName, const std::vector<std::string>& tags) {
    if (tags.empty()) m_Tags.erase(fileName);
    else m_Tags[fileName] = tags;
    Save();
}

void TagManager::AddTag(const std::string& fileName, const std::string& tag) {
    auto& tags = m_Tags[fileName];
    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
        tags.push_back(tag);
        Save();
    }
}

void TagManager::RemoveTag(const std::string& fileName, const std::string& tag) {
    auto it = m_Tags.find(fileName);
    if (it == m_Tags.end()) return;
    auto& tags = it->second;
    tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
    if (tags.empty()) m_Tags.erase(it);
    Save();
}

bool TagManager::HasTag(const std::string& fileName, const std::string& tag) const {
    auto it = m_Tags.find(fileName);
    if (it == m_Tags.end()) return false;
    return std::find(it->second.begin(), it->second.end(), tag) != it->second.end();
}

std::vector<std::string> TagManager::GetAllKnownTags() const {
    std::unordered_set<std::string> unique;
    for (auto& [key, tags] : m_Tags) {
        for (auto& t : tags) unique.insert(t);
    }
    std::vector<std::string> result(unique.begin(), unique.end());
    std::sort(result.begin(), result.end());
    return result;
}

void TagManager::PruneOrphanedEntries(const std::vector<std::string>& validFileNames) {
    std::unordered_set<std::string> valid(validFileNames.begin(), validFileNames.end());
    for (auto it = m_Tags.begin(); it != m_Tags.end();) {
        if (valid.count(it->first) == 0) it = m_Tags.erase(it);
        else ++it;
    }
    Save();
}
