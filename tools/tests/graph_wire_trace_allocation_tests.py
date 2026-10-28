#!/usr/bin/env python3
"""Check production expansion-wire preparation, not full-window frame timing.

Extracts the per-call trim-buffer declaration and per-link preparation unchanged.
Uses production adaptive sampling and real ImGui/CurveSample types. No GPU or
EditorModule stand-ins are needed for this allocation/geometry boundary.
"""
import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path)
    parser.add_argument('--imgui-dir', type=Path)
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    ui = root / 'src/Editor/NodeGraph/UI'
    source = (ui / 'EditorNodeGraphUILinksAndGroups.cpp').read_text()
    render = source[source.index('void EditorNodeGraphUI::RenderLinks('):]
    loop = render.index('    for (const EditorNodeGraph::Link& link : graph.GetLinks()) {')
    declaration = 'std::vector<CurveSample> tracedSamples;'
    before = declaration if declaration in render[:loop] else ''
    start = render.index('        if(trace<1)tracedSamples=wirePath.samples;')
    block = render[start:render.index('        const StackAppearance::GraphConnectionTextLayout textLayout', start)]
    if declaration in render[loop:start]:
        block = declaration + '\n' + block
    sampling = (ui / 'ContinuousLinkStroke.cpp').read_text()
    sampling = sampling[sampling.index('namespace {'):sampling.index('void DrawContinuousLinkStroke(')]
    generated = r'''
#include "Editor/NodeGraph/UI/ContinuousLinkStroke.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
static bool counting=false;
static std::size_t allocations=0;
void* operator new(std::size_t n) {
    if(counting)++allocations;
    if(auto* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
namespace Stack::Editor::NodeGraphUIVisuals {
''' + sampling + r'''
}
using namespace Stack::Editor::NodeGraphUIVisuals;
struct Wire {std::vector<CurveSample> samples;};
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
// Independent clipping oracle: retain all vertices before the cutoff and
// interpolate the terminal point, without changing the authored source curve.
std::vector<CurveSample> Expected(const Wire& wire,float trace) {
    if(trace>=1)return wire.samples;
    std::vector<CurveSample> result;
    for(std::size_t i=0;i<wire.samples.size();++i) {
        const auto& current=wire.samples[i];
        if(current.t<trace || i==0){result.push_back(current);continue;}
        const auto& prior=wire.samples[i-1];
        const float amount=(trace-prior.t)/(current.t-prior.t);
        result.push_back({{prior.point.x+(current.point.x-prior.point.x)*amount,
            prior.point.y+(current.point.y-prior.point.y)*amount},trace});
        break;
    }
    return result;
}
void Prepare(const std::vector<Wire>& wires,const std::vector<float>& traces,
    const std::vector<std::vector<CurveSample>>& expected) {
''' + before + r'''
    for(std::size_t i=0;i<wires.size();++i) {
        const auto& wirePath=wires[i];
        const float trace=traces[i];
''' + block + r'''
        Check(strokeSamples.size()==expected[i].size(),"Trim sample count changed");
        for(std::size_t j=0;j<strokeSamples.size();++j) {
            const auto& a=strokeSamples[j];const auto& b=expected[i][j];
            Check(a.t==b.t && a.point.x==b.point.x && a.point.y==b.point.y,
                "Trimmed geometry differs from reference");
        }
    }
}
int main() {
    std::vector<Wire> wires(64);
    for(std::size_t i=0;i<wires.size();++i) {
        const float length=1024.f-float(i%8)*64.f;
        wires[i].samples=SampleLinkCurve({0,0},{length*.8f,0},{length*.2f,180},{length,180});
    }
    for(int increasing=0;increasing<2;++increasing) {
    if(increasing)std::reverse(wires.begin(),wires.end());
    const auto original=wires;
    std::vector<float> traces(wires.size());
    std::vector<std::vector<CurveSample>> expected(wires.size());
    for(float cutoff:{.0001f,.125f,.5f,.9999f,1.f}) {
        for(int mixed=0;mixed<2;++mixed) {
            for(std::size_t i=0;i<wires.size();++i) {
                traces[i]=mixed && i%2 ? 1.f : cutoff;
                expected[i]=Expected(wires[i],traces[i]);
            }
            std::size_t growthBound=0,largest=0;
            for(std::size_t i=0;i<wires.size();++i) {
                if(traces[i]<1 && wires[i].samples.size()>largest) {
                    largest=wires[i].samples.size();++growthBound;
                }
            }
            allocations=0;counting=true;
            Prepare(wires,traces,expected);
            counting=false;
            Check(allocations<=growthBound && (growthBound==0 || allocations>0),
                "Animated wire allocation exceeded scratch capacity growth");
            for(std::size_t i=0;i<wires.size();++i) {
                Check(wires[i].samples.size()==original[i].samples.size(),"Source samples resized");
                for(std::size_t j=0;j<wires[i].samples.size();++j) {
                    const auto& a=wires[i].samples[j];const auto& b=original[i].samples[j];
                    Check(a.t==b.t && a.point.x==b.point.x && a.point.y==b.point.y,"Source geometry mutated");
                }
            }
        }
    }
    }
    std::cout<<"Expansion wire allocation and exact geometry checks passed.\n";
}
'''
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(generated)
        return
    if not args.imgui_dir:
        parser.error('--imgui-dir is required when compiling directly')
    with tempfile.TemporaryDirectory(prefix='stack-wire-trace-') as directory:
        cpp = Path(directory) / 'test.cpp'
        exe = Path(directory) / 'test'
        cpp.write_text(generated)
        subprocess.run([args.cxx, '-std=c++17', '-O2', '-I'+str(root/'src'),
                        '-I'+str(args.imgui_dir), str(cpp), '-o', str(exe)], check=True)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
