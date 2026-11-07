#include "AutoBracketEditingScope.h"
namespace Stack::AutoBracket {
// Automatic work yields through its coordinator; it never captures editor input.
EditingScope::EditingScope(EditorModule&,ImVec2,ImVec2) {}
EditingScope::~EditingScope()=default;
}
