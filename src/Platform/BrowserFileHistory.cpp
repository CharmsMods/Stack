
#include "BrowserFileHistory.h"

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <cwctype>
#include <stdexcept>
#include <set>
#include <map>
#include <system_error>
#include <vector>
#define NOMINMAX
#include <windows.h>

namespace Stack::BrowserFiles {
namespace {
namespace fs = std::filesystem;
using Kind = Change::Kind;

void Require(bool condition, const std::string& error) {
    if (!condition) throw std::runtime_error(error);
}

std::string Display(const fs::path& path) { return path.u8string(); }

void CheckWin(bool success, const char* operation, const fs::path& path) {
    if (success) return;
    const std::error_code error(GetLastError(), std::system_category());
    throw std::runtime_error(std::string(operation) + " " + Display(path) + " (" + error.message() + ")");
}

bool Same(const fs::path& left, const fs::path& right) {
    const auto a = left.lexically_normal().wstring(), b = right.lexically_normal().wstring();
    if (a.empty() || b.empty()) return a == b;
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()), b.data(),
        static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

fs::path ValidateName(const fs::path& name) {
    const auto value = name.wstring();
    Require(!value.empty() && value.size() <= 255 && value.back() != L'.' && value.back() != L' ' &&
        value.find_first_of(L"<>:\"/\\|?*") == std::wstring::npos &&
        std::none_of(value.begin(), value.end(), [](wchar_t c) { return c < 32; }),
        "Enter a valid Windows folder name.");
    auto base = value.substr(0, value.find(L'.'));
    while (!base.empty() && base.back() == L' ') base.pop_back();
    for (auto& c : base) c = static_cast<wchar_t>(std::towupper(c));
    const std::wstring prefix = base.substr(0, 3);
    Require(base != L"CON" && base != L"PRN" && base != L"AUX" && base != L"NUL" &&
        base != L"CONIN$" && base != L"CONOUT$" &&
        !(base.size() == 4 && (prefix == L"COM" || prefix == L"LPT") &&
            std::wstring_view(L"123456789\u00b9\u00b2\u00b3").find(base.back()) != std::wstring_view::npos),
        "That name is reserved by Windows.");
    return name;
}

DWORD Attributes(const fs::path& path, bool allowMissing = false) {
    const DWORD attrs = GetFileAttributesW(path.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) return attrs;
    const DWORD error = GetLastError();
    if (allowMissing && (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND))
        return INVALID_FILE_ATTRIBUTES;
    CheckWin(false, "Could not inspect", path);
    return INVALID_FILE_ATTRIBUTES;
}

void CheckPath(const fs::path& path) {
    Require(path.is_absolute() && path == path.lexically_normal(), "The location is invalid.");
    for (auto parent = path; parent != parent.root_path(); parent = parent.parent_path()) {
        const auto attrs = Attributes(parent, true);
        Require(attrs == INVALID_FILE_ATTRIBUTES || !(attrs & FILE_ATTRIBUTE_REPARSE_POINT),
            "Redirected locations cannot be changed: " + Display(parent));
    }
}

struct Handle {
    HANDLE value;
    explicit Handle(const fs::path& path, DWORD access, DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
        : value(CreateFileW(path.c_str(), access, share, nullptr, OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)) {
        CheckWin(value != INVALID_HANDLE_VALUE, "Could not open", path);
    }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    ~Handle() { CloseHandle(value); }
};

struct Identity {
    DWORD volume = 0;
    std::uint64_t file = 0;
    std::uint64_t created = 0;
    bool directory = false;
    bool operator==(const Identity& rhs) const {
        return volume == rhs.volume && file == rhs.file && created == rhs.created && directory == rhs.directory;
    }
};

Identity Identify(const Handle& handle, const fs::path& path) {
    BY_HANDLE_FILE_INFORMATION data{};
    CheckWin(GetFileInformationByHandle(handle.value, &data) != FALSE, "Could not identify", path);
    Require(!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT), "A redirected item cannot be changed.");
    const auto combine = [](DWORD high, DWORD low) { return (std::uint64_t(high) << 32) | low; };
    const auto id = combine(data.nFileIndexHigh, data.nFileIndexLow);
    Require(id != 0, "This filesystem did not provide a usable file identity.");
    return {data.dwVolumeSerialNumber, id,
        combine(data.ftCreationTime.dwHighDateTime, data.ftCreationTime.dwLowDateTime),
        (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0};
}

Identity Identify(const fs::path& path) { return Identify(Handle(path, FILE_READ_ATTRIBUTES), path); }

struct Step {
    Change change;
    std::shared_ptr<Identity> object;
    std::shared_ptr<Identity> fromParent, toParent;
};

Step Invert(Step step) {
    std::swap(step.change.from, step.change.to);
    std::swap(step.fromParent, step.toParent);
    if (step.change.kind == Kind::Create) step.change.kind = Kind::Remove;
    else if (step.change.kind == Kind::Remove) step.change.kind = Kind::Create;
    return step;
}

std::vector<Step> Reverse(const std::vector<Step>& steps) {
    std::vector<Step> result;
    result.reserve(steps.size());
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) result.push_back(Invert(*it));
    return result;
}

struct Command {
    std::string label;
    std::vector<Step> steps;
    fs::path unpackFolder;
    std::size_t unpackCount = 0;
};

void CheckGuard(const Guard& guard, const fs::path& path, bool descendants) {
    Require(!guard || !guard(path, descendants), "Stack is using or protecting " + Display(path));
}

void CheckTarget(const Step& step) {
    const auto& change = step.change;
    if (change.to.empty()) return;
    const bool caseRename = change.kind == Kind::Move && Same(change.from, change.to);
    Require(caseRename || Attributes(change.to, true) == INVALID_FILE_ATTRIBUTES,
        "An item already exists at " + Display(change.to));
}

void Apply(const Step& step) {
    const auto& change = step.change;
    if (!change.from.empty()) CheckPath(change.from);
    if (!change.to.empty()) CheckPath(change.to);
    CheckTarget(step);
    const auto checkParent = [](const fs::path& child, const std::shared_ptr<Identity>& expected) {
        if (expected) Require(Identify(child.parent_path()) == *expected,
            "The parent folder changed: " + Display(child.parent_path()));
    };
    checkParent(change.from, step.fromParent);
    checkParent(change.to, step.toParent);
    if (change.kind == Kind::Create) {
        CheckWin(CreateDirectoryW(change.to.c_str(), nullptr) != FALSE, "Could not create", change.to);
        try { *step.object = Identify(change.to); }
        catch (...) {
            RemoveDirectoryW(change.to.c_str());
            throw;
        }
        return;
    }

    const Handle item(change.from, DELETE | FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE);
    const auto current = Identify(item, change.from);
    Require(current == *step.object, "The item changed: " + Display(change.from));
    if (change.kind == Kind::Remove) {
        Require(current.directory, "Only an empty folder can be removed.");
        FILE_DISPOSITION_INFO disposition{TRUE};
        CheckWin(SetFileInformationByHandle(item.value, FileDispositionInfo, &disposition,
            sizeof(disposition)) != FALSE, "Could not remove the empty folder", change.from);
        return;
    }

    const auto parent = change.to.parent_path();
    const Handle destination(parent, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE);
    const auto destinationId = Identify(destination, parent);
    Require(destinationId == *step.toParent && destinationId.volume == current.volume,
        "The destination changed or is on another volume.");
    const auto name = change.to.wstring();
    const auto bytes = name.size() * sizeof(wchar_t);
    std::vector<unsigned char> buffer(sizeof(FILE_RENAME_INFO) + bytes);
    auto& rename = *reinterpret_cast<FILE_RENAME_INFO*>(buffer.data());
    rename.ReplaceIfExists = FALSE;
    rename.RootDirectory = nullptr;
    rename.FileNameLength = static_cast<DWORD>(bytes);
    std::copy(name.begin(), name.end(), rename.FileName);
    CheckWin(SetFileInformationByHandle(item.value, FileRenameInfo, &rename,
        static_cast<DWORD>(buffer.size())) != FALSE, "Could not move to", change.to);
    // Some Windows filesystems derive IDs from directory entries. Keep the
    // shared history reference current after moving the same open object.
    BY_HANDLE_FILE_INFORMATION after{};
    if (GetFileInformationByHandle(item.value, &after)) {
        const auto id = (std::uint64_t(after.nFileIndexHigh) << 32) | after.nFileIndexLow;
        if (id) step.object->file = id;
        step.object->created = (std::uint64_t(after.ftCreationTime.dwHighDateTime) << 32) |
            after.ftCreationTime.dwLowDateTime;
    }
}

} // namespace

struct History::State {
    std::vector<Command> undo, redo;
    std::vector<Step> recovery;
    using IdentityKey = std::pair<DWORD, std::uint64_t>;
    std::map<IdentityKey, std::weak_ptr<Identity>> identities;
    Command awaiting;
    bool awaitingRedo = false;
    unsigned currentConfirmation = 0, nextConfirmation = 0;

    std::shared_ptr<Identity> Reference(const fs::path& path) {
        const auto identity = Identify(path);
        auto& entry = identities[{identity.volume, identity.file}];
        if (auto existing = entry.lock(); existing && *existing == identity) return existing;
        auto result = std::make_shared<Identity>(identity);
        entry = result;
        return result;
    }

    void ApplyStep(const Step& step) {
        Apply(step);
        const auto& identity = *step.object;
        if (step.change.kind != Kind::Remove)
            identities[{identity.volume, identity.file}] = step.object;
    }

    Command Prepare(const Request& request) {
        const auto& target = request.target;
        CheckPath(target);
        Command command;
        auto create = [&](const fs::path& path) {
            auto identity = std::make_shared<Identity>();
            command.steps.push_back({{Kind::Create, {}, path, true}, identity, {}, Reference(path.parent_path())});
            return identity;
        };
        auto move = [&](const fs::path& from, const fs::path& to, std::shared_ptr<Identity> object,
                std::shared_ptr<Identity> parent) {
            command.steps.push_back({{Kind::Move, from, to, object->directory},
                std::move(object), Reference(from.parent_path()), std::move(parent)});
        };
        switch (request.action) {
        case Action::CreateFolder:
            command.label = "New Folder";
            create(target / ValidateName(fs::u8path(request.name)));
            break;
        case Action::RenameFolder: {
            command.label = "Rename";
            auto folder = Reference(target);
            Require(folder->directory && target != target.root_path(), "Select a folder to rename.");
            const auto rename = target.parent_path() / ValidateName(fs::u8path(request.name));
            if (target.native() != rename.native()) move(target, rename, folder, Reference(target.parent_path()));
            break;
        }
        case Action::Unpack: {
            command.label = "Unpack";
            command.unpackFolder = target;
            auto folder = Reference(target);
            Require(folder->directory && target != target.root_path(), "Select a folder to unpack.");
            const auto parent = Reference(target.parent_path());
            for (const auto& child : fs::directory_iterator(target)) {
                CheckPath(child.path());
                const auto item = Reference(child.path());
                move(child.path(), target.parent_path() / child.path().filename(), item, parent);
                ++command.unpackCount;
            }
            command.steps.push_back({{Kind::Remove, target, {}, true}, folder, parent, {}});
            break;
        }
        case Action::GroupImages:
        case Action::MoveImages: {
            const bool group = request.action == Action::GroupImages;
            command.label = group ? "Group Images" : "Move Images";
            std::vector<fs::path> sources = request.images;
            fs::path destination = target;
            std::shared_ptr<Identity> parent;
            if (group) {
                Require(sources.size() == 1 && !Same(target, sources.front()), "Drop one image onto a different image.");
                sources.push_back(target);
                auto name = target.stem();
                try { ValidateName(name); } catch (const std::exception&) { name = L"New Folder"; }
                destination = target.parent_path() / name;
                unsigned index = 2;
                while (Attributes(destination, true) != INVALID_FILE_ATTRIBUTES) {
                    const auto suffix = L" (" + std::to_wstring(index++) + L")";
                    auto stem = name.wstring();
                    if (stem.size() > 255 - suffix.size()) stem.resize(255 - suffix.size());
                    destination = target.parent_path() / (stem + suffix);
                }
                parent = create(destination);
            } else {
                parent = Reference(destination);
                Require(parent->directory, "The destination must be a folder.");
            }
            for (const auto& source : sources) {
                CheckPath(source);
                auto item = Reference(source);
                Require(!item->directory, "Only image files can be dragged.");
                const auto to = destination / source.filename();
                if (!Same(source, to)) move(source, to, item, parent);
            }
            break;
        }
        default: throw std::runtime_error("Unsupported file operation.");
        }
        return command;
    }

    void Verify(const std::vector<Step>& steps, const Guard& guard) {
        const auto pathLess = [](const fs::path& a, const fs::path& b) {
            return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
        };
        std::set<fs::path, decltype(pathLess)> destinations(pathLess), movingOut(pathLess);
        const Step* precedingCreate = nullptr;
        for (const auto& step : steps) {
            const auto& change = step.change;
            if (!change.from.empty()) {
                CheckPath(change.from);
                Require(Identify(change.from) == *step.object, "The item changed: " + Display(change.from));
                if (step.fromParent)
                    Require(Identify(change.from.parent_path()) == *step.fromParent, "The source folder changed.");
                CheckGuard(guard, change.from, change.directory);
                if (change.kind == Kind::Move) movingOut.insert(change.from);
            }
            if (!change.to.empty()) {
                CheckPath(change.to);
                CheckGuard(guard, change.to, false);
                CheckTarget(step);
                Require(destinations.insert(change.to).second,
                    "Multiple items have the same destination name: " + Display(change.to));
                const bool newParent = precedingCreate && precedingCreate->object == step.toParent;
                DWORD volume = 0;
                if (newParent) {
                    volume = precedingCreate->toParent->volume;
                } else if (step.toParent) {
                    const auto parent = Identify(change.to.parent_path());
                    Require(parent == *step.toParent, "The destination folder changed.");
                    volume = parent.volume;
                }
                if (change.kind == Kind::Move)
                    Require(step.object->volume == volume, "Moving between volumes is unavailable.");
            }
            if (change.kind == Kind::Create) precedingCreate = &step;
            if (change.kind == Kind::Remove) {
                for (const auto& child : fs::directory_iterator(change.from)) {
                    Require(movingOut.count(child.path()) != 0,
                        "The folder contains an unexpected item: " + Display(child.path()));
                }
            }
        }
    }

    bool Execute(const std::vector<Step>& steps, Result& result) {
        std::size_t applied = 0;
        try {
            for (; applied < steps.size(); ++applied) ApplyStep(steps[applied]);
        } catch (const std::exception& error) {
            result.message = error.what();
            while (applied) {
                try { ApplyStep(Invert(steps[applied - 1])); }
                catch (const std::exception& rollback) {
                    result.message += "\nCould not roll back: ";
                    result.message += rollback.what();
                    const std::vector<Step> remaining(steps.begin(), steps.begin() + applied);
                    recovery = Reverse(remaining);
                    for (const auto& step : remaining) result.changes.push_back(step.change);
                    break;
                }
                --applied;
            }
            return false;
        }
        for (const auto& step : steps) result.changes.push_back(step.change);
        return true;
    }

    void Run(const Request& request, const Guard& guard, Result& result) {
        for (auto it = identities.begin(); it != identities.end();) {
            const auto identity = it->second.lock();
            if (!identity || it->first != IdentityKey{identity->volume, identity->file})
                it = identities.erase(it);
            else ++it;
        }
        if (request.action == Action::Recover) {
            while (!recovery.empty()) {
                const auto& step = recovery.front();
                Verify({step}, guard);
                ApplyStep(step);
                result.changes.push_back(step.change);
                recovery.erase(recovery.begin());
            }
            result.message = "File operation rolled back.";
            return;
        }
        Require(recovery.empty(), "Finish recovering the interrupted operation first.");
        const bool confirm = request.action == Action::Confirm;
        const bool redoing = request.action == Action::Redo || (confirm && awaitingRedo);
        const bool undoing = request.action == Action::Undo;
        Command command;
        if (confirm) {
            Require(request.confirmation && request.confirmation == currentConfirmation,
                "This confirmation is no longer current.");
            command = std::move(awaiting);
        } else if (undoing || redoing) {
            const auto& stack = undoing ? undo : redo;
            if (stack.empty()) return;
            command = stack.back();
        } else command = Prepare(request);
        awaiting = {};
        currentConfirmation = 0;
        if (command.steps.empty()) return;

        const auto steps = undoing ? Reverse(command.steps) : command.steps;
        Verify(steps, guard);
        if (!confirm && !undoing && command.unpackCount > 50) {
            result.confirmationFolder = command.unpackFolder;
            result.confirmationCount = command.unpackCount;
            result.confirmation = currentConfirmation = ++nextConfirmation;
            awaiting = std::move(command);
            awaitingRedo = redoing;
            return;
        }
        if (!Execute(steps, result)) return;
        if (undoing) {
            undo.pop_back();
            redo.push_back(std::move(command));
        } else {
            if (redoing) redo.pop_back();
            else redo.clear();
            undo.push_back(std::move(command));
        }
    }
};

bool AffectsLocation(const Change& change, const fs::path& location) {
    if (location.empty()) return false;
    const auto within = [](const fs::path& path, const fs::path& root) {
        if (path.empty() || root.empty()) return false;
        auto current = path.begin();
        for (const auto& part : root) {
            if (current == path.end() || !Same(*current++, part)) return false;
        }
        return true;
    };
    return within(change.from, location) || within(change.to, location) ||
        (change.directory && within(location, change.from));
}

fs::path RemapLocation(fs::path location, const std::vector<Change>& changes) {
    for (const auto& change : changes) {
        if (!change.directory || change.from.empty()) continue;
        auto suffix = location.begin();
        const auto match = std::all_of(change.from.begin(), change.from.end(), [&](const fs::path& part) {
            return suffix != location.end() && Same(*suffix++, part);
        });
        if (!match) continue;
        if (change.kind == Kind::Remove) {
            location = change.from.parent_path();
        } else {
            auto newLocation = change.to;
            for (; suffix != location.end(); ++suffix) newLocation /= *suffix;
            location = std::move(newLocation);
        }
    }
    return location;
}

History::History() : state_(std::make_unique<State>()) {}
History::~History() = default;

Result History::Run(const Request& request, const Guard& guard) {
    Result result;
    try { state_->Run(request, guard, result); }
    catch (const std::exception& error) { result.message = error.what(); }
    if (!state_->undo.empty()) result.undo = "Undo " + state_->undo.back().label;
    if (!state_->redo.empty()) result.redo = "Redo " + state_->redo.back().label;
    result.recovery = !state_->recovery.empty();
    return result;
}

} // namespace Stack::BrowserFiles
