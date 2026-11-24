
#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Stack::BrowserFiles {

enum class Action { CreateFolder, RenameFolder, GroupImages, MoveImages, Unpack, Undo, Redo, Confirm, Recover };

struct Request {
    Action action;
    std::filesystem::path target;
    std::vector<std::filesystem::path> images;
    std::string name;
    unsigned confirmation = 0;
};

struct Change {
    enum class Kind { Move, Create, Remove };
    Kind kind;
    std::filesystem::path from, to;
    bool directory = false;
};

bool AffectsLocation(const Change&, const std::filesystem::path&);
std::filesystem::path RemapLocation(std::filesystem::path, const std::vector<Change>&);

struct Result {
    std::vector<Change> changes;
    std::string message, undo, redo;
    std::filesystem::path confirmationFolder;
    std::size_t confirmationCount = 0;
    unsigned confirmation = 0;
    bool recovery = false;
};

// Called only on the serialized mutation worker. The second argument requests
// protection for descendants too, for example when renaming a subtree.
using Guard = std::function<bool(const std::filesystem::path&, bool)>;

class History {
public:
    History();
    ~History();
    Result Run(const Request&, const Guard& = {});

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace Stack::BrowserFiles
