#pragma once
#include "Raw/RawWorkspace.h"
namespace Stack::RawWorkspace::StorageIO {
std::filesystem::path MakeTempPath(const std::filesystem::path& path);
bool ReplaceFileAtomically(const std::filesystem::path& from, const std::filesystem::path& to, std::string* error);
bool WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& value, const PersistenceCommitPredicate& commit, std::string* error);
bool WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& value, std::string* error);
bool WriteCompactJsonFile(const std::filesystem::path& path, const nlohmann::json& value, const PersistenceCommitPredicate& commit, std::string* error);
bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& value);
}
