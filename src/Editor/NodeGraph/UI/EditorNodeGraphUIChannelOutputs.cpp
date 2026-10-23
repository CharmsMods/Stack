#include "Editor/NodeGraph/EditorNodeGraphUI.h"
#include "Editor/EditorModule.h"
#include "Editor/Internal/GraphEditorCommands.h"
#include "Editor/NodeGraph/ChannelOutputs.h"
#include "Notifications/NotificationPresenter.h"

void EditorNodeGraphUI::ExposeChannelSocket(EditorModule* editor,int id,const std::string& base,const std::string& component) {
    const auto* node=editor->GetNodeGraph().FindNode(id);
    if(!node)return;
    const auto start=ScreenToGraph(OutputPinScreenPos(*node,base));
    std::string output,error;
    if(!Stack::Editor::ApplyGraphCommand(*editor,[&](auto& graph) {
        output=EditorNodeGraph::ExposeChannelOutput(graph,id,base,component,false,&error);
        if(output.empty())throw std::runtime_error(error);
    },&error)) {
        editor->ShowUiNotification(UiNotificationSeverity::Error,error,"graph-channel-output");return;
    }
    if(!Stack::Notifications::SystemReducedMotion()) {
        auto motion=std::make_unique<ExpansionMotion>();motion->channelFold=true;
        motion->columns.emplace(id,0.f);
        motion->socketOrigins.emplace(EditorNodeGraph::GraphOutputIdentity(id,output),start);
        m_ExpansionMotion=std::move(motion);
        AdoptLayoutMotionOwner(editor->GetGraphEditorContext());++m_WireWorldRevision;
    }
}
