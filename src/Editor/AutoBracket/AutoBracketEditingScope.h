#pragma once
#include "App/WorkspaceInputScope.h"
#include <memory>
class EditorModule;
namespace Stack::AutoBracket {
// Guards polling-based tools as well as widgets. Gallery drawing stays outside this scope.
class EditingScope {
public:
    EditingScope(EditorModule&,ImVec2 minimum,ImVec2 size);
    ~EditingScope();
private:
    bool active_=false;
    ImVector<ImWchar> characters_;
    std::unique_ptr<Workspace::InputScope> input_;
};
}
