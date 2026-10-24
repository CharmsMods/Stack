#pragma once

#include "ThirdParty/json.hpp"
#include <cstdint>
#include <string>
#include <utility>

namespace Stack::EditorMultiFrameCache {

enum class SavedResultCacheState { Ignored, Ready, Invalid };

struct SavedResultCacheDescriptor {
    std::string cacheKey;
    std::string cacheDirectory;
    std::string cachePath;
    nlohmann::json referenceMetadata = nlohmann::json::object();
};

// Derived cache hints must not make the authoritative project unloadable.
// Ignore stale results before inspecting their unused cache fields.
inline SavedResultCacheState ReadSavedResultCacheDescriptor(
    const nlohmann::json& settings, const std::string& graphIdentity,
    std::uint64_t inputRevision, SavedResultCacheDescriptor& descriptor,
    std::string& error) {
    descriptor = {};
    error.clear();
    const auto invalid = [&](const char* field) {
        error = std::string("The saved result cache has an invalid ") + field + ".";
        return SavedResultCacheState::Invalid;
    };
    if (!settings.is_object()) return invalid("settings object");
    const auto found = settings.find("result");
    if (found == settings.end() || !found->is_object()) return SavedResultCacheState::Ignored;
    const auto& result = *found;
    const auto readString = [&](const char* key, std::string& value) {
        const auto field = result.find(key);
        if (field == result.end()) return true;
        if (!field->is_string()) return false;
        value = field->get<std::string>();
        return true;
    };
    std::string state;
    if (!readString("state", state)) return invalid("state");
    if (state != "ready") return SavedResultCacheState::Ignored;
    std::string savedIdentity;
    if (!readString("graphIdentity", savedIdentity)) return invalid("graph identity");
    if (!savedIdentity.empty() && savedIdentity != graphIdentity) return SavedResultCacheState::Ignored;
    std::uint64_t savedRevision = 0;
    if (const auto revision = result.find("inputRevision"); revision != result.end()) {
        if (!revision->is_number_integer() ||
            (!revision->is_number_unsigned() && revision->get<std::int64_t>() < 0))
            return invalid("input revision");
        savedRevision = revision->get<std::uint64_t>();
    }
    if (savedRevision != inputRevision) return SavedResultCacheState::Ignored;
    SavedResultCacheDescriptor candidate;
    if (!readString("cacheKey", candidate.cacheKey)) return invalid("cache key");
    if (!readString("cacheDirectory", candidate.cacheDirectory)) return invalid("cache directory");
    if (!readString("cachePath", candidate.cachePath)) return invalid("cache path");
    if (const auto metadata = result.find("referenceMetadata"); metadata != result.end()) {
        if (!metadata->is_object()) return invalid("reference metadata");
        candidate.referenceMetadata = *metadata;
    }
    descriptor = std::move(candidate);
    return SavedResultCacheState::Ready;
}
} // namespace Stack::EditorMultiFrameCache
