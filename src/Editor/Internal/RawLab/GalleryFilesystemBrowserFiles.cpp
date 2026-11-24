
#include "GalleryFilesystemBrowserState.h"

#include "App/PlatformHelpers.h"
#include "Async/TaskSystem.h"
#include "Persistence/ProjectIndex.h"
#include "Persistence/ProjectStore.h"
#include "Raw/RawSourceFolders.h"

#include <algorithm>
#include <cstdio>
#include <utility>
#include <unordered_set>
#define NOMINMAX
#include <windows.h>

namespace Stack::Editor::RawLabInternal {
namespace {
namespace fs = std::filesystem;
using FileAction = BrowserFiles::Action;
using Change = BrowserFiles::Change;

bool Within(const std::wstring& path, const std::wstring& root) {
    return path == root || (!root.empty() && path.size() > root.size() &&
        path.compare(0, root.size(), root) == 0 && (root.back() == L'\\' || path[root.size()] == L'\\'));
}

std::wstring PathKey(const fs::path& path) {
    auto key = fs::u8path(RawWorkspace::SourceFolderPathKey(path)).wstring();
    std::replace(key.begin(), key.end(), L'/', L'\\');
    return key;
}

void AppendSavedReferences(std::vector<fs::path>& paths, const std::vector<Project::ProjectRecord>& records) {
    for (const auto& project : records) {
        paths.push_back(project.absolutePath);
        const auto root = RawWorkspace::ResolveManagedProjectSourceFolder(project.absolutePath);
        for (const auto& source : project.sources) {
            if (!source.linked) continue;
            if (!source.originalPath.empty()) paths.push_back(source.originalPath);
            if (!root.empty() && !source.workspaceRelativePath.empty())
                paths.push_back(root / source.workspaceRelativePath);
        }
        if (project.rawSourceLinked && !root.empty() && !project.rawSourceRelativePathKey.empty())
            paths.push_back(root / fs::u8path(project.rawSourceRelativePathKey));
    }
}

bool IsCloset(const fs::path& path) {
    return _wcsicmp(path.filename().c_str(), L"Closet") == 0 && RawWorkspace::IsMarkedCloset(path);
}

void AppendRelocatedReferences(std::vector<fs::path>& paths, const fs::path& root,
    const std::vector<Project::ProjectRecord>& records) {
    std::unordered_set<std::string> linked;
    for (const auto& project : records) {
        if (project.rawSourceLinked && !project.rawSourceFingerprint.empty()) linked.insert(project.rawSourceFingerprint);
        for (const auto& source : project.sources) {
            if (!source.linked) continue;
            if (!source.contentSha256.empty()) linked.insert(source.contentSha256);
            if (!source.fingerprint.empty()) linked.insert(source.fingerprint);
        }
    }
    if (linked.empty()) return;
    for (const auto& layout : {RawWorkspace::BuildManagedLayout(root), RawWorkspace::BuildGalleryLayout(root)}) {
        std::vector<RawWorkspace::SourceRecord> sources;
        if (!RawWorkspace::LoadCatalogSnapshot(layout, sources)) continue;
        for (const auto& source : sources)
            if (linked.count(source.fingerprint)) paths.push_back(source.absolutePath);
    }
}

BrowserFiles::Guard BuildGuard(std::vector<fs::path> paths) {
    AppendSavedReferences(paths, Project::ProjectIndex::Get().Snapshot());
    std::vector<std::wstring> keys;
    keys.reserve(paths.size());
    for (const auto& path : paths)
        if (!path.empty()) keys.push_back(PathKey(path));
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
    return [keys = std::move(keys), checked = std::unordered_set<std::wstring>{},
            closets = std::vector<std::wstring>{}](const fs::path& path, bool subtree) mutable {
        const auto key = PathKey(path);
        if (IsCloset(path) || std::any_of(closets.begin(), closets.end(),
                [&](const auto& closet) { return Within(key, closet); })) return true;
        // Check ancestor workspaces once per batch, including replay of older
        // operations whose original target is no longer in the UI.
        for (auto parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            const auto parentKey = PathKey(parent);
            if (!checked.insert(parentKey).second) break;
            const auto closet = parent / "Closet";
            if (RawWorkspace::IsMarkedCloset(closet)) {
                const auto closetKey = PathKey(closet);
                closets.push_back(closetKey);
                if (Within(key, closetKey)) return true;
                const auto records = Project::ReadBrowserProtectionRecords(closet / "Projects");
                std::vector<fs::path> links;
                AppendSavedReferences(links, records);
                AppendRelocatedReferences(links, parent, records);
                for (const auto& link : links) keys.push_back(PathKey(link));
                std::sort(keys.begin(), keys.end());
                keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            }
            if (parent == parent.root_path()) break;
        }
        if (std::binary_search(keys.begin(), keys.end(), key)) return true;
        if (!subtree) return false;
        auto prefix = key;
        if (!prefix.empty() && prefix.back() != L'\\') prefix.push_back(L'\\');
        const auto first = std::lower_bound(keys.begin(), keys.end(), prefix);
        if (first != keys.end() && Within(*first, key)) return true;
        // A whole-directory change can conceal a workspace whose catalog has
        // never been opened in this instance.
        std::error_code error;
        fs::recursive_directory_iterator entry(path, error), end;
        for (; !error && entry != end; entry.increment(error)) {
            const auto& child = entry->path();
            const auto attrs = GetFileAttributesW(child.c_str());
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) return true;
            if ((attrs & FILE_ATTRIBUTE_DIRECTORY) && IsCloset(child)) return true;
            if (_wcsicmp(child.extension().c_str(), L".stack") == 0 &&
                (Project::IsWorkingProjectFile(child) || Project::IsPortableV3Project(child))) return true;
        }
        return bool(error);
    };
}
} // namespace

struct GalleryFilesystemBrowser::FileSession {
    BrowserFiles::History history;
    std::mutex mutex;
    std::optional<BrowserFiles::Result> ready;
    std::atomic<bool> completed{false};
    std::weak_ptr<State> requestingView;
    bool busy = false, recovery = false;
    std::string undo, redo, message;
};

GalleryFilesystemBrowser::GalleryFilesystemBrowser()
    : files_(std::make_shared<FileSession>()), state_(std::make_shared<State>()) {}

bool GalleryFilesystemBrowser::FileOperationBusy() const { return files_->busy; }
bool GalleryFilesystemBrowser::FileInputAvailable() const {
    return !files_->busy && !files_->recovery;
}

void GalleryFilesystemBrowser::SubmitFile(BrowserFiles::Request request) {
    auto session = files_;
    if (session->busy) return;
    auto protectedPaths = protectionProvider_ ? protectionProvider_() : std::vector<fs::path>{};
    session->busy = true;
    session->requestingView = state_;
    session->message.clear();
    if (!Async::TaskSystem::Get().SubmitHighPriority("Organizing Gallery files",
        [session, request = std::move(request), paths = std::move(protectedPaths)]() mutable {
            BrowserFiles::Result result;
            try {
                auto guard = BuildGuard(std::move(paths));
                result = session->history.Run(request, guard);
            } catch (const std::exception& e) {
                result.message = e.what();
                result.recovery = session->recovery;
                result.undo = session->undo;
                result.redo = session->redo;
            }
            { std::lock_guard lock(session->mutex); session->ready = std::move(result); }
            session->completed.store(true);
        })) {
        session->busy = false;
        session->message = "Could not schedule the file operation.";
    }
}

void GalleryFilesystemBrowser::PumpFiles(Action& changed) {
    if (!files_->busy || !files_->completed.exchange(false)) return;
    std::optional<BrowserFiles::Result> result;
    { std::lock_guard lock(files_->mutex); result.swap(files_->ready); }
    if (!result) return;
    files_->busy = false;
    files_->recovery = result->recovery;
    files_->undo = std::move(result->undo);
    files_->redo = std::move(result->redo);
    files_->message = std::move(result->message);
    auto& s = *state_;
    if (result->confirmation && files_->requestingView.lock() == state_ && !s.closed.load()) {
        s.confirmFile = std::move(*result);
        return;
    }
    if (result->changes.empty()) return;
    changed.changes = std::move(result->changes);
    if (s.closed.load()) return;

    const auto current = BrowserFiles::RemapLocation(s.current, changed.changes);
    for (auto& location : s.history) location = BrowserFiles::RemapLocation(location, changed.changes);
    s.motion.Stop();
    s.CancelGridInput(true);
    s.InvalidatePreviews(changed.changes);
    std::unordered_set<std::wstring> parents, items;
    for (const auto& change : changed.changes) {
        for (const auto* path : {&change.from, &change.to}) {
            if (path->empty()) continue;
            items.insert(State::Key(*path));
            parents.insert(State::Key(path->parent_path()));
        }
    }
    for (auto& [key, node] : s.listings) {
        const bool parentChanged = parents.count(key) != 0;
        bool itemChanged = items.count(key) != 0;
        for (auto end = key.find_last_of(L'\\'); !itemChanged && end != std::wstring::npos;
             end = end > 0 ? key.find_last_of(L'\\', end - 1) : std::wstring::npos)
            itemChanged = items.count(key.substr(0, end)) != 0;
        if (!parentChanged && !itemChanged) continue;
        ++node->listingGeneration;
        node->pending = false;
        node->loaded = false;
        node->error.clear();
        if (itemChanged) node->children.clear();
        if (parentChanged && !itemChanged && node->expanded && node != s.directory) s.Request(state_, node);
    }
    s.dirty = true;
    if (s.current.native() != current.native()) {
        s.Navigate(current);
        s.publishNavigation = true;
    } else {
        if (s.directory && !s.directory->loaded) s.Request(state_, s.directory);
        if (s.searchIndex) s.searchIndex->Refresh();
        if (!s.searchKey.empty()) s.SearchChanged();
    }
}

void GalleryFilesystemBrowser::RenderFileStatus() {
    if (files_->busy) ImGui::TextDisabled("Organizing files…");
    if (!files_->message.empty()) ImGui::TextWrapped("%s", files_->message.c_str());
    if (files_->recovery && !files_->busy && ImGui::Button("Retry rollback")) SubmitFile({FileAction::Recover});
}

void GalleryFilesystemBrowser::RenderFileHistory() {
    const bool available = !files_->busy && !files_->recovery && !state_->confirmFile;
    if (ImGui::MenuItem(files_->undo.empty() ? "Undo" : files_->undo.c_str(), "Ctrl+Z",
            false, available && !files_->undo.empty())) SubmitFile({FileAction::Undo});
    if (ImGui::MenuItem(files_->redo.empty() ? "Redo" : files_->redo.c_str(), "Ctrl+Y",
            false, available && !files_->redo.empty())) SubmitFile({FileAction::Redo});
}

void GalleryFilesystemBrowser::BeginFolderName(FileAction action, const fs::path& path) {
    auto& s = *state_;
    s.renameFile = BrowserFiles::Request{action, path};
    const auto initial = action == FileAction::RenameFolder ? path.filename().u8string() : "New Folder";
    std::snprintf(s.fileName, sizeof(s.fileName), "%s", initial.c_str());
    s.nameRequested = true;
}

void GalleryFilesystemBrowser::RenderFolderCommands(const fs::path& path) {
    const bool available = FileInputAvailable() && !path.empty();
    if (ImGui::MenuItem("New Folder…", nullptr, false, available)) BeginFolderName(FileAction::CreateFolder, path);
    if (ImGui::MenuItem("Rename…", nullptr, false, available && path != path.root_path()))
        BeginFolderName(FileAction::RenameFolder, path);
    if (ImGui::MenuItem("Unpack", nullptr, false, available && path != path.root_path())) SubmitFile({FileAction::Unpack, path});
}

void GalleryFilesystemBrowser::RenderFileDialogs() {
    auto& s = *state_;
    if (s.contextRequested) {
        ImGui::OpenPopup("BrowseItem");
        s.contextRequested = false;
    }
    if (ImGui::BeginPopup("BrowseItem")) {
        if (!s.contextNode) {
            if (ImGui::MenuItem("New Folder…", nullptr, false,
                    !s.current.empty() && !files_->busy && !files_->recovery))
                BeginFolderName(FileAction::CreateFolder, s.current);
            ImGui::Separator();
            RenderFileHistory();
        } else if (s.contextNode->kind == State::Kind::Image) {
            if (ImGui::MenuItem("Open in File Explorer")) {
                files_->message.clear();
                if (!PlatformHelpers::RevealPathInExplorer(s.contextNode->path, &files_->message) && files_->message.empty())
                    files_->message = "Could not reveal the image in File Explorer.";
            }
        } else RenderFolderCommands(s.contextNode->path);
        ImGui::EndPopup();
    }
    if (s.renameFile) {
        const bool rename = s.renameFile->action == FileAction::RenameFolder;
        const char* title = rename ? "Rename Folder" : "New Folder";
        if (s.nameRequested) ImGui::OpenPopup(title);
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            if (s.nameRequested) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##FolderName", s.fileName, sizeof(s.fileName),
                ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue);
            s.nameRequested = false;
            ImGui::BeginDisabled(!*s.fileName || files_->busy);
            if ((ImGui::Button(rename ? "Rename" : "Create") || enter) && *s.fileName && !files_->busy) {
                s.renameFile->name = s.fileName;
                SubmitFile(std::move(*s.renameFile));
                s.renameFile.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                s.escapeConsumed = true;
                s.renameFile.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    if (s.confirmFile) {
        constexpr auto title = "Unpack folder?";
        if (!ImGui::IsPopupOpen(title)) ImGui::OpenPopup(title);
        if (ImGui::BeginPopupModal(title, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Move %zu items out of %s and remove the empty folder?",
                s.confirmFile->confirmationCount, s.confirmFile->confirmationFolder.filename().u8string().c_str());
            if (ImGui::Button("Unpack")) {
                BrowserFiles::Request request{FileAction::Confirm, s.confirmFile->confirmationFolder};
                request.confirmation = s.confirmFile->confirmation;
                SubmitFile(std::move(request));
                s.confirmFile.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
                s.escapeConsumed = true;
                s.confirmFile.reset();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }
    if (!files_->busy && !files_->recovery && !s.confirmFile && !s.renameFile &&
        !ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
        if (!files_->undo.empty() && ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z))
            SubmitFile({FileAction::Undo});
        if (!files_->redo.empty() && (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y) ||
            ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z))) SubmitFile({FileAction::Redo});
    }
}

} // namespace Stack::Editor::RawLabInternal
