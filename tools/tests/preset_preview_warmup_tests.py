#!/usr/bin/env python3
"""Bounded CPU checks of production preset refresh/publication/warmup bodies.

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
#include <atomic>
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
bool onWorker=false;
int catalogReads=0,payloadReads=0,textureDeletes=0;
struct RefreshClock {
    static std::chrono::steady_clock::time_point value;
    static auto now(){return value;}
    static void Advance(){value+=std::chrono::seconds(2);}
};
std::chrono::steady_clock::time_point RefreshClock::value{};
namespace AppPaths { std::filesystem::path GetPresetsDirectory(){return testRoot;} }
namespace AppVersion { constexpr const char* kVersionString="test-version"; }
void glDeleteTextures(int count,const unsigned*){Require(!onWorker,"texture retirement must remain on main thread");textureDeletes+=count;}
std::string BuildTimestampString(){return "same-second";}
std::string BuildPresetId(){static int next=0;return "preset-"+std::to_string(++next);}
namespace Async {
struct ActivityMetadata {std::string label;bool maintenance=false;};
class TaskSystem {
public:
    using Task=std::function<void()>;
    static TaskSystem& Get(){static TaskSystem instance;return instance;}
    std::deque<Task> workers,main;
    bool rejectNext=false;
    bool Submit(const char*,Task task){if(std::exchange(rejectNext,false))return false;workers.push_back(std::move(task));return true;}
    bool Submit(ActivityMetadata activity,Task task){Require(activity.maintenance,"refresh must use maintenance priority");return Submit(activity.label.c_str(),std::move(task));}
    bool PostToMain(Task task){main.push_back(std::move(task));return true;}
    void Workers(){onWorker=true;while(!workers.empty()){auto task=std::move(workers.front());workers.pop_front();task();}onWorker=false;}
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
    if(options.includeGraphPayload)++payloadReads;else ++catalogReads;
    if(failPayloadRead&&options.includeGraphPayload)return false;
    try {
    std::ifstream in(path);if(!in)return false;json value;in>>value;
    auto& m=document.metadata;m.id=value["id"];m.displayName=value["name"];m.timestamp=value["timestamp"];m.savedWithVersion=value["version"];
    m.nodeCount=value["nodes"];m.inputCount=value["inputs"];m.outputCount=value["outputs"];
    if(options.includeGraphPayload)document.graphPayload=value["graph"];
    return true;
    }catch(...){return false;}
}
}
'''
methods = method(manager, 'struct PresetCatalogScan {') + ';\n'
for signature in ['bool PresetScanCanceled(', 'bool BuildPresetSignature(', 'PresetCatalogScan ScanPresetCatalog(']:
    methods += method(manager, signature)
methods += method(manager, 'struct PresetManager::RefreshRequest {') + ';\n'
for signature in [
    'std::string TrimWhitespace(', 'std::string SanitizeFileStem(',
    'void ScrubPresetImageNodePayload(', 'bool SanitizePresetGraphPayload(',
    'PresetManager& PresetManager::Get()', 'PresetManager::PresetManager()',
    'PresetManager::~PresetManager()', 'void PresetManager::EnsureDirectories()',
    'void PresetManager::ReleaseTexture(', 'void PresetManager::CancelPresetRefresh()',
    'void PresetManager::RefreshPresets()', 'void PresetManager::RequestPresetRefresh()',
    'void PresetManager::ApplyPresetCatalog(', 'void PresetManager::RequestBackgroundGraphPayloadPreload(',
    'bool PresetManager::SaveUserPreset(', 'bool PresetManager::TryGetPreloadedPresetPayload(',
    'bool PresetManager::ReadUserPresetDocument(', 'bool PresetManager::WriteUserPresetDocument(',
    'bool PresetManager::RenameUserPreset(', 'bool PresetManager::DeleteUserPreset(',
    'bool PresetManager::OverwriteUserPreset(']:
    body = method(manager, signature)
    if signature in ('void PresetManager::RefreshPresets()', 'void PresetManager::RequestPresetRefresh()'):
        body = body.replace('std::chrono::steady_clock::now()', 'RefreshClock::now()')
    methods += body

# Check the real synchronization and lifetime boundary, not a fixture rewrite.
assert 'ready.load(std::memory_order_acquire)' in method(manager, 'void PresetManager::RequestPresetRefresh()')
assert 'ready.store(true, std::memory_order_release)' in method(manager, 'void PresetManager::RequestPresetRefresh()')
assert '[request, path, signature, hasSignature]' in method(manager, 'void PresetManager::RequestPresetRefresh()')
assert 'CancelPresetRefresh();' in method(manager, 'PresetManager::~PresetManager()')
assert 'RefreshPresets();' not in method(drawer, 'void EditorNodeGraphUI::RenderPresetsPanel(')

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
void RefreshTests(){
    auto& manager=PresetManager::Get();auto& tasks=Async::TaskSystem::Get();tasks.Drain();
    auto before=Named("Renamed");before->thumbnailTex=91;
    StackBinaryFormat::NodePresetDocument document;
    const auto path=testRoot/std::filesystem::u8path(before->fileName);
    Require(StackBinaryFormat::ReadNodePresetFile(path,document,{}),"read external source");
    document.metadata.displayName="External rename";document.graphPayload=Payload(52);
    Require(StackBinaryFormat::WriteNodePresetFile(path,document),"external mutation");
    const auto revision=manager.GetPreviewDataRevision();const auto reads=catalogReads;
    RefreshClock::Advance();manager.RequestPresetRefresh();
    Require(tasks.workers.size()==1&&catalogReads==reads,"frame only enqueues, never reads catalog documents");
    manager.RequestPresetRefresh();manager.RequestPresetRefresh();
    Require(tasks.workers.size()==1&&manager.GetPreviewDataRevision()==revision,"in-flight scan is coalesced without premature publication");
    tasks.Workers();
    Require(catalogReads>reads&&manager.GetPreviewDataRevision()==revision&&before->thumbnailTex==91,
        "worker reads candidate but cannot publish or release GL resources");
    RefreshClock::Advance(); // A long scan still publishes before scheduling another.
    manager.RequestPresetRefresh();
    Require(Named("External rename")->id==before->id&&manager.GetPreviewDataRevision()>revision,
        "ready scan publishes on main thread");
    Require(before->thumbnailTex==0&&textureDeletes==1,"old texture retires only with main publication");
    tasks.Drain();const auto count=tasks.workers.size();manager.RequestPresetRefresh();
    Require(tasks.workers.size()==count,"completed result is not followed immediately by another scan");
    RefreshClock::Advance();manager.RequestPresetRefresh();tasks.Workers();
    const auto unchanged=manager.GetPreviewDataRevision();manager.RequestPresetRefresh();
    Require(manager.GetPreviewDataRevision()==unchanged,"unchanged async signature does not rewarm catalog");

    // Finish a scan, then mutate through the app before its result is polled.
    Require(StackBinaryFormat::ReadNodePresetFile(path,document,{}),"read stale source");
    document.metadata.displayName="Stale external";
    Require(StackBinaryFormat::WriteNodePresetFile(path,document),"stale disk change");
    RefreshClock::Advance();manager.RequestPresetRefresh();tasks.Workers();
    auto current=Named("External rename");
    Require(manager.RenameUserPreset(*current,"Immediate app rename"),"app rename supersedes background result");
    Require(Named("Immediate app rename")->id==before->id,"explicit rename publishes immediately");
    const auto explicitRevision=manager.GetPreviewDataRevision();manager.RequestPresetRefresh();
    Require(manager.GetPreviewDataRevision()==explicitRevision,"stale completed result cannot overwrite explicit publication");
    tasks.Drain();

    // Cancel a queued scan before it starts: same cancellation used at teardown.
    RefreshClock::Advance();manager.RequestPresetRefresh();const auto queuedReads=catalogReads;
    manager.RefreshPresets();tasks.Workers();
    Require(catalogReads==queuedReads,"canceled queued worker does no catalog document work");
    tasks.Main();

    // Individual corrupt documents keep skip-unreadable semantics.
    std::ofstream(testRoot/"bad.stackpreset")<<"not a preset";
    Require(StackBinaryFormat::ReadNodePresetFile(path,document,{}),"read valid neighbor");
    document.metadata.displayName="Valid beside corrupt";
    Require(StackBinaryFormat::WriteNodePresetFile(path,document),"valid neighboring edit");
    RefreshClock::Advance();manager.RequestPresetRefresh();tasks.Workers();manager.RequestPresetRefresh();tasks.Drain();
    Require(Named("Valid beside corrupt")->id==before->id,"corrupt file does not block valid catalog changes");

    // Replace the directory with a file: identifiable directory failure retains
    // the exact old entries and signature, then a later successful scan recovers.
    const auto backup=testRoot.string()+"-backup";std::filesystem::rename(testRoot,backup);
    std::ofstream(testRoot)<<"not a directory";
    const auto retained=Named("Valid beside corrupt");const auto failedRevision=manager.GetPreviewDataRevision();
    RefreshClock::Advance();manager.RequestPresetRefresh();tasks.Workers();manager.RequestPresetRefresh();
    Require(manager.GetPreviewDataRevision()==failedRevision&&Named("Valid beside corrupt")==retained,
        "directory failure retains prior catalog and does not accept failure signature");
    std::filesystem::remove(testRoot);std::filesystem::rename(backup,testRoot);
    Require(StackBinaryFormat::ReadNodePresetFile(path,document,{}),"read recovered entry");
    document.metadata.displayName="Recovered";Require(StackBinaryFormat::WriteNodePresetFile(path,document),"recover disk update");
    RefreshClock::Advance();manager.RequestPresetRefresh();tasks.Workers();manager.RequestPresetRefresh();tasks.Drain();
    Require(Named("Recovered")->id==before->id,"next successful scan recovers after directory failure");

    tasks.rejectNext=true;RefreshClock::Advance();manager.RequestPresetRefresh();
    Require(tasks.workers.empty(),"submission failure is terminal");manager.RequestPresetRefresh();
    Require(tasks.workers.empty(),"submission failure is cadence-limited");
    RefreshClock::Advance();manager.RequestPresetRefresh();Require(tasks.workers.size()==1,"submission failure retries later");
    tasks.Workers();manager.RequestPresetRefresh();

    // No request calls while the panel is closed means no self-rescheduling.
    RefreshClock::Advance();RefreshClock::Advance();tasks.Drain();
    Require(tasks.workers.empty(),"closed panel creates no additional scans");
    std::filesystem::remove_all(testRoot);
    RefreshClock::Advance();manager.RequestPresetRefresh();
    Require(!std::filesystem::exists(testRoot),"missing-directory recovery does not create it from drawing");
    tasks.Workers();Require(std::filesystem::is_directory(testRoot),"worker recreates missing preset directory");
    manager.RequestPresetRefresh();Require(manager.GetUserPresets().empty(),"successful empty catalog publishes");
    const auto emptyRevision=manager.GetPreviewDataRevision();
    RefreshClock::Advance();manager.RequestPresetRefresh();tasks.Workers();manager.RequestPresetRefresh();
    Require(manager.GetPreviewDataRevision()==emptyRevision,"stable empty catalog is not republished");
}
int main(){
    testRoot=std::filesystem::temp_directory_path()/std::filesystem::path("stack-preset-warmup-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(testRoot);
    try{Tests();RefreshTests();std::filesystem::remove_all(testRoot);std::cout<<"Preset refresh and preview warmup checks passed\n";return 0;}
    catch(const std::exception& e){std::filesystem::remove_all(testRoot);std::cerr<<e.what()<<'\n';return 1;}
}
'''
args.output.parent.mkdir(parents=True, exist_ok=True)
args.output.write_text(pre + methods + ui + tests)
