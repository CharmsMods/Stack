#include "Editor/CompositePersistence.h"
#include "Editor/Timeline/TimelinePersistence.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
template<class Callback> void RequireRejected(Callback callback) {
    bool rejected = false;
    try { callback(); } catch (const nlohmann::json::exception&) { rejected = true; }
    Require(rejected, "Malformed authored presentation was not rejected");
}
}

int main() try {
    using nlohmann::json;
    using namespace Stack::EditorPersistence;
    using namespace Stack::EditorModuleTypes;
    const json valid = {{"editorComposite", {
        {"sceneItems", json::array({{{"outputNodeId", 8}, {"position", {10, 20}},
            {"scale", {2, 3}}, {"rotation", 45}, {"visible", false}, {"locked", true}}})},
        {"zOrder", {8}}, {"exportSettings", {{"boundsMode", "custom"},
            {"backgroundMode", "solid"}, {"backgroundColor", {.1, .2, .3, 1.}},
            {"aspectPreset", "16:9"}, {"customWidth", 2048}, {"outputWidth", 4000}}}
    }}};
    auto current = ReadCompositePersistence(valid);
    Require(current.entries.size() == 1 && current.entries[0].position.x == 10 &&
        current.entries[0].scale.y == 3 && !current.entries[0].visible && current.entries[0].locked &&
        current.zOrder == std::vector<int>{8} &&
        current.exportSettings.boundsMode == CompositeExportBoundsMode::Custom &&
        current.exportSettings.backgroundMode == CompositeExportBackgroundMode::Solid &&
        current.exportSettings.aspectPreset == CompositeExportAspectPreset::Ratio16x9 &&
        current.exportSettings.outputWidth == 4000 && current.exportSettings.customWidth == 2048,
        "Valid composite presentation changed");
    for (const char* key : {"boundsMode", "backgroundMode", "aspectPreset", "customX",
            "customY", "customWidth", "customHeight", "customAspectRatio", "outputWidth", "outputHeight"}) {
        auto invalid = valid;
        invalid["editorComposite"]["exportSettings"][key] = json::object();
        RequireRejected([&] { current = ReadCompositePersistence(invalid); });
        Require(current.entries[0].position.x == 10 && current.exportSettings.outputWidth == 4000,
            "Rejected candidate replaced existing presentation");
    }
    for (const char* key : {"outputNodeId", "rotation", "visible", "locked"}) {
        auto invalid = valid;
        invalid["editorComposite"]["sceneItems"][0][key] = json::array();
        RequireRejected([&] { current = ReadCompositePersistence(invalid); });
    }
    auto invalid = valid;
    invalid["editorComposite"]["sceneItems"][0]["position"] = {"bad", 0};
    RequireRejected([&] { current = ReadCompositePersistence(invalid); });
    invalid = valid;
    invalid["editorComposite"]["exportSettings"]["backgroundColor"] = {"bad"};
    RequireRejected([&] { current = ReadCompositePersistence(invalid); });
    const auto defaults = ReadCompositePersistence(json::object());
    Require(defaults.entries.empty() && defaults.exportSettings.outputWidth == 1024,
        "Missing composite defaults changed");
    Require(ReadCompositePersistence({{"editorComposite", nullptr}}).entries.empty(),
        "Previously ignored optional composite changed behavior");
    auto unknown = valid;
    unknown["editorComposite"]["exportSettings"]["aspectPreset"] = "future-token";
    Require(ReadCompositePersistence(unknown).exportSettings.aspectPreset == CompositeExportAspectPreset::Ratio1x1,
        "Unknown aspect token fallback changed");

    using namespace Stack::Timeline;
    auto timeline = DeserializeTimelineDocument({{"schemaVersion", 2}, {"durationFrames", 240},
        {"framesPerSecond", 60}, {"currentFrame", 25}});
    Require(timeline.durationFrames == 240 && timeline.framesPerSecond == 60 && timeline.currentFrame == 25,
        "Valid timeline changed");
    RequireRejected([&] { timeline = DeserializeTimelineDocument(
        {{"schemaVersion", 2}, {"durationFrames", "bad"}}); });
    RequireRejected([&] { timeline = DeserializeTimelineDocument(
        {{"schemaVersion", 2}, {"tracks", json::array({{{"target", {{"nodeId", 1}}}}})}}); });
    Require(timeline.currentFrame == 25, "Rejected timeline replaced prior candidate");
    Require(DeserializeTimelineDocument({{"schemaVersion", 999}, {"durationFrames", "bad"}}).durationFrames == 120,
        "Unsupported optional timeline changed behavior");
    Require(DeserializeTimelineDocument(nullptr).animation.tracks.empty(), "Absent timeline changed behavior");
    std::cout << "Project presentation preflight and candidate preservation checks passed.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
