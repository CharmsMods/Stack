// Included by the pointer-lifetime generator. Layout preparation is a boundary
// double; its success/failure branch and camera handoff are production code.
namespace DragAutoFocusCheck {
using Graph=EditorNodeGraph::Graph;
using Node=EditorNodeGraph::Node;
namespace ImGui { double now=0; double GetTime(){return now;} }
// PRODUCTION_AUTOFOCUS_STATE
// PRODUCTION_AUTOFOCUS_HELPERS
struct EditorModule {
    GraphAutoFocusState m_GraphAutoFocus;
    void ApplyGraphAutoFocusFrame(float,float,float&,float&,float&);
    void RequestGraphNodeAutoFocus(int,const ImVec2&,const ImVec2&,float,float,float);
    void CancelGraphAutoFocusTracking();
    void ShowUiNotification(UiNotificationSeverity,const std::string&,const char*){}
};
// PRODUCTION_AUTOFOCUS_METHODS
struct Room {bool accepted=true;template<class...T>bool Begin(T&&...){return accepted;}};
struct Drag {Room room;std::vector<int> held;std::unordered_set<int> easedHeld;};
struct EditorNodeGraphUI {
    std::unique_ptr<Drag> m_DragLayout;
    std::unordered_map<int,int> m_LayoutNodeMotion;
    int m_LayoutRestingGroups=0;
    bool m_SmoothZoomActive=true;
    float m_Zoom=1,m_ZoomTarget=2;
    int MeasureNodeForLayout(const Graph&,const Node&){return 0;}
    bool Begin(EditorModule* editor,bool accepted) {
        Graph graph;int anchors=0,groups=0,rests=0;std::string error;
        auto drag=std::make_unique<Drag>();drag->room.accepted=accepted;
// PRODUCTION_DRAG_HANDOFF
    }
};
int Run() {
    const int before=failures;
    for(bool accepted:{false,true}) {
        EditorModule editor;EditorNodeGraphUI ui;float x=0,y=0,z=1;
        ImGui::now=0;
        editor.RequestGraphNodeAutoFocus(1,{300,100},{200,150},x,y,z);
        editor.ApplyGraphAutoFocusFrame(800,600,x,y,z);
        ImGui::now=1./60;
        editor.ApplyGraphAutoFocusFrame(800,600,x,y,z);
        const float startX=x,startY=y,startZoom=z;
        ui.m_Zoom=z;
        Check(ui.Begin(&editor,accepted)==accepted,"Drag preparation outcome changed");
        Check(editor.m_GraphAutoFocus.trackingActive!=accepted,
            "Only a successful node drag may cancel autofocus");
        Check(x==startX && y==startY && z==startZoom,"Drag takeover snapped camera");
        ImGui::now=2./60;
        editor.ApplyGraphAutoFocusFrame(800,600,x,y,z);
        if(accepted) {
            Check(x==startX && y==startY && z==startZoom,"Stationary held node drifted under autofocus");
            editor.ApplyGraphAutoFocusFrame(900,650,x,y,z);
            Check(x==startX && y==startY && z==startZoom,"Resize revived abandoned autofocus");
            editor.RequestGraphNodeAutoFocus(2,{500,200},{160,100},x,y,z);
            editor.ApplyGraphAutoFocusFrame(900,650,x,y,z);
            ImGui::now+=.1;
            editor.ApplyGraphAutoFocusFrame(900,650,x,y,z);
            Check(editor.m_GraphAutoFocus.trackingActive && (x!=startX || y!=startY || z!=startZoom),
                "A later explicit focus request no longer animates");
        } else Check(x!=startX || y!=startY || z!=startZoom,"Rejected drag interrupted autofocus");
    }
    return failures!=before;
}
} // namespace DragAutoFocusCheck
