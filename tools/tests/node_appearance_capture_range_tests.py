#!/usr/bin/env python3
"""CPU check of the production appearance capture's triangle selection loop.

GPU texture copying is unchanged and excluded. Count actual index accesses to
verify the new range avoids scanning earlier canvas geometry.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/Editor/UI/NodeOpenAppearance.cpp').read_text()
loop = source[source.index('    for (const auto& command:list.CmdBuffer)'):source.index('        const auto id=command.GetTexID();')]
code = r'''
#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
struct Mesh { int clip; std::vector<int> vertices; };
struct Command { bool UserCallback=false; int ClipRect=0; unsigned IdxOffset=0,ElemCount=0,VtxOffset=0; };
struct Indices {
 std::vector<unsigned> data; mutable unsigned reads=0;
 unsigned operator[](unsigned i)const{++reads;return data.at(i);}
};
struct Vertices {std::vector<int> data;int Size=0;int operator[](unsigned i)const{return data.at(i);} };
struct List {std::vector<Command> CmdBuffer;Indices IdxBuffer;Vertices VtxBuffer;};
std::vector<Mesh> Capture(const List& list,int firstVertex,unsigned firstIndex){
 std::vector<Mesh> meshes;
''' + loop + r'''
        meshes.push_back(std::move(mesh));
    }
    return meshes;
}
void Check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
void Same(const std::vector<Mesh>& a,const std::vector<Mesh>& b){
 Check(a.size()==b.size(),"mesh count changed");
 for(unsigned i=0;i<a.size();++i)Check(a[i].clip==b[i].clip && a[i].vertices==b[i].vertices,"node geometry/decorations changed");
}
int main(){
 List list;
 constexpr unsigned earlier=60000;
 list.VtxBuffer.Size=24;for(int i=0;i<24;++i)list.VtxBuffer.data.push_back(i);
 list.IdxBuffer.data.resize(earlier,0);
 // Earlier commands and a command shared by earlier geometry and this node.
 list.CmdBuffer.push_back({false,1,0,earlier-6,0});
 list.CmdBuffer.push_back({false,2,earlier-6,12,0});
 for(unsigned i:{12,13,14,12,14,15})list.IdxBuffer.data.push_back(i);
 // Callback geometry must not enter the capture.
 list.CmdBuffer.push_back({true,3,earlier+6,3,0});
 for(unsigned i:{12,13,14})list.IdxBuffer.data.push_back(i);
 // A separate decoration command uses a nonzero vertex offset.
 list.CmdBuffer.push_back({false,4,earlier+9,6,16});
 for(unsigned i:{0,1,2,0,2,3})list.IdxBuffer.data.push_back(i);
 // A mixed triangle crossing the vertex ownership boundary must be rejected.
 list.CmdBuffer.push_back({false,5,earlier+15,3,0});
 for(unsigned i:{0,12,13})list.IdxBuffer.data.push_back(i);
 const auto old=Capture(list,12,0);const auto oldReads=list.IdxBuffer.reads;
 list.IdxBuffer.reads=0;const auto bounded=Capture(list,12,earlier);
 Same(old,bounded);Check(bounded.size()==2,"decoration/callback/ownership selection failed");
 Check(list.IdxBuffer.reads==15,"capture scanned unrelated canvas indices");
 Check(oldReads==earlier+15,"baseline fixture is not meaningful");
 list.IdxBuffer.reads=0;Check(Capture(list,12,earlier+18).empty(),"empty tail captured geometry");
 Check(list.IdxBuffer.reads==0,"empty tail scanned geometry");
 std::cout<<"PASS appearance range: 60015 -> 15 index reads; identical node/decorations, callbacks, offsets and vertex ownership.\n";
}
'''
with tempfile.TemporaryDirectory(prefix='stack-capture-range-') as folder:
    cpp=Path(folder)/'test.cpp';exe=Path(folder)/'test';cpp.write_text(code)
    subprocess.run(['g++','-std=c++17','-O2',str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
