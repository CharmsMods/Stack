#include "ThirdParty/json.hpp"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <vector>
using nlohmann::json;
static bool counting=false;
static std::size_t allocations=0;
void* operator new(std::size_t n) { if(counting)++allocations;if(void* p=std::malloc(n))return p;throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,std::size_t) noexcept { std::free(p); }
// ACTUAL_CELL_TYPE
// Only the parse's output boundary is doubled; cell fields and parsing are production.
struct State { bool refinedMapActive=false;int refinedMapGridWidth=0,refinedMapGridHeight=0;std::vector<DevelopSubjectViewportMapCell> refinedMapCells; };
constexpr const char* kDevelopSubjectRefinedMapVersion="SubjectRefinedMapV1";
State Parse(const json& tone) { State outState;
// ACTUAL_PARSE
return outState; }
State Original(const json& tone) { State outState;
// ORIGINAL_PARSE
return outState; }
json Snapshot(const State& s) {
    json cells=json::array();for(const auto& c:s.refinedMapCells)cells.push_back({c.importance,c.reveal,c.protect,c.preserveMood,c.lowPriority,c.confidence,c.boundaryHint});
    return {s.refinedMapActive,s.refinedMapGridWidth,s.refinedMapGridHeight,cells};
}
json Outcome(const json& j,bool original) {
    try{return Snapshot(original?Original(j):Parse(j));}
    catch(const json::exception& e){return {{"error",e.id},{"message",e.what()}};}
}
void Check(bool ok,const char* message){if(!ok){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
int main(){
    json cells=json::array();for(int y=0;y<5;++y)for(int x=0;x<5;++x)cells.push_back({{"x",x},{"y",y},{"importance",.5},{"confidence",.7},{"readability",.4},{"protection",.2},{"preserveMood",.3},{"lowPriority",.1},{"boundaryHint",.8}});
    json solved={{"version",kDevelopSubjectRefinedMapVersion},{"active",true},{"gridWidth",5},{"gridHeight",5},{"cells",cells}};
    const auto wrap=[](json map){return json{{"autoSubjectSceneRefinedMap",std::move(map)}};};
    std::vector<json> cases={json::object(),nullptr,17,"bad",json::array(),wrap(solved)};
    for(const json& bad:std::vector<json>{nullptr,17,"bad",json::array(),json::object()})cases.push_back(wrap(bad));
    for(const json& value:std::vector<json>{nullptr,17,"bad",json::object(),json::array(),json::array({nullptr,3,json::object(),cells[0]})}){auto map=solved;map["cells"]=value;cases.push_back(wrap(map));}
    for(const char* key:{"cells","version","active","gridWidth","gridHeight"}){auto map=solved;map.erase(key);cases.push_back(wrap(map));map=solved;map[key]=nullptr;cases.push_back(wrap(map));}
    auto wrong=solved;wrong["version"]="old";cases.push_back(wrap(wrong));wrong=solved;wrong["gridWidth"]=0;cases.push_back(wrap(wrong));
    wrong=solved;wrong["cells"][0]["importance"]="bad";cases.push_back(wrap(wrong));
    for(const auto& input:cases){const auto before=input.dump();Check(Outcome(input,false)==Outcome(input,true),"normal/missing/malformed output and exact exception parity");Check(input.dump()==before,"borrowed JSON remains unchanged");}
    auto input=wrap(solved);auto result=Parse(input);Check(result.refinedMapActive&&result.refinedMapCells.size()==25,"all shipped grid cells parsed");
    const auto& c=result.refinedMapCells.back();Check(c.importance==.5f&&c.reveal==.4f&&c.protect==.2f&&c.preserveMood==.3f&&c.lowPriority==.1f&&c.confidence==.7f&&c.boundaryHint==.8f,"all cell metadata retained");
    allocations=0;counting=true;result=Parse(input);counting=false;const auto current=allocations;
    allocations=0;counting=true;result=Original(input);counting=false;const auto old=allocations;
    std::printf("solved 5x5 parse allocations: current=%zu original=%zu\n",current,old);
    Check(current<10 && old>500,"no deep copies of solved map/cells");
    input["autoSubjectSceneRefinedMap"]["cells"][0]["importance"]=.9;Check(Parse(input).refinedMapCells[0].importance==.9f,"next call observes edited data without cache");
    Check(Parse(json::object()).refinedMapCells.empty(),"missing map after populated call uses empty fallback");
    std::puts("Subject map reference parse PASS");
}
