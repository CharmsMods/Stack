#include "Utils/AtomicExportFile.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

namespace {
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Write(const std::filesystem::path& p, const std::string& data) { std::ofstream out(p,std::ios::binary);out<<data; Require(out.good(),"fixture write"); }
std::string Read(const std::filesystem::path& p) { std::ifstream in(p,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}}; }
struct Fixtures {
 std::filesystem::path root;
 Fixtures() {root=std::filesystem::temp_directory_path()/("stack-export-tests-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())); Require(std::filesystem::create_directory(root),"fixture reserve");}
 ~Fixtures(){std::error_code ignored;std::filesystem::remove_all(root,ignored);}
};
}
int main() try {
 using namespace Stack::FileSave;
 Fixtures f;std::string error; ExportFileTarget target;
 const std::vector<unsigned char> bytes{'P','N','G'};
 auto original=f.root/"original.DNG";Write(original,"CAMERA-ORIGINAL");
 Require(!PrepareExportFileTarget(original,{}, {".png"}, target,error),"RAW extension accepted");
 auto alias=f.root/"alias.png";
 std::error_code ec;std::filesystem::create_hard_link(original,alias,ec);
 if (!ec) {
  Require(!PrepareExportFileTarget(alias,{original},{".png"},target,error),"known original hardlink accepted");
  Require(PrepareExportFileTarget(alias,{}, {".png"},target,error),"ordinary linked output preparation");
  Require(WriteExportFileAtomically(target,bytes,error),"atomic alias export failed");
  Require(Read(original)=="CAMERA-ORIGINAL","atomic alias replacement changed original");
 }
 auto symlink=f.root/"symlink.png";ec.clear();std::filesystem::create_symlink(original,symlink,ec);
 if (!ec) Require(!PrepareExportFileTarget(symlink,{}, {".png"},target,error),"symlink export accepted");
 else std::cout<<"SKIP symlink creation unavailable: "<<ec.message()<<'\n';
 auto png=f.root/"output.PNG";
 Require(PrepareExportFileTarget(png,{}, {".png"},target,error),"uppercase PNG rejected");
 Require(WriteExportFileAtomically(target,bytes,error),"new export failed");
 Require(Read(png)=="PNG","new export bytes differ");
 Require(PrepareExportFileTarget(png,{}, {".png"},target,error),"replacement preparation failed");
 Require(!WriteExportFileAtomically(target,{},error),"empty encoding accepted");
 Require(Read(png)=="PNG","failed export destroyed existing output");
 Require(!WriteExportFileAtomically(target,bytes,error,[]{return true;}),"canceled export published");
 Require(Read(png)=="PNG","canceled export destroyed existing output");
 Write(png,"A newer file");
 Require(!WriteExportFileAtomically(target,bytes,error),"changed destination replaced");
 Require(Read(png)=="A newer file","stale target changed");
 Require(PrepareExportFileTarget(png,{}, {".png"},target,error),"identity preparation failed");
 auto replacement=f.root/"replacement";Write(replacement,"A newer file");
 std::filesystem::last_write_time(replacement,target.approval.modified);
 std::filesystem::remove(png);std::filesystem::rename(replacement,png);
 Require(!WriteExportFileAtomically(target,bytes,error),"same-size same-time replacement accepted");
 Require(Read(png)=="A newer file","identity replacement overwritten");
 auto pending=f.root/"pending.png";
 Require(PrepareExportFileTarget(pending,{}, {".png"},target,error),"pending preparation failed");
 Write(pending,"CREATED-AFTER-REQUEST");
 Require(!WriteExportFileAtomically(target,bytes,error),"new destination was clobbered");
 Require(Read(pending)=="CREATED-AFTER-REQUEST","new destination changed");
 auto protectedAlias=f.root/"late-link.png";
 Require(PrepareExportFileTarget(protectedAlias,{}, {".png"},target,error),"late-link preparation failed");
 ec.clear();std::filesystem::create_symlink(original,protectedAlias,ec);
 if(!ec) {
  Require(!WriteExportFileAtomically(target,bytes,error),"late symbolic link accepted");
  Require(Read(original)=="CAMERA-ORIGINAL","late symbolic link changed original");
 }
 auto oldStage=f.root/"output.PNG.stack-exporting";Write(oldStage,"OTHER-OPERATION");
 Require(PrepareExportFileTarget(png,{}, {".png"},target,error),"existing output preparation");
 Require(WriteExportFileAtomically(target,bytes,error),"existing output replacement");
 Require(Read(oldStage)=="OTHER-OPERATION","unowned legacy staging deleted");
 auto parentA=f.root/"parent-a",parentB=f.root/"parent-b",parentLink=f.root/"parent-link";
 std::filesystem::create_directory(parentA);std::filesystem::create_directory(parentB);
 ec.clear();std::filesystem::create_directory_symlink(parentA,parentLink,ec);
 if(!ec) {
  Require(PrepareExportFileTarget(parentLink/"resolved.png",{}, {".png"},target,error),"parent alias preparation");
  std::filesystem::remove(parentLink);std::filesystem::create_directory_symlink(parentB,parentLink);
  Require(WriteExportFileAtomically(target,bytes,error),"resolved parent export failed");
  Require(Read(parentA/"resolved.png")=="PNG" && !std::filesystem::exists(parentB/"resolved.png"),"retargeted parent changed export destination");
 }
 auto race=f.root/"race.png";ExportFileTarget a,b;
 Require(PrepareExportFileTarget(race,{}, {".png"},a,error),"race preparation A");
 Require(PrepareExportFileTarget(race,{}, {".png"},b,error),"race preparation B");
 bool okA=false,okB=false;std::atomic<int> ready{0};
 auto worker=[&](const ExportFileTarget& t,bool& result,unsigned char value){std::string why;ready.fetch_add(1);while(ready.load()!=2)std::this_thread::yield();result=WriteExportFileAtomically(t,std::vector<unsigned char>(1024*1024,value),why);};
 std::thread t1(worker,std::cref(a),std::ref(okA),'A'),t2(worker,std::cref(b),std::ref(okB),'B');t1.join();t2.join();
 Require(okA!=okB,"concurrent exports did not preserve first publication");
 auto output=Read(race);Require(output==std::string(1024*1024,okA?'A':'B'),"mixed or incorrect concurrent bytes");
 for(const auto& entry:std::filesystem::directory_iterator(f.root))
  Require(entry.path().filename().u8string().find(".stack-export-")!=0,"staging directory leaked");
 Require(Read(original)=="CAMERA-ORIGINAL","original source changed");
 std::cout<<"Atomic export source, alias, stale-target, failure and concurrency checks passed.\n";
 return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
