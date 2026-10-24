#include "Library/Internal/LibraryProjectExport.h"
#include "Persistence/ProjectStore.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
namespace {
namespace fs = std::filesystem;
namespace Project = Stack::Project;
void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
void Put(const fs::path& path, const std::string& bytes) { std::ofstream f(path,std::ios::binary); f << bytes; f.close(); Require(f.good(), "write fixture"); }
std::string Read(const fs::path& path) { std::ifstream f(path,std::ios::binary); return {std::istreambuf_iterator<char>(f),{}}; }
struct Sandbox {
    fs::path path=fs::temp_directory_path()/("stack-project-export-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Sandbox(){Require(fs::create_directory(path),"create sandbox");}
    ~Sandbox(){std::error_code ec;fs::remove_all(path,ec);}
};
}
int main() {
    Sandbox box; std::string error;
    const auto source=box.path/"working";
    const auto original=box.path/"original.dng";
    const auto destination=box.path/"copy.stack";
    Put(original,"synthetic immutable source");
    Project::RawProjectSnapshot snapshot;
    snapshot.projectId=Project::GenerateStableUuid(); snapshot.projectName="Export fixture";
    auto opened=Project::CreateProjectStore(source,Project::ProjectStorageKind::DirectoryBundle,snapshot);
    Require(bool(opened),"create actual project store");
    auto tx=opened.store->BeginTransaction(opened.snapshot.persistedStorageRevision);
    Project::EmbeddedAssetRecord asset;
    Require(opened.store->StageAssetFile(tx,original,Project::MultiFrameInputFamily::Raw,nlohmann::json::object(),asset,&error),"stage actual asset");
    opened.snapshot.embeddedAssets.push_back(asset);
    const auto commit=opened.store->Commit(tx,opened.snapshot);
    Require(bool(commit),"commit actual project");
    const auto manifest=Project::WorkingProjectDocumentPath(source);
    const auto manifestBytes=Read(manifest);
    Require(Stack::Library::ExportPortableProject(source,destination,error),"export directory project");
    Require(Project::IsPortableV3Project(destination),"portable format");
    Require(Read(manifest)==manifestBytes && Read(original)=="synthetic immutable source","source unchanged");
    auto packed=Project::OpenProjectStore(destination);
    Require(bool(packed) && packed.snapshot.projectId==opened.snapshot.projectId,"reopen portable identity");
    packed.store.reset(); opened.store.reset();
    fs::rename(source,box.path/"source-moved"); fs::rename(original,box.path/"original-moved.dng");
    packed=Project::OpenProjectStore(destination);
    Require(bool(packed),"portable independent of source paths");
    auto stream=packed.store->OpenAssetStream(asset.assetId);
    Require(bool(stream),"portable owns asset");
    const std::string restored{std::istreambuf_iterator<char>(*stream.stream),{}};
    Require(restored=="synthetic immutable source","portable asset bytes");
    stream.stream.reset(); packed.store.reset();
    fs::rename(box.path/"source-moved",source); fs::rename(box.path/"original-moved.dng",original);
    Require(!Stack::Library::ExportPortableProject(source,manifest,error) && Read(manifest)==manifestBytes,"reject own manifest");
    Require(!Stack::Library::ExportPortableProject(destination,destination,error),"reject same portable source");
    Require(!Stack::Library::ExportPortableProject(source,original,error) && Read(original)=="synthetic immutable source","reject original");
    const auto originalAlias=box.path/"original-alias.stack";
    fs::create_hard_link(original,originalAlias);
    Require(!Stack::Library::ExportPortableProject(source,originalAlias,error) &&
        Read(original)=="synthetic immutable source","reject hardlinked original alias");
    std::error_code linkError;
    const auto originalLink=box.path/"original-link.stack";
    fs::create_symlink(original,originalLink,linkError);
    if (!linkError) Require(!Stack::Library::ExportPortableProject(source,originalLink,error) &&
        Read(original)=="synthetic immutable source","reject symlinked original alias");
    Put(destination,"previous export");
    Require(!Stack::Library::ExportPortableProject(source,destination,error,[]{return true;}) && Read(destination)=="previous export","early cancel preserves");
    int checks=0;
    Require(!Stack::Library::ExportPortableProject(source,destination,error,[&]{return ++checks==2;}) && Read(destination)=="previous export","late cancel preserves");
    checks=0;
    Require(!Stack::Library::ExportPortableProject(source,destination,error,[&]{if(++checks==2)Put(destination,"late replacement");return false;}) && Read(destination)=="late replacement","stale approval preserves arrival");
    Require(Stack::Library::ExportPortableProject(source,destination,error),"approved overwrite");
    auto broken=Project::OpenProjectStore(source);
    const auto managed=broken.store->ResolveAssetFile(asset.assetId);
    broken.store.reset();
    const auto previous=Read(destination);
    checks=0;
    Require(!Stack::Library::ExportPortableProject(source,destination,error,[&]{
        if (++checks==1) fs::rename(managed,managed.string()+".missing");
        return false;
    }) && Read(destination)==previous,"conversion failure retains existing export");
    const auto recovered=Project::OpenProjectStore(source);
    Require(bool(recovered) && recovered.recoveredPreviousManifest,"missing source fixture requires recovery");
    Require(!Stack::Library::ExportPortableProject(source,destination,error) &&
        error.find("recovery")!=std::string::npos && Read(destination)==previous,
        "recovered backup must not silently replace export");
    fs::remove(manifest.string()+".previous");
    Put(manifest,"invalid primary manifest");
    Require(!Stack::Library::ExportPortableProject(source,destination,error) && Read(destination)==previous,
        "unrecoverable project preserves export");
    for(const auto& entry:fs::directory_iterator(box.path))
        Require(entry.path().filename().string().find(".stack-project-export-")!=0 && entry.path().filename().string().find(".stack-export-")!=0,"owned staging cleaned");
    std::cout<<"Library project export tests passed\n";
}
