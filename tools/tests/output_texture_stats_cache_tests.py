#!/usr/bin/env python3
"""Compile real renderer method bodies with a fake readback/GL boundary.

No OpenGL context is needed. The getter, reducer, adoption, clear, transfer and
upload code below are extracted unchanged from production sources. Additional
source-path checks guard invalidation in execution/resource paths whose GPU
implementation is deliberately not run by this CPU regression test.
"""
import argparse
import pathlib
import re
import subprocess
import tempfile


def functions(source, name):
    pattern = r"(?:void|bool|unsigned int|RenderTextureStats) RenderPipeline::" + name + r"\([^{}]*\)(?: noexcept)?\s*\{"
    result = []
    for match in re.finditer(pattern, source):
        end = match.end()
        depth = 1
        while depth:
            depth += (source[end] == "{") - (source[end] == "}")
            end += 1
        result.append(source[match.start():end])
    return result


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--cxx", default="g++", help="Compiler for optional standalone cloud check")
    parser.add_argument("--output", type=pathlib.Path, help="Generate only; let CMake compile the harness normally")
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    renderer = root / "src/Renderer"
    resources = (renderer / "Internal/RenderPipelineResources.cpp").read_text()
    execution = (renderer / "RenderPipeline.cpp").read_text()
    readback = (renderer / "Internal/RenderPipelineReadback.cpp").read_text()
    header = (renderer / "RenderPipeline.h").read_text()
    for source, names in ((execution, ("Execute", "ExecuteMasked", "ExecuteGraph", "HandleGraphExecutionFailure")),
                          (resources, ("Shutdown", "Clear", "ClearOutput", "Resize", "InvalidateGraphCaches",
                                       "TakeExternalOutputTexture", "AdoptExternalOutputTexture"))):
        for name in names:
            bodies = functions(source, name)
            require(len(bodies) == (2 if name == "ExecuteGraph" else 1), "missing mutation method: " + name)
            for body in bodies:
                require(body.split("{", 1)[1].lstrip().startswith("InvalidateOutputTextureStats();"),
                        "output mutation lacks entry invalidation: " + name)
    for name in ("LoadSourceImage", "LoadSourceFromPixels", "LoadSourceFromSharedPixels"):
        body = functions(resources, name)[0]
        require(body.index("InvalidateGraphCaches(") < body.index("glDeleteTextures("),
                "source replacement must invalidate before recycling names: " + name)
    upload = functions(resources, "UploadOutputFromPixels")[0]
    require(upload.index("if (!replacement)") < upload.index("InvalidateOutputTextureStats();") <
            upload.index("m_ExternalOutputTexture = replacement.Release()"),
            "upload must invalidate only after a successful replacement exists")
    # Every assignment in the public pipeline/resource units must remain in an
    # audited invalidating method. Graph implementation writes are covered by
    # both invalidating ExecuteGraph entry points checked above.
    for source, names in ((execution, ("Execute", "ExecuteMasked", "HandleGraphExecutionFailure")),
                          (resources, ("Shutdown", "Clear", "ClearOutput", "TakeExternalOutputTexture",
                                       "UploadOutputFromPixels", "AdoptExternalOutputTexture"))):
        covered = "\n".join(body for name in names for body in functions(source, name))
        pattern = r"\bm_OutputTexture\s*=(?!=)"
        require(len(re.findall(pattern, covered)) == len(re.findall(pattern, source)),
                "new output assignment needs invalidation coverage")
    stats = re.search(r"struct RenderTextureStats \{.*?\n\};", header, re.S).group()
    source = r'''
#include "Renderer/OutputTextureStatsCache.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <new>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
''' + stats + r'''
int nextAllocationFailure=0,failReductionAllocation=0;
void* operator new(std::size_t size) {
    const int failure=nextAllocationFailure;nextAllocationFailure=0;
    if(failure==1)throw std::bad_alloc();
    if(failure==2)throw std::length_error("forced reduction allocation failure");
    if(void* memory=std::malloc(size?size:1))return memory;
    throw std::bad_alloc();
}
void operator delete(void* memory)noexcept{std::free(memory);}
void operator delete(void* memory,std::size_t)noexcept{std::free(memory);}
int reads=0;
bool failRead=false,transparent=false,failUpload=false;
float content=1.f;
std::vector<float> ReadTexturePixelsRgbaFloat(unsigned int,int,int,int& w,int& h,int,const char*) {
    ++reads;w=2;h=1;
    if(failRead)return {};
    std::vector<float> result{content,content,content,transparent?0.f:1.f,content,content,content,transparent?0.f:1.f};
    // Fail the next allocation, which is the actual reducer's lumas.reserve,
    // not this fake readback. Moving the returned vector does not allocate.
    nextAllocationFailure=failReductionAllocation;failReductionAllocation=0;
    return result;
}
void glDeleteTextures(int,const unsigned int*){}
namespace GLHelpers { unsigned int CreateTextureFromPixels(const unsigned char*,int,int,int){return failUpload?0:7;} }
namespace Stack::Renderer { struct ScopedGLTexture {
    unsigned int name;
    explicit ScopedGLTexture(unsigned int n):name(n){}
    explicit operator bool()const{return name!=0;}
    unsigned int Release(){auto n=name;name=0;return n;}
}; }
class RenderPipeline {
public:
    unsigned int m_OutputTexture=0,m_ExternalOutputTexture=0,m_GraphSourceTexture=0;
    int m_Width=0,m_Height=0,m_RawDevelopmentLocalSuggestionImage=0;
    Stack::Renderer::OutputTextureStatsCache<RenderTextureStats> m_OutputTextureStatsCache;
    void InvalidateOutputTextureStats(){m_OutputTextureStatsCache.Invalidate();}
    void ClearRawDevelopmentStageStatsReadbacks(){}
    RenderTextureStats GetOutputTextureStats();
    RenderTextureStats ReadTextureStats(unsigned int,int,int,const char*,bool=false,bool* =nullptr);
    void AdoptExternalOutputTexture(unsigned int,int,int);
    bool UploadOutputFromPixels(const unsigned char*,int,int,int);
    unsigned int TakeExternalOutputTexture(int&,int&,bool=false);
    void ClearOutput();
};
''' + "\n".join(functions(readback, name)[0] for name in ("ReadTextureStats", "GetOutputTextureStats")) + "\n" + \
        "\n".join(functions(resources, name)[0] for name in
                  ("AdoptExternalOutputTexture", "UploadOutputFromPixels", "TakeExternalOutputTexture", "ClearOutput")) + r'''
void Check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
int main()try{
    RenderPipeline a,b;
    Check(!a.GetOutputTextureStats().valid && reads==0,"empty output attempted readback");
    a.AdoptExternalOutputTexture(7,1024,1024);
    Check(a.GetOutputTextureStats().minRgb==1.f,"initial reduction failed");
    for(int i=0;i<100;++i)Check(a.GetOutputTextureStats().valid,"cached valid result lost");
    Check(reads==1,"unchanged getter repeated readback");
    content=2.f;a.AdoptExternalOutputTexture(7,1024,1024);
    Check(a.GetOutputTextureStats().maxRgb==2.f && reads==2,"same-name new content reused stale stats");
    content=3.f;b.AdoptExternalOutputTexture(7,1024,1024);
    Check(b.GetOutputTextureStats().maxRgb==3.f && reads==3,"cache crossed pipeline owners");
    Check(a.GetOutputTextureStats().maxRgb==2.f && reads==3,"other pipeline invalidated owner cache");
    a.m_Width=512;Check(a.GetOutputTextureStats().maxRgb==3.f && reads==4,"extent change missed cache key");
    content=4.f;a.InvalidateOutputTextureStats();
    Check(a.GetOutputTextureStats().maxRgb==4.f && reads==5,"content invalidation retained stale stats");
    failRead=true;a.AdoptExternalOutputTexture(7,1024,1024);
    Check(!a.GetOutputTextureStats().valid && reads==6,"failed read returned prior success");
    failRead=false;Check(a.GetOutputTextureStats().valid && reads==7,"failure was cached permanently");
    transparent=true;a.AdoptExternalOutputTexture(7,1024,1024);
    Check(!a.GetOutputTextureStats().valid && reads==8,"transparent reduction should be known-empty");
    Check(!a.GetOutputTextureStats().valid && reads==8,"known-empty result repeated readback");
    transparent=false;a.ClearOutput();
    Check(!a.GetOutputTextureStats().valid && reads==8,"clear returned cached prior output");
    unsigned char pixel=0;content=5.f;
    Check(a.UploadOutputFromPixels(&pixel,1,1,4),"upload failed");
    Check(a.GetOutputTextureStats().maxRgb==5.f && reads==9,"upload reused old name stats");
    failUpload=true;Check(!a.UploadOutputFromPixels(&pixel,1,1,4),"failed upload accepted");
    Check(a.GetOutputTextureStats().maxRgb==5.f && reads==9,"failed upload discarded unchanged cache");
    int w=0,h=0;Check(a.TakeExternalOutputTexture(w,h)==7,"texture transfer failed");
    Check(!a.GetOutputTextureStats().valid && reads==9,"transfer retained cache for missing output");
    a.AdoptExternalOutputTexture(7,1,1);content=6.f;
    Check(a.GetOutputTextureStats().maxRgb==6.f && reads==10,"transferred name reused stale result");
    for(int failure:{1,2}) {
        a.AdoptExternalOutputTexture(7,1,1);content=6.f+failure;
        failReductionAllocation=failure;
        const int before=reads;
        Check(!a.GetOutputTextureStats().valid && reads==before+1,"reduction allocation failure escaped or reused old stats");
        Check(nextAllocationFailure==0 && a.m_OutputTexture==7 && a.m_Width==1 && a.m_Height==1,
              "diagnostic allocation failure changed rendered output or missed reserve");
        Check(a.GetOutputTextureStats().maxRgb==content && reads==before+2,"reduction allocation failure prevented retry");
    }
    std::cout<<"Actual output stats getter/reducer/adopt/upload/take/clear and mutation wiring checks passed.\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(source)
        return
    with tempfile.TemporaryDirectory(prefix="stack-output-stats-") as directory:
        cpp = pathlib.Path(directory) / "test.cpp"
        exe = pathlib.Path(directory) / "test"
        cpp.write_text(source)
        if pathlib.Path(args.cxx).name.lower() in ("cl", "cl.exe", "clang-cl", "clang-cl.exe"):
            command = [args.cxx, "/nologo", "/std:c++17", "/EHsc", "/I" + str(root / "src"), str(cpp), "/Fe:" + str(exe) + ".exe"]
            exe = pathlib.Path(str(exe) + ".exe")
        else:
            command = [args.cxx, "-std=c++17", "-O2", "-I" + str(root / "src"), str(cpp), "-o", str(exe)]
        subprocess.run(command, check=True, cwd=directory)
        subprocess.run([str(exe)], check=True)


if __name__ == "__main__":
    main()
