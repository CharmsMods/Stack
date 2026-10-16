#include "App/AppPaths.h"
#include "App/settings/AppearanceTheme.h"
#include "Raw/RawViewportPreferences.h"
#include "ThirdParty/json.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <thread>

namespace Stack::Validation {
namespace {
using Json = nlohmann::json;
void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
Json ReadJson(const std::filesystem::path& path) {
    std::ifstream input(path);
    return Json::parse(input);
}
void CheckAppearance(const StackAppearance::AppearanceLibrary& value) {
    using namespace StackAppearance;
    Require(value.savedCreamPalette.id == kMonochromeGrayPaletteId &&
        value.savedCreamPalette.colorPolicy == PaletteColorPolicy::Monochrome,
        "The first-run palette must be Gray.");
    Require(std::abs(value.graphLineOpacity - .20f) < 1e-6f &&
        std::abs(value.graphPanSensitivity - .40f) < 1e-6f &&
        std::abs(value.graphNodeSliderDragSensitivity - .10f) < 1e-6f &&
        value.graphConnectionTextSize == 11 &&
        value.graphConnectionTextSizing == GraphConnectionTextSizing::Fixed,
        "The chosen graph defaults did not survive storage.");
    Require(!value.backgroundImageEnabled && !value.experimentalIslandEnabled &&
        !value.graphStraightLinks && !value.graphDottedMaskLinks && !value.graphConnectionTextOutline &&
        value.viewportTiling.mode == ViewportTilingMode::Off && !value.viewportTiling.debugOverlay,
        "A disabled first-run option was enabled.");
}
void CheckViewport(const Raw::ViewportPreferences& value) {
    Require(value.targetFps == 30 && value.fadeBelowFps == 400 && value.smoothUpdates &&
        value.mode == Raw::ViewportInteractionMode::TargetFps && value.minimumDetailPercent == 100 &&
        value.backgroundLearning, "The chosen image-preview defaults did not survive storage.");
}
}

// Explicit layouts and public settings stores exercise real file I/O without
// redirecting the application's global paths or modifying a user's preferences.
bool ValidateSettingsDefaults(const std::filesystem::path& directory, bool reload) {
    Json report = {{"passed", false}, {"reload", reload}};
    bool ownsDirectory = false;
    try {
        if (reload) {
            Require(std::filesystem::is_regular_file(directory / "fresh-check.json"),
                "Reload requires a completed fresh-default check.");
            Require(ReadJson(directory / "fresh-check.json").value("passed", false),
                "The initial fresh-default check did not pass.");
        } else {
            Require(!std::filesystem::exists(directory), "Choose a new check output folder.");
            Require(std::filesystem::create_directories(directory), "Could not create the check folder.");
        }
        ownsDirectory = true;
        Json appearanceReference, viewportReference;
        for (const auto mode : {AppPaths::InstallMode::Portable, AppPaths::InstallMode::Installed}) {
            const std::string name = mode == AppPaths::InstallMode::Portable ? "portable" : "installed";
            const auto root = directory / name;
            const auto layout = AppPaths::BuildRuntimeLayoutForExecutable(root / "app" / "Stack.exe",
                mode, root / "roaming", root / "local");
            StackAppearance::AppearanceLibrary appearance;
            const bool loaded = StackAppearance::LoadAppearanceLibrary(layout.settingsFilePath, appearance);
            Require(loaded == reload, "Appearance loading did not distinguish fresh and saved settings.");
            CheckAppearance(appearance);
            auto viewport = Raw::ViewportPreferencesStore::Open(layout.settingsDirectory);
            CheckViewport(viewport->Read());
            if (!reload) {
                std::string error;
                Require(StackAppearance::SaveAppearanceLibrary(layout.settingsFilePath, appearance, &error), error);
                StackAppearance::AppearanceLibrary restored;
                Require(StackAppearance::LoadAppearanceLibrary(layout.settingsFilePath, restored),
                    "The saved appearance record could not be reopened.");
                CheckAppearance(restored);
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!viewport->IsPersisted() && viewport->SaveError().empty() &&
                std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Require(viewport->IsPersisted(), "Viewport settings did not finish saving: " + viewport->SaveError());
            const auto appearanceJson = ReadJson(layout.settingsFilePath);
            const auto viewportJson = ReadJson(layout.settingsDirectory / "RawViewportSettings.json");
            Require(viewportJson.at("targetFps") == 30 && viewportJson.at("fadeBelowFps") == 400,
                "The persisted viewport rate values differ from the selected defaults.");
            if (appearanceReference.is_null()) {
                appearanceReference = appearanceJson;
                viewportReference = viewportJson;
            } else {
                Require(appearanceJson == appearanceReference && viewportJson == viewportReference,
                    "Portable and installed settings records differ.");
            }
            report[name] = {{"appearance", appearanceJson}, {"viewport", viewportJson},
                {"settingsDirectory", layout.settingsDirectory.u8string()}};
        }
        report["portableInstalledRecordsEqual"] = true;
        report["passed"] = true;
    } catch (const std::exception& error) {
        report["error"] = error.what();
        std::cerr << "Settings defaults check failed: " << error.what() << '\n';
    }
    if (ownsDirectory) std::ofstream(directory / (reload ? "reload-check.json" : "fresh-check.json")) << report.dump(2);
    if (report.at("passed").get<bool>()) std::cout << "Settings defaults " << (reload ? "reload" : "fresh") << " check passed.\n";
    return report.at("passed").get<bool>();
}
}
