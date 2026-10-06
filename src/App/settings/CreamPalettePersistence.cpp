#include "CreamPalettePersistence.h"
#include <fstream>
#include <cmath>
#include <atomic>
#include <exception>
#include <system_error>
#include <utility>
#if defined(_WIN32)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
namespace StackAppearance {
namespace {
const char* AccentName(NodeAccent accent) {
    return accent==NodeAccent::Blue ? "blue" : accent==NodeAccent::Teal ? "teal" : "olive";
}
const char* PolicyName(PaletteColorPolicy policy) {
    return policy == PaletteColorPolicy::Monochrome ? "monochrome" : "full-color";
}
void ReadAccent(const nlohmann::json& value,const char* key,NodeAccent& accent) {
    if (!value.contains(key) || !value[key].is_string()) return;
    const auto name=value[key].get<std::string>();
    if (name=="blue") accent=NodeAccent::Blue;
    else if (name=="teal") accent=NodeAccent::Teal;
    else if (name=="olive") accent=NodeAccent::Olive;
}
}
nlohmann::json EncodeCreamPalette(const CreamPalette& palette) {
    nlohmann::json seeds = nlohmann::json::array();
    for (const auto& c : palette.seeds) seeds.push_back({c.x,c.y,c.z});
    return {{"id",palette.id},{"name",palette.name},{"seeds",seeds},{"numberAccent",AccentName(palette.numberAccent)},{"resetAccent",AccentName(palette.resetAccent)},{"colorPolicy",PolicyName(palette.colorPolicy)}};
}
bool DecodeCreamPalette(const nlohmann::json& value, CreamPalette& palette) {
    if (!value.is_object() || !value.contains("id") || !value["id"].is_string() ||
        !value.contains("name") || !value["name"].is_string() ||
        !value.contains("seeds") || !value["seeds"].is_array() || (value["seeds"].size()!=7 && value["seeds"].size()!=9)) return false;
    CreamPalette result;
    result.id = value["id"].get<std::string>(); result.name = value["name"].get<std::string>();
    if (result.id.empty() || result.name.empty()) return false;
    for (size_t i=0;i<value["seeds"].size();++i) {
        const auto& c=value["seeds"][i];
        if (!c.is_array() || c.size()!=3) return false;
        for (int j=0;j<3;++j) {
            if (!c[j].is_number()) return false;
            const double n=c[j].get<double>();
            if (!std::isfinite(n) || n<0 || n>1) return false;
            (&result.seeds[i].x)[j]=static_cast<float>(n);
        }
        result.seeds[i].w=1;
    }
    ReadAccent(value,"numberAccent",result.numberAccent);
    ReadAccent(value,"resetAccent",result.resetAccent);
    if (value.value("colorPolicy",std::string("full-color"))=="monochrome")
        result.colorPolicy=PaletteColorPolicy::Monochrome;
    palette=std::move(result); return true;
}
bool WriteAppearanceAtomically(const std::filesystem::path& path, const nlohmann::json& value,
    std::string* errorMessage) {
    if (errorMessage) errorMessage->clear();
    std::filesystem::path temporary;
    const auto fail = [&](const std::string& message) {
        if (errorMessage) *errorMessage = message;
        if (!temporary.empty()) {
            std::error_code cleanupError;
            std::filesystem::remove(temporary, cleanupError);
        }
        return false;
    };
    try {
        // Serialize before creating a file so invalid data cannot leave a
        // partial write behind or replace the last saved settings.
        const std::string serialized = value.dump(2);
        std::error_code ec;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) return fail("Could not create the settings folder. " + ec.message());
        static std::atomic<unsigned> sequence{0};
        auto temporaryPath = path;
#if defined(_WIN32)
        temporaryPath += ".pending-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(++sequence);
#else
        temporaryPath += ".pending-" + std::to_string(++sequence);
#endif
        temporary = std::move(temporaryPath);
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) return fail("Could not open a temporary settings file for writing.");
        stream << serialized << '\n';
        if (!stream.good()) {
            stream.close();
            return fail("Could not write the temporary settings file.");
        }
        stream.flush();
        if (!stream.good()) {
            stream.close();
            return fail("Could not flush the temporary settings file.");
        }
        stream.close();
        if (!stream.good()) return fail("Could not close the temporary settings file.");
#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            const DWORD replaceCode = GetLastError();
            const auto replaceError = std::error_code(static_cast<int>(replaceCode), std::system_category());
            return fail("Could not replace the settings file. " + replaceError.message());
        }
#else
        std::filesystem::rename(temporary, path, ec);
        if (ec) return fail("Could not replace the settings file. " + ec.message());
#endif
        return true;
    } catch (const std::exception& error) {
        return fail(std::string("Could not save settings. ") + error.what());
    } catch (...) {
        return fail("Could not save settings.");
    }
}
}
