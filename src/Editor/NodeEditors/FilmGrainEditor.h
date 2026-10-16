#pragma once
class EditorModule;
namespace Stack::Editor {
struct RawWorkspaceEditContext;
bool DrawFilmGrain(EditorModule&,RawWorkspaceEditContext&);
bool ResetFilmGrain(EditorModule&,RawWorkspaceEditContext&);
}
