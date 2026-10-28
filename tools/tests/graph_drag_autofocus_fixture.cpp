// Included by the pointer-lifetime generator. Layout preparation is a boundary
// double; its success/failure branch and camera handoff are production code.
namespace DragAutoFocusCheck {
// Graph/display and input services are boundary doubles. Hit testing and the
// accepted click branch below are extracted unchanged from RenderInteraction.
struct Boundary {bool is_object()const{return false;}};
struct Group {int id=7;ImVec2 position{300,100},size{200,150};Boundary compoundBoundary;};
struct Graph:EditorNodeGraph::Graph {
    std::vector<Group> groups{Group{}};
    const auto& GetGroups()const{return groups;}
};
using Node=EditorNodeGraph::Node;
namespace ImGui {
using ::ImGui::GetIO;using ::ImGui::IsMouseClicked;
double now=0; double GetTime(){return now;}
}
// PRODUCTION_AUTOFOCUS_STATE
// PRODUCTION_AUTOFOCUS_HELPERS
struct EditorModule {
    Graph graph;Graph&GetNodeGraph(){return graph;}
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
    enum class GraphMouseOwner {Canvas,NodeContent};
    GraphMouseOwner m_MouseOwner=GraphMouseOwner::Canvas;
    struct Motion {std::unordered_map<int,int> groups;};
    std::unique_ptr<Motion> m_ExpansionMotion;
    int m_HoveredGroupId=-1,m_DragGroupId=-1,m_ResizingGroupId=-1;
    bool m_BoxSelecting=false;double m_GroupPointerGestureStartedAt=-1;
    float panX=0,panY=0;
    bool groupVisible=true;
    bool LayoutGroupVisible(int)const{return groupVisible;}
    EditorNodeGraph::GraphRect DisplayGroupRect(const Group&g)const {
        return {g.position,{g.position.x+g.size.x,g.position.y+g.size.y}};
    }
    ImVec2 ScreenToGraph(ImVec2 p)const{return {(p.x-panX)/m_Zoom,(p.y-panY)/m_Zoom};}
    bool IsPointInRect(ImVec2 p,ImVec2 a,ImVec2 b)const {
        return p.x>=a.x&&p.y>=a.y&&p.x<=b.x&&p.y<=b.y;
    }
    void AcceptGroup(EditorModule*editor,bool graphHovered,ImVec2 mouse) {
// PRODUCTION_GROUP_HANDOFF
    }
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
    // Header drag and resize corner must both own the displayed camera. The
    // rejected cases exercise the production hover/owner/hit/click gates.
    for(bool resize:{false,true})for(int rejected=0;rejected<7;++rejected) {
        EditorModule editor;EditorNodeGraphUI ui;
        ImGui::now=0;
        editor.RequestGraphNodeAutoFocus(1,{300,100},{200,150},0,0,1);
        editor.ApplyGraphAutoFocusFrame(800,600,ui.panX,ui.panY,ui.m_Zoom);
        ImGui::now=1./60;
        editor.ApplyGraphAutoFocusFrame(800,600,ui.panX,ui.panY,ui.m_Zoom);
        const auto anchor=resize?ImVec2(495,245):ImVec2(310,110);
        ImVec2 mouse{ui.panX+anchor.x*ui.m_Zoom,ui.panY+anchor.y*ui.m_Zoom};
        if(rejected==2)ui.m_MouseOwner=EditorNodeGraphUI::GraphMouseOwner::NodeContent;
        if(rejected==3)mouse={-1000,-1000};
        if(rejected==5)ui.groupVisible=false;
        if(rejected==6) {ui.m_ExpansionMotion=std::make_unique<EditorNodeGraphUI::Motion>();ui.m_ExpansionMotion->groups[7]=1;}
        ::ImGui::clicked[0]=rejected!=4;
        ::ImGui::io.MouseDelta={0,0};
        const float x=ui.panX,y=ui.panY,z=ui.m_Zoom;
        const float screenX=x+anchor.x*z;
        ui.AcceptGroup(&editor,rejected!=1,mouse);
        const bool accepted=rejected==0;
        Check((resize?ui.m_ResizingGroupId:ui.m_DragGroupId)==(accepted?7:-1),
            "Group ownership gate changed");
        Check(editor.m_GraphAutoFocus.trackingActive!=accepted,"Group camera handoff ownership changed");
        Check(ui.panX==x&&ui.panY==y&&ui.m_Zoom==z,"Group takeover snapped camera");
        ImGui::now=2./60;
        editor.ApplyGraphAutoFocusFrame(800,600,ui.panX,ui.panY,ui.m_Zoom);
        if(accepted) {
            Check(ui.panX+anchor.x*ui.m_Zoom==screenX,"Stationary group grip drifted under autofocus");
            editor.RequestGraphNodeAutoFocus(2,{500,200},{160,100},ui.panX,ui.panY,ui.m_Zoom);
            editor.ApplyGraphAutoFocusFrame(800,600,ui.panX,ui.panY,ui.m_Zoom);
            ImGui::now+=.1;
            editor.ApplyGraphAutoFocusFrame(800,600,ui.panX,ui.panY,ui.m_Zoom);
            Check(editor.m_GraphAutoFocus.trackingActive&&
                (ui.panX!=x||ui.panY!=y||ui.m_Zoom!=z),"Later focus after group gesture failed");
        } else Check(ui.panX!=x||ui.panY!=y||ui.m_Zoom!=z,"Rejected group gesture interrupted focus");
    }
    return failures!=before;
}
} // namespace DragAutoFocusCheck
