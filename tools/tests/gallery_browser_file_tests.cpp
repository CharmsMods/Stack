
#include "Platform/BrowserFileHistory.h"
#include "Editor/Internal/RawLab/GalleryBrowserMotion.h"
#include "Editor/Internal/RawLab/GalleryBrowserSelection.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#define NOMINMAX
#include <windows.h>

namespace fs = std::filesystem;
namespace Files = Stack::BrowserFiles;
using Files::Action;

namespace {

void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

void Put(const fs::path& path) {
    std::ofstream output(path, std::ios::binary);
    output << "disposable filesystem fixture";
    if (!output) throw std::runtime_error(path.u8string());
}

struct TempFolder {
    fs::path path = fs::temp_directory_path() / (L"StackBrowserFileTests-" + std::to_wstring(GetCurrentProcessId()));
    TempFolder() { Require(fs::create_directory(path), "Could not own fixture folder"); }
    ~TempFolder() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};

Files::Result Apply(Files::History& history, const Files::Request& request, std::size_t count) {
    auto result = history.Run(request);
    if (!result.message.empty()) throw std::runtime_error(result.message);
    Require(result.changes.size() == count, "Wrong number of changed paths");
    return result;
}

void HistoryTest(const fs::path& root) {
    Files::History history;
    const auto a = root / L"Photo A.jpg", b = root / L"Photo B.jpg", c = root / L"Camera.nef";
    Put(a);
    Put(b);
    Put(c);
    const auto group = root / L"Photo B";
    Apply(history, {Action::GroupImages, b, {a}}, 3);
    Require(fs::exists(group / a.filename()) && fs::exists(group / b.filename()), "Grouping did not move both images");
    Apply(history, {Action::Undo}, 3);
    Require(!fs::exists(group) && fs::exists(a) && fs::exists(b), "Grouping undo did not restore paths");
    Apply(history, {Action::Redo}, 3);

    const auto target = root / L"Album";
    Apply(history, {Action::CreateFolder, root, {}, "Album"}, 1);
    const auto invalid = history.Run({Action::CreateFolder, root, {}, "AUX.txt"});
    Require(!invalid.message.empty() && !fs::exists(root / L"AUX.txt"), "Reserved name was accepted");
    Put(target / c.filename());
    const auto collision = history.Run({Action::MoveImages, target, {c, group / a.filename()}});
    Require(collision.changes.empty() && !collision.message.empty() && fs::exists(c) &&
        fs::exists(group / a.filename()), "A collision changed part of the batch");
    fs::remove(target / c.filename());

    const auto protectedResult = history.Run({Action::MoveImages, target, {c}}, [&](const auto& path, bool) {
        return path == c;
    });
    Require(!protectedResult.message.empty() && fs::exists(c), "A protected RAW moved");
    Apply(history, {Action::MoveImages, target, {c, group / a.filename()}}, 2);
    Apply(history, {Action::Undo}, 2);
    Apply(history, {Action::Redo}, 2);
    const auto renamed = root / L"ALBUM";
    Apply(history, {Action::RenameFolder, target, {}, "ALBUM"}, 1);
    Apply(history, {Action::Undo}, 1);
    Apply(history, {Action::Redo}, 1);
    Require(fs::exists(renamed / c.filename()), "Renaming lost an image");

    const auto held = root / L"held.jpg";
    fs::rename(renamed, root / L"Displaced");
    fs::create_directory(renamed);
    Put(renamed / held.filename());
    const auto replaced = history.Run({Action::Undo});
    Require(!replaced.message.empty() && fs::exists(renamed / held.filename()) &&
        fs::exists(root / L"Displaced" / c.filename()), "Undo adopted a replacement folder");
}

void UnpackTest(const fs::path& root) {
    Files::History history;
    Apply(history, {Action::CreateFolder, root, {}, "Pieces"}, 1);
    const auto folder = root / L"Pieces";
    const auto nested = folder / L"Nested";
    fs::create_directory(nested);
    Put(nested / L"Keep.txt");
    for (unsigned i = 0; i < 49; ++i) Put(folder / (std::to_wstring(i) + L".jpg"));
    Apply(history, {Action::Unpack, folder}, 51);
    Require(!fs::exists(folder) && fs::exists(root / L"Nested" / L"Keep.txt"), "Unpack failed to keep the nested directory intact");
    Apply(history, {Action::Undo}, 51);
    Require(fs::exists(nested / L"Keep.txt"), "Unpack undo did not restore the nested folder");
    const auto refusal = history.Run({Action::Undo});
    Require(!refusal.message.empty() && fs::exists(nested), "Undo deleted a folder with independent contents");

    const auto extra = folder / L"Source.xmp";
    Put(extra);
    const auto confirmation = history.Run({Action::Unpack, folder});
    Require(confirmation.confirmation != 0 && confirmation.confirmationCount == 51 &&
        confirmation.changes.empty() && fs::exists(extra), "51 entries did not require confirmation");
    const auto newEntry = folder / L"New.txt";
    Put(newEntry);
    Files::Request confirm{Action::Confirm};
    confirm.confirmation = confirmation.confirmation;
    const auto stale = history.Run(confirm);
    Require(!stale.message.empty() && stale.changes.empty() && fs::exists(newEntry),
        "Confirmation accepted changed contents");
    fs::remove(newEntry);

    const auto fresh = history.Run({Action::Unpack, folder});
    confirm.confirmation = fresh.confirmation;
    Apply(history, confirm, 52);
    Apply(history, {Action::Undo}, 52);
    const auto redo = history.Run({Action::Redo});
    Require(redo.confirmation && redo.confirmationCount == 51 && fs::exists(extra), "Redo lost confirmation");
    confirm.confirmation = redo.confirmation;
    Apply(history, confirm, 52);
}

void SelectionTest() {
    using Stack::Editor::RawLabInternal::BrowserRightDrag;
    BrowserRightDrag gesture;
    const ImRect tile({10, 10}, {100, 100});
    gesture.Begin(4, tile);
    gesture.Update({95, 90}, 4);
    Require(!gesture.Range() && gesture.ReleaseShowsMenu(true), "Right-click jitter became a selection");

    gesture.Begin(4, tile);
    gesture.Update({95, 110}, std::nullopt);
    gesture.Update({120, 110}, 7);
    Require(gesture.Range()->Contains(4) && gesture.Range()->Contains(7) &&
        gesture.Range()->Contains(5) && !gesture.Range()->Contains(3), "Range omitted intermediate tiles");
    gesture.Update({80, 80}, 4);
    Require(!gesture.ReleaseShowsMenu(true), "Returning inside the tile reopened a menu");

    gesture.Begin(std::nullopt, {});
    gesture.Update({150, 110}, 8);
    gesture.Update({20, 20}, 2);
    Require(gesture.Range()->First() == 2 && gesture.Range()->Last() == 8 &&
        !gesture.ReleaseShowsMenu(true), "A drag from empty space lost its inclusive reverse range");
}

void MotionTest() {
    using Stack::Editor::RawLabInternal::GalleryBrowserMotion;
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {800, 600};
    io.Fonts->AddFontDefault();
    io.Fonts->Build();
    GalleryBrowserMotion motion;
    const ImRect surface{{60, 60}, {740, 540}}, anchor{{100, 110}, {235, 225}};
    GalleryBrowserMotion::Tile tile{{{80, 80}, {220, 200}}, "Photo", 0, 0, 0, true, 1};
    motion.Capture(surface);
    motion.CaptureTile(tile);
    motion.Start(false, anchor);

    auto frame = [&](bool ready, bool reduced, float time) {
        io.DeltaTime = time;
        ImGui::NewFrame();
        ImGui::SetNextWindowPos({0, 0});
        ImGui::SetNextWindowSize(io.DisplaySize);
        ImGui::Begin("Browser");
        auto& draw = *ImGui::GetWindowDrawList();
        const int first = draw.VtxBuffer.Size;
        motion.Begin(surface, ready, reduced);
        motion.DrawUnder(draw);
        motion.DrawLiveTile(draw, tile);
        motion.DrawOver(draw);
        const bool drew = draw.VtxBuffer.Size > first;
        ImGui::End();
        ImGui::Render();
        return drew;
    };
    Require(frame(false, false, .016f) && motion.Active() && motion.HideLiveGrid(), "Opening lost the waiting surface");
    Require(frame(true, false, .016f) && motion.Active(), "Opening lost the miniature grid");
    tile.texture = 0x99;
    tile.width = 120;
    tile.height = 80;
    Require(frame(true, false, .125f) && motion.Active(), "Opening ended before the zoom completed");
    frame(true, false, .14f);
    Require(!motion.Active() && !motion.HideLiveGrid(), "Opening did not settle");

    motion.Capture(surface);
    motion.CaptureTile(tile);
    motion.Start(true, {});
    frame(true, false, .016f);
    motion.Anchor(anchor);
    Require(frame(true, false, .016f) && motion.Active() && !motion.HideLiveGrid(), "Closing did not inherit the parent grid");
    frame(true, true, .016f);
    Require(!motion.Active(), "Reduced motion left a transition active");
    ImGui::DestroyContext();
}
} // namespace

int main() {
    try {
        TempFolder folder;
        fs::create_directory(folder.path / L"history");
        fs::create_directory(folder.path / L"unpack");
        HistoryTest(folder.path / L"history");
        UnpackTest(folder.path / L"unpack");
        SelectionTest();
        MotionTest();
        std::cout << "Gallery file history and folder motion passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
