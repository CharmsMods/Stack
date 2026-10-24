#!/usr/bin/env python3
"""Generate focused Value-control coverage from unchanged production methods.

Real Graph and ProjectGraphHistory code is linked. The UI supplies a deterministic
Scalar edit; the editor's unrelated rendering and Compound lookup are boundary
doubles. This checks edit notification/ownership, not native UI or GPU behavior.

Link the generated source with:
  src/Editor/NodeGraph/EditorNodeGraph.cpp
  src/Editor/NodeGraph/Model/EditorNodeGraphLayoutValidation.cpp
  src/NodeMath/FirstClassValue.cpp
  src/NodeMath/DescriptorSerialization.cpp
  src/NodeMath/ContractTypes.cpp
  src/NodeMath/CompoundDefinition.cpp
Include src and the ImGui include directory. Use function/data sections and dead
code elimination, as other reduced-editor component tests do.
"""
import argparse
from pathlib import Path


def extract_method(root, path, signature):
    source = (root / path).read_text(encoding="utf-8")
    if source.count(signature) != 1:
        raise ValueError(f"Expected one production method: {signature}")
    start = source.index(signature)
    cursor = source.index("{", start) + 1
    depth = 1
    while depth:
        if source[cursor] == "{":
            depth += 1
        elif source[cursor] == "}":
            depth -= 1
        cursor += 1
    return source[start:cursor]


PREAMBLE = r'''

#include "Project/ProjectGraphHistory.h"
#include "Editor/NodeGraph/EditorNodeGraph.h"
#include <algorithm>
#include <map>
#include <memory>
#include <iostream>
#include <stdexcept>
struct ImVec2 { float x=0,y=0; ImVec2()=default; ImVec2(float a,float b):x(a),y(b){} };
struct ImVec4 { ImVec4(float,float,float,float){} };
struct ImDrawList {};
using ImTextureID=std::intptr_t;
constexpr int ImGuiDataType_S64=0, ImGuiDataType_Double=1, ImGuiColorEditFlags_NoInputs=0;
constexpr int ImGuiMouseButton_Left=0,ImGuiMouseButton_Right=1;
#define IM_ARRAYSIZE(x) (sizeof(x)/sizeof((x)[0]))
namespace ImGui {
inline bool change=false;
inline bool IsMouseDown(int){return false;}
template<class...T> void TextDisabled(T...){ }
template<class...T> void TextColored(T...){ }
template<class...T> void TextWrapped(T...){ }
inline void SetNextItemWidth(float){}
inline bool InputDouble(const char*,double* value,double,double,const char*) {if(!change)return false;*value+=1;return true;}
inline bool InputScalar(const char*,int,void*){return false;}
inline bool InputScalarN(const char*,int,void*,int){return false;}
inline bool Button(const char*){return false;}
inline void BeginDisabled(bool){} inline void EndDisabled(){}
inline void Dummy(ImVec2){} inline void SameLine(float,float){}
inline void Image(ImTextureID,ImVec2,ImVec2,ImVec2){}
inline ImVec2 GetItemRectMin(){return{};} inline ImVec2 GetItemRectMax(){return{};}
}
namespace ImGuiExtras {
inline bool NodeCheckbox(const char*,const char*,bool*,float){return false;}
inline void SetNextNodeNumericDefault(double){}
inline bool NodeTextMultiline(const char*,const char*,std::string&,float,int){return false;}
inline bool NodeColorEdit4(const char*,const char*,float*,int,float){return false;}
inline bool NodeCombo(const char*,const char*,int*,const char* const*,int,float){return false;}
}
namespace Stack::NodeEditors { inline bool DrawFramingControls(Stack::NodeMath::ReformatSettings&,float){return false;} }
struct NodeLayoutMetrics {float itemGap=1;}; struct NodeSurfaceSpec {};struct GraphStyleTokens {};
inline ImVec2 FitPreviewRect(ImVec2,ImVec2){return{};}
inline void DrawPreviewFrame(ImDrawList*,ImVec2,ImVec2,const GraphStyleTokens&,float){}
struct Project {EditorNodeGraph::Graph graph;Stack::Project::ProjectGraphHistory graphHistory;bool restoringGraphHistory=false;bool dirty=false;int editRevision=0;};
struct RawDraft {EditorNodeGraph::Graph graph;bool dirty=false,affectsPixels=false;};
class EditorModule {
public:
 std::unique_ptr<Project> m_Project=std::make_unique<Project>();
 std::optional<RawDraft> m_RawLayerMaskWorkspace;
 std::vector<int> notified;
 bool IsEditingRawLayerMaskGraph()const{return m_RawLayerMaskWorkspace.has_value();}
 EditorNodeGraph::Graph& GetNodeGraph(){return m_RawLayerMaskWorkspace?m_RawLayerMaskWorkspace->graph:m_Project->graph;}
 Stack::Project::ProjectGraphState CaptureProjectGraphState() const {Stack::Project::ProjectGraphState s;s.graph=m_Project->graph;return s;}
 void ObserveProjectGraphEdit(bool);
 void MarkGraphEdited(int,bool=true);
 void MarkDirty(){m_Project->dirty=true;++m_Project->editRevision;}
 void MarkRenderDirty(int id){notified.push_back(id);MarkDirty();}
};
class EditorNodeGraphUI {
public:
 struct NodeContentRenderContext {
  EditorModule* editor; EditorNodeGraph::Graph& graph;EditorNodeGraph::Node& node;
  NodeLayoutMetrics metrics;NodeSurfaceSpec nodeSurfaceSpec;GraphStyleTokens graphStyle;ImDrawList* drawList=nullptr;
  ImVec2 previewSize;float controlWidth=100,safeContentWidth=100,logicalControlWidth=100,logicalSafeContentWidth=100,uiScale=1,contentScale=1,itemGap=1,sectionGap=1;
  bool richExpandedSurface=false,selected=false;
  std::function<void()> captureIfActive=[]{};
  std::function<bool(const char*,const char*,float*,float,float)> renderSlider=[](auto...){return false;};
  std::function<void()> drawInlineSeparator=[]{};
 };
 std::map<int,ImVec2> m_GraphPreviewSizes;
 bool RenderUtilityNodeContent(NodeContentRenderContext&);
 unsigned GetGraphPreviewTexture(EditorModule*,const EditorNodeGraph::Node&){return 0;}
 const EditorNodeGraph::Node* FindCachedNode(const EditorNodeGraph::Graph& g,int id){return g.FindNode(id);}
};
// The unrelated Compound branch is not exercised. Its graph lookup is a boundary test double.
namespace EditorNodeGraph {const Stack::NodeMath::CompoundDefinition* Graph::FindCompoundDefinition(const Stack::NodeMath::DefinitionReference&)const{return nullptr;}Stack::NodeMath::CompoundDefinition* Graph::FindCompoundDefinition(const Stack::NodeMath::DefinitionReference&){return nullptr;}}
'''

TESTS = r'''

void Require(bool pass, const char* message) {
    if (!pass) throw std::runtime_error(message);
}
void Setup(EditorModule& editor, int targets) {
    auto& graph = editor.GetNodeGraph();
    auto& nodes = graph.EditNodes();
    EditorNodeGraph::Node value;
    value.id = 1;
    value.instanceUuid = "typed-value";
    value.kind = EditorNodeGraph::NodeKind::Value;
    value.value.value = Stack::NodeMath::MakeUniformScalar(1.0);
    nodes.push_back(std::move(value));
    for (int index = 0; index < targets; ++index) {
        EditorNodeGraph::Node target;
        target.id = 2 + index;
        target.kind = EditorNodeGraph::NodeKind::TechnicalImage;
        nodes.push_back(std::move(target));
        EditorNodeGraph::Link link;
        link.fromNodeId = 1;
        link.fromSocketId = EditorNodeGraph::kValueOutputSocketId;
        link.toNodeId = 2 + index;
        link.toSocketId = EditorNodeGraph::kExposureValueInputSocketId;
        graph.EditLinks().push_back(std::move(link));
    }
    editor.m_Project->graphHistory.Reset(editor.CaptureProjectGraphState());
}
void Draw(EditorModule& editor, bool changed) {
    EditorNodeGraphUI ui;
    auto& graph = editor.GetNodeGraph();
    EditorNodeGraphUI::NodeContentRenderContext context{&editor, graph, *graph.FindNode(1)};
    ImGui::change = changed;
    Require(ui.RenderUtilityNodeContent(context), "Value did not use utility controls");
}
int main() try {
    EditorModule isolated;
    Setup(isolated, 0);
    const auto before = Stack::NodeMath::SerializeFirstClassValue(
        isolated.GetNodeGraph().FindNode(1)->value.value);
    Draw(isolated, true);
    Require(before != Stack::NodeMath::SerializeFirstClassValue(
        isolated.GetNodeGraph().FindNode(1)->value.value), "Control did not edit authored Value");
    Require(isolated.notified == std::vector<int>{1}, "Disconnected Value must own one edit notification");
    Require(isolated.m_Project->dirty && isolated.m_Project->editRevision == 1,
        "Disconnected Value must dirty and revise its project");
    Require(isolated.m_Project->graphHistory.CanUndo(), "Disconnected Value edit must enter history");

    EditorModule fanout;
    Setup(fanout, 2);
    Draw(fanout, true);
    Require(fanout.notified == std::vector<int>{1} && fanout.m_Project->editRevision == 1,
        "Value fan-out must not duplicate project edit notifications");
    auto& history = fanout.m_Project->graphHistory;
    Require(history.CanUndo(), "Connected Value edit must enter history");
    const auto* undo = history.Candidate(false);
    Require(undo && std::get<double>(undo->graph.FindNode(1)->value.value.payload) == 1.0,
        "The first undo must restore the old Value, not a duplicate new snapshot");
    history.AcceptRestore(false);
    Require(!history.CanUndo(), "One Value edit must create exactly one undo entry");
    const auto* redo = history.Candidate(true);
    Require(redo && std::get<double>(redo->graph.FindNode(1)->value.value.payload) == 2.0,
        "Redo must restore the new Value");

    EditorModule raw;
    raw.m_RawLayerMaskWorkspace.emplace();
    Setup(raw, 0);
    Draw(raw, true);
    Require(raw.m_RawLayerMaskWorkspace->dirty && raw.m_RawLayerMaskWorkspace->affectsPixels,
        "Disconnected RAW Value edit must schedule its owned draft commit");
    Require(!raw.m_Project->dirty && !raw.m_Project->graphHistory.CanUndo(),
        "RAW draft must not mutate unrelated project-graph history before commit");

    EditorModule untouched;
    Setup(untouched, 2);
    Draw(untouched, false);
    Require(untouched.notified.empty() && !untouched.m_Project->dirty &&
        !untouched.m_Project->graphHistory.CanUndo(), "Drawing unchanged Value must not create an edit");
    std::cout << "Value controls: disconnected dirty/history, fan-out single undo/redo, "
        "RAW draft ownership, and unchanged drawing passed. CPU component only.\n";
    return 0;
} catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
}
'''



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    methods = [
        ("src/Editor/Internal/RawLab/EditorModuleRawLayerGraph.cpp",
         "void EditorModule::MarkGraphEdited("),
        ("src/Editor/Internal/EditorModuleGraphHistory.cpp",
         "void EditorModule::ObserveProjectGraphEdit("),
        ("src/Editor/NodeGraph/UI/EditorNodeGraphUINodes.cpp",
         "bool EditorNodeGraphUI::RenderUtilityNodeContent("),
    ]
    generated = PREAMBLE + "\n" + "\n".join(
        extract_method(root, path, signature) for path, signature in methods) + "\n" + TESTS
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated, encoding="utf-8")


if __name__ == "__main__":
    main()
