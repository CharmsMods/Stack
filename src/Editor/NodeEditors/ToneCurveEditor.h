#pragma once
class EditorModule;
namespace Stack::Editor {
struct RawWorkspaceEditContext;
bool DrawToneCurve(EditorModule& editor,RawWorkspaceEditContext& context);
bool DrawToneCurveSettings(EditorModule& editor,RawWorkspaceEditContext& context);
bool ResetPanelToneCurves(RawWorkspaceEditContext& context,bool rgb);
bool ResetToneCurve(EditorModule& editor,RawWorkspaceEditContext& context);
}
