#include "Editor/SavedResultCacheDescriptor.h"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
}

int main() try {
    using namespace Stack::EditorMultiFrameCache;
    using nlohmann::json;
    const json valid = {{"result", {
        {"state", "ready"}, {"graphIdentity", "graph-a"}, {"inputRevision", 7u},
        {"cacheKey", "key"}, {"cacheDirectory", "folder"}, {"cachePath", "burst.cache"},
        {"referenceMetadata", {{"rawWidth", 20}}}
    }}};
    SavedResultCacheDescriptor descriptor;
    std::string error;
    const auto read = [&](const json& settings, std::uint64_t revision = 7) {
        return ReadSavedResultCacheDescriptor(settings, "graph-a", revision, descriptor, error);
    };
    Require(read(valid) == SavedResultCacheState::Ready && error.empty() &&
        descriptor.cacheKey == "key" && descriptor.cacheDirectory == "folder" &&
        descriptor.cachePath == "burst.cache" && descriptor.referenceMetadata["rawWidth"] == 20,
        "Valid saved cache descriptor changed");
    for (const char* field : {"state", "graphIdentity", "inputRevision", "cacheKey",
            "cacheDirectory", "cachePath", "referenceMetadata"}) {
        auto malformed = valid;
        malformed["result"][field] = json::array();
        Require(read(malformed) == SavedResultCacheState::Invalid && !error.empty() &&
            descriptor.cacheKey.empty() && descriptor.referenceMetadata.empty(),
            "Wrong field type escaped rejection or leaked previous descriptor");
    }
    for (const json& revision : {json(-1), json(7.5), json("7"), json(nullptr), json(true)}) {
        auto malformed = valid;
        malformed["result"]["inputRevision"] = revision;
        Require(read(malformed) == SavedResultCacheState::Invalid, "Invalid revision accepted");
    }
    auto maximal = valid;
    maximal["result"]["inputRevision"] = std::numeric_limits<std::uint64_t>::max();
    Require(read(maximal, std::numeric_limits<std::uint64_t>::max()) == SavedResultCacheState::Ready,
        "Unsigned revision narrowed");
    auto ignored = valid;
    ignored["result"]["cachePath"] = false;
    ignored["result"]["state"] = "stale";
    Require(read(ignored) == SavedResultCacheState::Ignored, "Stale result was inspected");
    ignored["result"]["state"] = "ready";
    ignored["result"]["graphIdentity"] = "different";
    Require(read(ignored) == SavedResultCacheState::Ignored, "Mismatched identity was inspected");
    ignored["result"]["graphIdentity"] = "graph-a";
    ignored["result"]["inputRevision"] = 8;
    Require(read(ignored) == SavedResultCacheState::Ignored, "Mismatched revision was inspected");
    Require(read(json::object()) == SavedResultCacheState::Ignored && error.empty(),
        "Absent cache was not ignored");
    Require(read({{"result", nullptr}}) == SavedResultCacheState::Ignored,
        "Non-object cache changed prior ignore behavior");
    Require(read({{"result", {{"state", "ready"}}}}, 0) == SavedResultCacheState::Ready &&
        descriptor.cacheKey.empty() && descriptor.cachePath.empty() && descriptor.referenceMetadata.is_object(),
        "Optional fields lost their defaults");
    Require(read(json::array()) == SavedResultCacheState::Invalid, "Non-object settings accepted");
    Require(valid["result"]["cachePath"] == "burst.cache", "Parser mutated source settings");
    std::cout << "Saved result cache descriptor validation checks passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
