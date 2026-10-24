#include "Persistence/StackBinaryFormat.h"
// This standalone test target owns its stb writer implementation.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ThirdParty/stb_image_write.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#ifndef _WIN32
#include <csignal>
#include <sys/resource.h>
#endif
namespace {
namespace fs = std::filesystem;
namespace Format = StackBinaryFormat;
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
std::string Read(const fs::path& path) { std::ifstream f(path, std::ios::binary); return {std::istreambuf_iterator<char>(f), {}}; }
void Put(const fs::path& path, const std::string& bytes) { std::ofstream f(path, std::ios::binary); f << bytes; f.close(); Require(f.good(), "write fixture"); }
struct Sandbox {
    fs::path path = fs::temp_directory_path() / ("stack-archive-tests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Sandbox() { Require(fs::create_directory(path), "create sandbox"); }
    ~Sandbox() { std::error_code ec; fs::remove_all(path, ec); }
};
#ifndef _WIN32
struct FileSizeLimit {
    rlimit previous{};
    using Handler = void (*)(int);
    Handler handler;
    FileSizeLimit() {
        Require(getrlimit(RLIMIT_FSIZE, &previous) == 0, "get limit");
        handler = std::signal(SIGXFSZ, SIG_IGN);
        auto limited = previous; limited.rlim_cur = 1;
        if (setrlimit(RLIMIT_FSIZE, &limited) != 0) {
            std::signal(SIGXFSZ, handler); throw std::runtime_error("set limit");
        }
    }
    ~FileSizeLimit() { setrlimit(RLIMIT_FSIZE, &previous); std::signal(SIGXFSZ, handler); }
};
#endif
}
int main() {
    Sandbox sandbox;
    const auto raw = sandbox.path / "synthetic.NEF";
    const auto destination = sandbox.path / "bundle.stacklib";
    const auto oldTemp = fs::path(destination.string() + ".tmp");
    Format::LibraryBundleDocument bundle; bundle.bundleName = "Synthetic fixture";
    bundle.assets.push_back({}); bundle.assets.back().fileName = "fixture.png";
    bundle.assets.back().imageBytes.assign(10000, 42);
    Put(raw, "original fixture");
    Require(!Format::WriteLibraryBundle(raw, bundle) && Read(raw) == "original fixture", "reject RAW path");
    Require(Format::WriteLibraryBundle(destination, bundle), "write archive");
    Format::LibraryBundleDocument loaded;
    Require(Format::ReadLibraryBundle(destination, loaded) && loaded.assets[0].imageBytes == bundle.assets[0].imageBytes, "round trip compressed sections");
    Put(oldTemp, "unrelated staging name");
    Require(Format::WriteLibraryBundle(destination, bundle) && Read(oldTemp) == "unrelated staging name", "preserve unrelated old temp");
    fs::remove(oldTemp);
    fs::create_hard_link(raw, oldTemp);
    Require(Format::WriteLibraryBundle(destination, bundle) && Read(raw) == "original fixture", "preserve hardlinked old temp");
    fs::remove(oldTemp);
    std::error_code symlinkError; fs::create_symlink(raw, oldTemp, symlinkError);
    if (!symlinkError) {
        Require(Format::WriteLibraryBundle(destination, bundle) && Read(raw) == "original fixture", "preserve symlinked old temp");
        fs::remove(oldTemp);
        fs::remove(destination); fs::create_symlink(raw, destination);
        Require(!Format::WriteLibraryBundle(destination, bundle) && Read(raw) == "original fixture", "reject final symlink");
        fs::remove(destination);
    }
    Stack::FileSave::ExportFileTarget target; std::string error;
    fs::remove(destination);
    Require(Stack::FileSave::PrepareExportFileTarget(destination, {}, {".stacklib"}, target, error), "capture absent approval");
    Put(destination, "late arrival");
    Require(!Format::WriteLibraryBundle(target, bundle, error) && Read(destination) == "late arrival", "reject late target");
    Require(Stack::FileSave::PrepareExportFileTarget(destination, {}, {".stacklib"}, target, error), "capture existing approval");
    Put(destination, "changed after approval");
    Require(!Format::WriteLibraryBundle(target, bundle, error) && Read(destination) == "changed after approval", "reject changed approved target");
    Format::BundledProjectDocument project;
    project.project.pipelineData = {{"sourcePath", destination.u8string()}};
    bundle.projects.push_back(project);
    Require(!Format::WriteLibraryBundle(destination, bundle) && Read(destination) == "changed after approval", "protect referenced original regardless of extension");
    bundle.projects.clear();
    const auto alias = sandbox.path / "original-alias.stacklib";
    fs::create_hard_link(raw, alias);
    project.project.pipelineData = {{"sourcePath", raw.u8string()}};
    bundle.projects.push_back(project);
    Require(!Format::WriteLibraryBundle(alias, bundle) && Read(raw) == "original fixture", "reject protected hardlink alias");
    bundle.projects.clear();
    Format::NodePresetDocument preset; preset.metadata.id = "fixture";
    const auto presetPath = sandbox.path / "node.stackpreset";
    Require(Format::WriteNodePresetFile(presetPath, preset), "preset write");
    Require(!Format::WriteNodePresetFile(raw, preset) && Read(raw) == "original fixture", "preset rejects original");
#ifndef _WIN32
    fs::create_hard_link(destination, oldTemp);
    { FileSizeLimit limit; Require(!Format::WriteLibraryBundle(destination, bundle), "late close fails"); }
    Require(Read(destination) == "changed after approval" && Read(oldTemp) == "changed after approval", "late failure retains destination and aliased old staging");
#endif
    for (const auto& entry : fs::directory_iterator(sandbox.path))
        Require(entry.path().filename().string().find(".stack-export-") != 0, "staging cleaned");
    std::cout << "Archive export tests passed\n";
}
