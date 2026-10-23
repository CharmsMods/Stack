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
    m_KnownTagsDirty = true;
    std::error_code error;
    if (m_TagFilePath.empty() || !std::filesystem::exists(m_TagFilePath, error)) return;

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
    const auto existing = m_Tags.find(fileName);
    if ((existing == m_Tags.end() && tags.empty()) ||
        (existing != m_Tags.end() && existing->second == tags)) return;
    if (tags.empty()) m_Tags.erase(fileName);
    else m_Tags[fileName] = tags;
    m_KnownTagsDirty = true;
    Save();
}

void TagManager::AddTag(const std::string& fileName, const std::string& tag) {
    auto& tags = m_Tags[fileName];
    if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
        tags.push_back(tag);
        m_KnownTagsDirty = true;
        Save();
    }
}

void TagManager::RemoveTag(const std::string& fileName, const std::string& tag) {
    auto it = m_Tags.find(fileName);
    if (it == m_Tags.end()) return;
    auto& tags = it->second;
    const auto oldSize = tags.size();
    tags.erase(std::remove(tags.begin(), tags.end(), tag), tags.end());
    if (tags.size() == oldSize) return;
    if (tags.empty()) m_Tags.erase(it);
    m_KnownTagsDirty = true;
    Save();
}

bool TagManager::HasTag(const std::string& fileName, const std::string& tag) const {
    auto it = m_Tags.find(fileName);
    if (it == m_Tags.end()) return false;
    return std::find(it->second.begin(), it->second.end(), tag) != it->second.end();
}

bool TagManager::HasTags(const std::string& fileName) const {
    const auto found = m_Tags.find(fileName);
    return found != m_Tags.end() && !found->second.empty();
}

const std::vector<std::string>& TagManager::GetAllKnownTags() const {
    if (!m_KnownTagsDirty) return m_KnownTags;
    std::unordered_set<std::string> unique;
    for (auto& [key, tags] : m_Tags) {
        for (auto& t : tags) unique.insert(t);
    }
    m_KnownTags.assign(unique.begin(), unique.end());
    std::sort(m_KnownTags.begin(), m_KnownTags.end());
    m_KnownTagsDirty = false;
    return m_KnownTags;
}

void TagManager::PruneOrphanedEntries(const std::vector<std::string>& validFileNames) {
    std::unordered_set<std::string> valid(validFileNames.begin(), validFileNames.end());
    const auto oldSize = m_Tags.size();
    for (auto it = m_Tags.begin(); it != m_Tags.end();) {
        if (valid.count(it->first) == 0) it = m_Tags.erase(it);
        else ++it;
    }
    if (m_Tags.size() == oldSize) return;
    m_KnownTagsDirty = true;
    Save();
}
