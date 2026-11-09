#include "AutoBracketPreferences.h"
#include "App/AppPaths.h"
#include "ThirdParty/json.hpp"
#include "Utils/AtomicExportFile.h"
#include <fstream>

namespace Stack::AutoBracket {
Preferences& Preferences::Get() { static Preferences preferences; return preferences; }
Preferences::Preferences():path_(AppPaths::GetSettingsDirectory()/"AutoBracketSettings.json") {
    std::error_code error;
    if(!std::filesystem::exists(path_,error) && !error)return;
    const auto size=std::filesystem::file_size(path_,error);
    if(!error && size<=4096) {
        std::ifstream stream(path_);
        const auto json=nlohmann::json::parse(stream,nullptr,false);
        if(json.is_object() && json.contains("enabled") && json["enabled"].is_boolean()) {
            enabled_=json["enabled"].get<bool>();return;
        }
    }
    enabled_=false;
    error_="Could not read the automatic bracketing setting. Automatic bracketing is off.";
}
Preferences::~Preferences() { if(writer_.joinable())writer_.join(); }
std::string Preferences::Error() const { std::lock_guard<std::mutex> lock(mutex_);return error_; }
void Preferences::SetEnabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if(enabled_==enabled && error_.empty())return;
        enabled_=enabled;++revision_;
        if(writing_)return;
        writing_=true;
    }
    if(writer_.joinable())writer_.join();
    try {writer_=std::thread([this]{WritePending();});}
    catch(const std::exception& error) {
        std::lock_guard<std::mutex> lock(mutex_);error_=error.what();writing_=false;
    }
}
void Preferences::WritePending() {
    for(;;) {
        bool enabled;std::uint64_t revision;
        {std::lock_guard<std::mutex> lock(mutex_);enabled=enabled_;revision=revision_;}
        std::string error;
        try {
            std::filesystem::create_directories(path_.parent_path());
            const auto text=nlohmann::json{{"version",1},{"enabled",enabled}}.dump(2);
            FileSave::ExportFileTarget target;
            if(FileSave::PrepareExportFileTarget(path_,{}, {".json"},target,error))
                FileSave::WriteExportFileAtomically(target,{text.begin(),text.end()},error);
        } catch(const std::exception& failure) {error=failure.what();}
        std::lock_guard<std::mutex> lock(mutex_);
        error_=std::move(error);
        if(revision==revision_) {writing_=false;return;}
    }
}
}
