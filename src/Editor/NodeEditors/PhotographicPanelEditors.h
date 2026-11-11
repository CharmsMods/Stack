#pragma once
#include "Raw/Curves/CurveTypes.h"
class EditorModule;
namespace Stack::Editor {
struct RawWorkspaceEditContext;
namespace RawLabInternal { struct CurveTonalSliderDomain; }
bool DrawPanelTonalRollers(RawWorkspaceEditContext&,std::vector<RawRecipe::RawBezierCurvePoint>&,
    const RawLabInternal::CurveTonalSliderDomain&,int maximumPoints,bool locked);
bool DrawLightPanel(EditorModule&,RawWorkspaceEditContext&);
bool DrawCalibrationPanel(EditorModule&,RawWorkspaceEditContext&);
bool DrawHslPanel(EditorModule&,RawWorkspaceEditContext&);
bool DrawLightPanelSettings(EditorModule&,RawWorkspaceEditContext&);
bool DrawCalibrationPanelSettings(EditorModule&,RawWorkspaceEditContext&);
void DrawMissingPanelControls(int order);
}
