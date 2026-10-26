#!/usr/bin/env python3
"""Bounded CPU checks of production preset publication/warmup method bodies.

Uses the real PresetManager declaration and public operations, temporary files,
and deterministic task/clock/graph-decoder stand-ins. No UI, GPU, user files, or
private-state hooks. Checks cache identity and scheduling, not frame timings.
"""
import argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
manager = (root / 'src/Presets/PresetManager.cpp').read_text()
drawer = (root / 'src/Editor/NodeGraph/UI/EditorNodeGraphUIPresetsDrawer.cpp').read_text()
header = (root / 'src/Editor/NodeGraph/EditorNodeGraphUI.h').read_text()


def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end] + '\n'


pre = r'''
#include "Presets/PresetManager.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <unordered_map>
#include <utility>
void Require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
std::filesystem::path testRoot;
namespace AppPaths { std::filesystem::path GetPresetsDirectory(){return testRoot;} }
namespace AppVersion { constexpr const char* kVersionString="test-version"; }
void glDeleteTextures(int,const unsigned*){}
std::string BuildTimestampString(){return "same-second";}
std::string BuildPresetId(){static int next=0;return "preset-"+std::to_string(++next);}
namespace Async {
class TaskSystem {
public:
    using Task=std::function<void()>;
    static TaskSystem& Get(){static TaskSystem instance;return instance;}
    std::deque<Task> workers,main;
    bool rejectNext=false;
    bool Submit(const char*,Task task){if(std::exchange(rejectNext,false))return false;workers.push_back(std::move(task));return true;}
    bool PostToMain(Task task){main.push_back(std::move(task));return true;}
    void Workers(){while(!workers.empty()){auto task=std::move(workers.front());workers.pop_front();task();}}
    void Main(){while(!main.empty()){auto task=std::move(main.front());main.pop_front();task();}}
    void Drain(){Workers();Main();}
};
}
namespace StackBinaryFormat {
bool failPayloadRead=false;
bool WriteNodePresetFile(const std::filesystem::path& path,const NodePresetDocument& document){
    const auto& m=document.metadata;
    json value={{"id",m.id},{"name",m.displayName},{"timestamp",m.timestamp},{"version",m.savedWithVersion},
        {"nodes",m.nodeCount},{"inputs",m.inputCount},{"outputs",m.outputCount},{"graph",document.graphPayload}};
    std::ofstream out(path);out<<value.dump();out.close();
    static int write=0;
    std::filesystem::last_write_time(path,std::filesystem::file_time_type{}+std::chrono::seconds(++write));
    return bool(out);
}
bool ReadNodePresetFile(const std::filesystem::path& path,NodePresetDocument& document,const NodePresetLoadOptions& options){
    if(failPayloadRead&&options.includeGraphPayload)return false;
    std::ifstream in(path);if(!in)return false;json value;in>>value;
    auto& m=document.metadata;m.id=value["id"];m.displayName=value["name"];m.timestamp=value["timestamp"];m.savedWithVersion=value["version"];
    m.nodeCount=value["nodes"];m.inputCount=value["inputs"];m.outputCount=value["outputs"];
    if(options.includeGraphPayload)document.graphPayload=value["graph"];
    return true;
}
}
'''
methods = ''
for signature in [
    'std::string TrimWhitespace(', 'std::string SanitizeFileStem(',
    'void ScrubPresetImageNodePayload(', 'bool SanitizePresetGraphPayload(',
    'PresetManager& PresetManager::Get()', 'PresetManager::PresetManager()',
    'PresetManager::~PresetManager()', 'void PresetManager::EnsureDirectories()',
    'void PresetManager::ReleaseTexture(', 'std::uintmax_t PresetManager::BuildPresetSignature()',
    'void PresetManager::RefreshPresets()', 'void PresetManager::RequestBackgroundGraphPayloadPreload(',
    'bool PresetManager::SaveUserPreset(', 'bool PresetManager::TryGetPreloadedPresetPayload(',
    'bool PresetManager::ReadUserPresetDocument(', 'bool PresetManager::WriteUserPresetDocument(',
    'bool PresetManager::RenameUserPreset(', 'bool PresetManager::DeleteUserPreset(',
    'bool PresetManager::OverwriteUserPreset(']:
    methods += method(manager, signature)

ui = r'''
int tokenBuilds=0,graphBuilds=0;
struct TestClock {
    static std::chrono::steady_clock::time_point now(){
        static auto value=std::chrono::steady_clock::time_point{};
        value+=std::chrono::microseconds(400);return value;
    }
};
struct EditorModule {};
struct EditorNodeGraphUI {
    struct PreviewGraphCacheEntry {
        std::string revisionToken,graph,error;
        std::vector<int> layers;
        bool loadAttempted=false;
    };
    std::unordered_map<std::string,PreviewGraphCacheEntry> m_PresetPreviewGraphCache;
'''
# Keep the real warmup defaults/field types rather than duplicating them here.
for line in header.splitlines():
    if ' m_PresetPreviewWarmup' in line:
        ui += line + '\n'
ui += r'''
    bool EnsurePresetPreviewGraphLoaded(const PresetEntry&);
    void WarmPresetPreviewCache(EditorModule*,double);
};
bool BuildPreviewGraphFromPayload(const StackBinaryFormat::json& payload,std::string& graph,std::vector<int>&,std::string& error){
    ++graphBuilds;
    if(payload.value("invalid",false)){error="Invalid graph";return false;}
    graph=payload.dump();return true;
}
'''
ui += method(drawer, 'std::string BuildPresetRevisionToken(')
token = method(drawer, 'std::string BuildPresetPreviewRevisionToken(')
ui += token.replace('BuildPresetPreviewRevisionToken(', 'UncountedPresetPreviewRevisionToken(', 1)
ui += 'std::string BuildPresetPreviewRevisionToken(const PresetEntry& preset){++tokenBuilds;return UncountedPresetPreviewRevisionToken(preset);}\n'
ui += method(drawer, 'bool EditorNodeGraphUI::EnsurePresetPreviewGraphLoaded(')
ui += method(drawer, 'void EditorNodeGraphUI::WarmPresetPreviewCache(').replace(
    'using Clock = std::chrono::steady_clock;', 'using Clock = TestClock;')

tests = r'''
auto Payload(int value){return StackBinaryFormat::json{{"value",value},{"nodeGraph",{{"nodes",StackBinaryFormat::json::array()}}}};}
std::shared_ptr<PresetEntry> Named(const std::string& name){
    for(const auto& entry:PresetManager::Get().GetUserPresets())if(entry->displayName==name)return entry;
    throw std::runtime_error("Missing preset "+name);
}
void Finish(EditorNodeGraphUI& ui,EditorModule& editor){
    for(std::size_t i=0;i<=PresetManager::Get().GetUserPresets().size();++i)ui.WarmPresetPreviewCache(&editor,.35);
}
void Idle(EditorNodeGraphUI& ui,EditorModule& editor){
    const auto tokens=tokenBuilds,graphs=graphBuilds;
    ui.WarmPresetPreviewCache(&editor,.35);ui.WarmPresetPreviewCache(&editor,.35);
    Require(tokens==tokenBuilds&&graphs==graphBuilds,"completed pass must do no per-entry work");
}
void Tests(){
    auto& manager=PresetManager::Get();auto& tasks=Async::TaskSystem::Get();
    EditorModule editor;EditorNodeGraphUI ui;
    Finish(ui,editor);Idle(ui,editor);
    for(int i=0;i<3;++i)Require(manager.SaveUserPreset("Preset "+std::to_string(i),Payload(i),{},{},1),"save");
    tasks.Drain();
    auto tokens=tokenBuilds,graphs=graphBuilds;
    ui.WarmPresetPreviewCache(&editor,.35);
    Require(tokenBuilds==tokens+1&&graphBuilds==graphs+1,"budget yields after one entry");
    ui.WarmPresetPreviewCache(&editor,.35);
    Require(tokenBuilds==tokens+2&&graphBuilds==graphs+2,"budget resumes at next entry");
    Finish(ui,editor);Idle(ui,editor);
    const auto stable=manager.GetPreviewDataRevision();manager.RefreshPresets();
    Require(manager.GetPreviewDataRevision()==stable,"unchanged nonempty catalog does not invalidate");Idle(ui,editor);

    auto old=Named("Preset 0");const auto oldToken=BuildPresetPreviewRevisionToken(*old);
    auto unchanged=Named("Preset 1");const auto actionToken=BuildPresetRevisionToken(*unchanged);
    Require(manager.OverwriteUserPreset(*old,Payload(99),{},{},1),"overwrite");
    auto overwritten=Named("Preset 0");
    Require(old->id==overwritten->id&&old->timestamp==overwritten->timestamp&&old->nodeCount==overwritten->nodeCount,
        "fixture actually overwrites in same timestamp with same node count");
    Require(BuildPresetPreviewRevisionToken(*overwritten)!=oldToken,"runtime incarnation rejects stale graph identity");
    Require(BuildPresetRevisionToken(*Named("Preset 1"))==actionToken,"unrelated publication preserves pending delete action identity");
    Finish(ui,editor);Idle(ui,editor); // Not-ready payloads may finish this pass.
    const auto pending=manager.GetPreviewDataRevision();tasks.Drain();
    Require(manager.GetPreviewDataRevision()>pending,"accepted payload success wakes warmup");
    Require(ui.EnsurePresetPreviewGraphLoaded(*overwritten),"visible preview can load before warmup");
    Require(ui.m_PresetPreviewGraphCache[old->id].graph==Payload(99).dump(),"overwrite graph replaces cached graph");
    Finish(ui,editor);Idle(ui,editor);

    old=Named("Preset 1");
    Require(manager.RenameUserPreset(*old,"Renamed"),"rename");tasks.Drain();
    Require(Named("Renamed")->previewContentRevision!=old->previewContentRevision,"rename republishes incarnation");
    Finish(ui,editor);Idle(ui,editor);
    Require(manager.DeleteUserPreset(*Named("Preset 2")),"delete");tasks.Drain();Finish(ui,editor);Idle(ui,editor);
    Require(manager.GetUserPresets().size()==2,"delete publishes shorter list");

    // An old list survives through this held entry and queued callbacks.
    Require(manager.SaveUserPreset("Late",Payload(6),{},{},1),"add delayed entry");
    auto late=Named("Late");tasks.Workers();
    Require(manager.DeleteUserPreset(*late),"delete before delayed completion");
    const auto current=manager.GetPreviewDataRevision();tasks.Main();
    Require(manager.GetPreviewDataRevision()==current&&late->graphPayloadLoaded&&!late->graphPayloadLoadRequested,
        "late callback completes retained entry without waking current views");
    tasks.Drain();Finish(ui,editor);Idle(ui,editor);

    Require(manager.SaveUserPreset("Read failure",Payload(7),{},{},1),"add read failure");
    Finish(ui,editor);Idle(ui,editor);const auto beforeFailure=manager.GetPreviewDataRevision();
    StackBinaryFormat::failPayloadRead=true;tasks.Drain();StackBinaryFormat::failPayloadRead=false;
    Require(manager.GetPreviewDataRevision()>beforeFailure,"accepted payload failure wakes warmup");
    auto failed=Named("Read failure");Finish(ui,editor);Idle(ui,editor);
    Require(!failed->graphPayloadLoadError.empty()&&!ui.m_PresetPreviewGraphCache[failed->id].error.empty(),
        "failure becomes terminal cached preview error");
    Require(manager.OverwriteUserPreset(*failed,Payload(8),{},{},1),"retry through supported overwrite refresh");
    tasks.Drain();Finish(ui,editor);Idle(ui,editor);
    Require(ui.m_PresetPreviewGraphCache[failed->id].graph==Payload(8).dump(),"retry replaces old error");

    tasks.rejectNext=true;
    Require(manager.SaveUserPreset("Submit failure",Payload(9),{},{},1),"add submit failure");
    const auto rejected=std::find_if(manager.GetUserPresets().begin(),manager.GetUserPresets().end(),
        [](const auto& p){return !p->graphPayloadLoadError.empty();});
    Require(rejected!=manager.GetUserPresets().end(),"submit failure retained");
    const auto rejectedId=(*rejected)->id;tasks.Drain();Finish(ui,editor);Idle(ui,editor);
    Require(!ui.m_PresetPreviewGraphCache[rejectedId].error.empty(),"submit failure reaches terminal preview");

    auto external=Named("Renamed");StackBinaryFormat::NodePresetDocument document;
    const auto path=testRoot/std::filesystem::u8path(external->fileName);
    Require(StackBinaryFormat::ReadNodePresetFile(path,document,{}),"external read");document.graphPayload=Payload(31);
    Require(StackBinaryFormat::WriteNodePresetFile(path,document),"external update");manager.RefreshPresets();tasks.Drain();
    Finish(ui,editor);Idle(ui,editor);
    Require(ui.m_PresetPreviewGraphCache[external->id].graph==Payload(31).dump(),"external refresh replaces preview");

    EditorNodeGraphUI newUi;tokens=tokenBuilds;Finish(newUi,editor);
    Require(tokenBuilds==tokens+int(manager.GetUserPresets().size()),"new UI warms its own cache once");Idle(newUi,editor);
    EditorModule replacement;Idle(ui,replacement); // Presets do not depend on the editor document.

    auto invalid=Payload(88);invalid["invalid"]=true;
    Require(manager.SaveUserPreset("Invalid graph",invalid,{},{},1),"add malformed graph");tasks.Drain();Finish(ui,editor);Idle(ui,editor);
    Require(!ui.m_PresetPreviewGraphCache[Named("Invalid graph")->id].error.empty(),"graph decode error is terminal");
}
int main(){
    testRoot=std::filesystem::temp_directory_path()/std::filesystem::path("stack-preset-warmup-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(testRoot);
    try{Tests();std::filesystem::remove_all(testRoot);std::cout<<"Preset preview warmup checks passed\n";return 0;}
    catch(const std::exception& e){std::filesystem::remove_all(testRoot);std::cerr<<e.what()<<'\n';return 1;}
}
'''
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(pre + methods + ui + tests)
