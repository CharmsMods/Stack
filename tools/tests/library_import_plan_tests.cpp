#include "Library/Internal/LibraryImportPlan.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary); stream << bytes; stream.close();
    Require(stream.good(), "Fixture write failed");
}
std::string Read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), {}};
}
struct Fixtures {
    std::filesystem::path root = std::filesystem::temp_directory_path() /
        ("stack-library-import-plan-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixtures() { Require(std::filesystem::create_directory(root), "Fixture reservation failed"); }
    ~Fixtures() { std::error_code error; std::filesystem::remove_all(root, error); }
};
}
int main() try {
    using namespace Stack::Library;
    Fixtures f;
    const auto projects = f.root / "Projects", assets = f.root / "Assets";
    std::filesystem::create_directory(projects);
    std::filesystem::create_directory(assets);
    const std::vector<unsigned char> old {'O','L','D'}, fresh {'N','E','W'};
    Write(assets / "existing.png", "OLD");
    LibraryImportPlan plan;
    std::string error;
    Require(!PrepareLibraryImportPlan({{projects / "new-project", false}},
        {{assets / "new.png", &fresh}, {assets / "existing.png", &fresh}}, plan, error),
        "Different existing asset was accepted");
    Require(!std::filesystem::exists(projects / "new-project") &&
        !std::filesystem::exists(assets / "new.png") && Read(assets / "existing.png") == "OLD",
        "Preflight collision modified files");
    Require(error.find("before writing") != std::string::npos, "Preflight error did not explain preservation");
    Require(PrepareLibraryImportPlan({{projects / "new-project", false}},
        {{assets / "existing.png", &old}, {assets / "new.png", &fresh}}, plan, error),
        "Identical asset reuse or new destination rejected");
    Require(plan.assets.size() == 2 && plan.assets[0].reuse && !plan.assets[1].reuse &&
        !plan.assets[1].target.approval.existed, "Incorrect asset plan");
    auto captured = plan.assets[1].target;
    Write(assets / "new.png", "OTHER-IMPORT");
    Require(!Stack::FileSave::WriteExportFileAtomically(captured, fresh, error),
        "New asset created after preflight was overwritten");
    Require(Read(assets / "new.png") == "OTHER-IMPORT", "Late collision changed bytes");

    Require(!PrepareLibraryImportPlan({{projects / "same", false}, {projects / "SAME", false}},
        {}, plan, error), "Windows-equivalent project names accepted");
    Require(!PrepareLibraryImportPlan({}, {{assets / "duplicate.png", &old},
        {assets / "DUPLICATE.PNG", &fresh}}, plan, error), "Duplicate image targets accepted");
    Require(!PrepareLibraryImportPlan({{assets / "shared.png", false}},
        {{assets / "shared.png", &fresh}}, plan, error), "Project/image shared destination accepted");

    const auto conflict = projects / "existing-project";
    std::filesystem::create_directory(conflict);
    Write(conflict / "sentinel", "PROJECT");
    Require(!PrepareLibraryImportPlan({{conflict, false}}, {}, plan, error),
        "Existing project without decision accepted");
    Require(PrepareLibraryImportPlan({{conflict, true}},
        {{assets / "existing.png", &fresh, true, true}}, plan, error),
        "Existing project preview decision was discarded");
    Require(plan.assets[0].deferred && !plan.assets[0].reuse &&
        Read(conflict / "sentinel") == "PROJECT" && Read(assets / "existing.png") == "OLD",
        "Deferred conflict changed files");
    Require(!PrepareLibraryImportPlan({{conflict, true}},
        {{assets / "existing.png", &fresh, true, false}}, plan, error),
        "Deferred project could replace an unrelated loose image");

    // ResolveConflict uses a fresh plan before saving the chosen project.
    // A non-preview target that appeared during the decision cannot acquire
    // replacement permission simply because the user chose project Replace.
    const auto delayed = assets / "delayed.png";
    Require(PrepareLibraryImportPlan({{conflict, true}},
        {{delayed, &fresh, true, false}}, plan, error), "Initial deferred plan failed");
    Write(delayed, "UNRELATED");
    Require(!PrepareLibraryImportPlan({{conflict, true}},
        {{delayed, &fresh, true, false}}, plan, error), "Late deferred collision accepted");
    Require(Read(delayed) == "UNRELATED" && Read(conflict / "sentinel") == "PROJECT",
        "Deferred conflict modified accepted data");
    Require(!PrepareLibraryImportPlan({{projects / "copy-project", false}},
        {{assets / "existing.png", &fresh, true, false}}, plan, error),
        "Keep Both preview collision accepted");
    Require(!std::filesystem::exists(projects / "copy-project"),
        "Keep Both preflight created a partial copy");
    const auto chosenPreview = assets / "chosen-copy.png";
    Require(PrepareLibraryImportPlan({{projects / "chosen-copy", false}},
        {{chosenPreview, &fresh, true, false}}, plan, error), "Keep Both plan failed");
    captured = plan.assets[0].target;
    Write(chosenPreview, "RACED");
    Require(!Stack::FileSave::WriteExportFileAtomically(captured, fresh, error) &&
        Read(chosenPreview) == "RACED", "Keep Both late publisher was overwritten");
    Require(PrepareLibraryImportPlan({{conflict, true}},
        {{assets / "existing.png", &fresh, true, true}}, plan, error),
        "Explicit own-preview replacement denied");
    Require(Stack::FileSave::WriteExportFileAtomically(plan.assets[0].target, fresh, error) &&
        Read(assets / "existing.png") == "NEW", "Explicit own-preview replacement failed");
    Write(assets / "existing.png", "OLD");

    std::error_code ec;
    const auto original = f.root / "camera.DNG";
    Write(original, "ORIGINAL");
    std::filesystem::create_symlink(original, assets / "link.png", ec);
    if (!ec) Require(!PrepareLibraryImportPlan({}, {{assets / "link.png", &fresh}}, plan, error),
        "Symbolic-link asset destination accepted");
    ec.clear();
    std::filesystem::create_hard_link(assets / "existing.png", assets / "alias.png", ec);
    if (!ec) Require(!PrepareLibraryImportPlan({}, {{assets / "alias.png", &fresh}}, plan, error),
        "Different existing alias contents accepted");
    Require(Read(original) == "ORIGINAL" && Read(assets / "existing.png") == "OLD",
        "Original bytes changed");
    Require(!PrepareLibraryImportPlan({}, {{assets / "invalid.png", nullptr}}, plan, error),
        "Missing image bytes accepted");
    Require(PrepareLibraryImportPlan({}, {{assets / "publish.png", &fresh}}, plan, error),
        "New image plan failed");
    Require(Stack::FileSave::WriteExportFileAtomically(plan.assets[0].target, fresh, error) &&
        Read(assets / "publish.png") == "NEW", "Planned new image publication failed");
    std::cout << "Library import preflight collision, reuse, deferral and late-write checks passed.\n";
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
