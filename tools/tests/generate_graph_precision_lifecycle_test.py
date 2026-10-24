#!/usr/bin/env python3
"""Generate a headless test from the exact current renderer method bodies.

Only GL allocation, source hashing, cache-store insertion and unrelated services
are faked. Precision selection, transition invalidation, ownership release,
source reload, and seeding control flow are extracted unchanged from production.
This does not validate live GL behavior or pixel accuracy. Extraction deliberately
fails on renamed method boundaries so refactors cannot silently test stale code.
"""
import argparse
from pathlib import Path
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
def extract(file,start,end):
 s=(root/file).read_text();a=s.index(start);return s[a:s.index(end,a)]
resources='src/Renderer/Internal/RenderPipelineResources.cpp'
methods=extract('src/Renderer/Internal/RenderPipelineGraphRenderTargets.cpp','void RenderPipeline::EnsureGraphFloat32Targets()', 'bool RenderPipeline::RenderIntoGraphTargetTextureImpl')
methods+=extract(resources,'void RenderPipeline::CleanupFBOs()', 'bool RenderPipeline::Resize(')
methods+=extract(resources,'bool RenderPipeline::Resize(', 'bool RenderPipeline::LoadSourceImage')
methods+=extract(resources,'void RenderPipeline::LoadSourceFromPixels(', 'void RenderPipeline::LoadSourceFromSharedPixels')
methods+=extract(resources,'void RenderPipeline::Clear()', 'void RenderPipeline::ClearRawDevelopmentLocalRangeOverlay')
methods+=extract('src/Renderer/Internal/RenderPipelineRawDevelopStageCache.cpp','void RenderPipeline::DestroyRawDevelopStageCache()', 'void RenderPipeline::PruneInactiveRawDevelopStageCache')
methods+=extract('src/Renderer/Internal/RenderPipelineGraphTextureCache.cpp','void RenderPipeline::DeleteGraphCacheEntry(', 'void RenderPipeline::ReleaseGraphCacheEntry(')
methods+=extract('src/Renderer/Internal/RenderPipelineGraphRenderTargets.cpp','void RenderPipeline::DestroyGraphTransientTargets()', 'std::uint64_t RenderPipeline::GraphTransientTargetBytes()')
s=(root/'src/Renderer/Internal/RenderPipelineCalibration.cpp').read_text();methods+=s[s.index('bool RenderPipeline::SeedViewportDependency'):]
predicate=extract('src/Renderer/Internal/RenderPipelineGraphExecution.cpp','    const bool sceneRaw =','    m_GraphSourceTexture = 0;')
estimate=extract('src/Renderer/Internal/RenderPipelineGraphExecutionHelpers.cpp','std::uint64_t EstimateRawDevelopStageCacheTextureBytes','std::size_t ResolveRawDevelopStageCacheMaxEntries')
pre=r'''#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include "Utils/PixelBufferUtils.h"
#include "Renderer/GraphTargetPrecision.h"
constexpr unsigned GL_RGBA32F=0x8814, GL_RGBA16F=0x881A;
std::map<unsigned,int> deleted;void glDeleteTextures(int n,const unsigned* p){for(int i=0;i<n;++i)if(p[i])++deleted[p[i]];}void glDeleteFramebuffers(int,const unsigned*){}
std::size_t HashBytes(const unsigned char* data,std::size_t size){std::size_t h=0;for(std::size_t i=0;i<size;++i)h=h*31+data[i];return h;}
namespace GLHelpers{unsigned next=1000,format=0;bool fail=false;unsigned CreateFBO(unsigned t){return t;}unsigned CreateStorageTexture(int,int,unsigned f){format=f;return fail?0:next++;}unsigned CreateEmptyTexture(int,int){format=GL_RGBA16F;return fail?0:next++;}unsigned CreateTextureFromPixels(const unsigned char*,int,int,int){return fail?0:next++;}}
namespace Stack::RawRecipe{struct RawDevelopmentRecipe{};}
namespace Stack::Renderer::RawDevelopmentCache{struct F{std::size_t neutralPlacement=123;};F BuildStageFingerprints(const Stack::RawRecipe::RawDevelopmentRecipe&,int){return {};}}
namespace Stack::Renderer{unsigned CopyViewportTexture(unsigned,int,int,int,int){return GLHelpers::fail?0:GLHelpers::next++;}struct ScopedGLTexture{unsigned t;explicit ScopedGLTexture(unsigned x):t(x){}~ScopedGLTexture(){if(t)glDeleteTextures(1,&t);}explicit operator bool()const{return t!=0;}unsigned Get()const{return t;}unsigned Release(){auto x=t;t=0;return x;}};}
namespace Stack::Renderer::GraphExecution{constexpr std::uint64_t kRawDevelopStageCacheBytesPerPixel=8;
'''
cls=r'''}
enum class RenderGraphNodeKind{Other,RawDevelopment,DataMath};struct Node{RenderGraphNodeKind kind;};struct Graph{int rawLayerBackgroundNodeId=0;std::vector<Node> nodes;};
struct RenderPipeline{
struct CachedGraphTexture{unsigned texture=0;bool owned=false;int width=2,height=2;std::size_t fingerprint=123;std::uint64_t bytes=0,lastUseSerial=0;bool requiresFloat32Seed=false;};
std::unordered_map<std::string,CachedGraphTexture> m_GraphImageCache,m_GraphMaskCache,m_LutTextureCache;
std::unordered_map<std::string,std::vector<CachedGraphTexture>> m_RawDevelopStageImageCache;
using GraphTransientTarget=CachedGraphTexture;std::vector<CachedGraphTexture> m_GraphTransientTargets;
bool m_GraphFloat32Targets=false,m_HasProvisionalRawRgbDenoiseOutput=false,storeFails=false;int m_Width=2,m_Height=2;
unsigned m_SourceTexture=0,m_ExternalOutputTexture=0,m_OutputTexture=0,m_GraphSourceTexture=0,m_PingTexture=0,m_PongTexture=0,m_PingFBO=0,m_PongFBO=0;
int m_BaseCanvasWidth=0,m_BaseCanvasHeight=0,m_GraphSourceWidth=0,m_GraphSourceHeight=0,m_SourceChannels=4,m_PreviewMaxDimension=0;
std::size_t m_SourceFingerprint=0;std::vector<unsigned char> m_SourcePixels;std::shared_ptr<std::vector<unsigned char>> m_SourcePixelsShared;
std::vector<int> m_RawPipelines,m_RawDataCache,m_RawSharedSourceData,m_RawDataCachePaths,m_RawPreviewDataCache,m_RawPreviewDataCacheKeys,m_RawDevelopmentLocalSuggestionImage,m_PreLocalExposureSummaries,m_RawDevelopmentGradingScopeReadback,m_GraphScalarCache,m_LastGraphImageCacheHits,m_AutoGainSceneStatsCache;
struct Warp{unsigned gateTexture=0,supportTexture=0,boundaryTexture=0,guideTexture=0;}m_RawDevelopmentColorWarpMaskCache;
struct Zone{void Clear(){}}m_RawZoneAreaRenderer;
int m_RawDevelopmentGradingScopeRequestRevision=0,m_RawDevelopmentGradingScopeLastIssueTime=0,statsInvalidations=0;
void InvalidateOutputTextureStats(){++statsInvalidations;}void InvalidateGraphCaches(bool=false);void ClearRawDevelopmentStageStatsReadbacks(){}void ClearRawDevelopmentLocalRangeOverlay(){}void ClearRawDevelopmentLocalRangeSelectionBits(){}void ClearRawDevelopmentLocalRangeTargetPreviewSelection(){}void ClearRawDevelopmentLocalRangeTargetSample(){}void ReleaseRawDevelopmentGradingScopeReadbackResources(){}
void CleanupFBOs();bool Resize(int,int);void Clear();void LoadSourceFromPixels(const unsigned char*,int,int,int);
void DestroyGraphCache(std::unordered_map<std::string,CachedGraphTexture>&);void DeleteGraphCacheEntry(CachedGraphTexture&);void DestroyGraphTransientTargets();void DestroyRawDevelopStageCache();void EnsureGraphFloat32Targets();void ConfigureGraphTargetPrecision(bool);unsigned CreateGraphRenderTargetTexture()const;std::uint64_t EstimateGraphTargetBytes(int,int)const;
bool SeedViewportDependency(const RenderPipeline&,const Stack::RawRecipe::RawDevelopmentRecipe&,int,int);void MarkSeededGraphPrecision(const std::string&,std::size_t);
bool StoreRawDevelopStageCacheEntry(const std::string& key,unsigned texture,std::size_t fingerprint,bool take=false){if(storeFails)return false;CachedGraphTexture e;e.texture=take?texture:GLHelpers::next++;e.owned=true;e.fingerprint=fingerprint;e.width=m_Width;e.height=m_Height;m_RawDevelopStageImageCache[key].push_back(e);return true;}
void ExecutePrecisionPredicate(const Graph& graph){
'''
main=r'''
void Check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
int main(){RenderPipeline p;Graph raster,math;math.nodes.push_back({RenderGraphNodeKind::DataMath});p.ExecutePrecisionPredicate(raster);Check(!p.m_GraphFloat32Targets,"fresh16");
p.m_GraphImageCache["image"]={11,true};p.m_GraphMaskCache["mask"]={12,true};p.m_RawDevelopStageImageCache["raw"]={{13,true}};p.m_GraphTransientTargets={{14,true}};p.m_SourceTexture=15;p.m_ExternalOutputTexture=16;p.m_OutputTexture=11;p.m_GraphSourceTexture=11;p.m_GraphScalarCache={1};
p.ExecutePrecisionPredicate(math);Check(p.m_GraphFloat32Targets&&p.m_OutputTexture==0&&p.m_GraphSourceTexture==0&&p.m_GraphScalarCache.empty(),"upgrade invalidates borrowed/cache state");for(unsigned id:{11,12,13,14})Check(deleted[id]==1,"delete each cache once");Check(deleted[15]==0&&deleted[16]==0,"source/external remain owned");
p.m_GraphImageCache["retained"]={17,true};auto revision=p.m_RawDevelopmentGradingScopeRequestRevision;p.ExecutePrecisionPredicate(math);Check(p.m_GraphImageCache.size()==1&&deleted[17]==0&&revision==p.m_RawDevelopmentGradingScopeRequestRevision,"same mode retains caches and scope");p.ExecutePrecisionPredicate(raster);Check(!p.m_GraphFloat32Targets&&deleted[17]==1,"downgrade");
RenderPipeline source;source.EnsureGraphFloat32Targets();source.m_RawDevelopStageImageCache["1:__rawDevelopmentRgbDenoise"]={{25,true}};Stack::RawRecipe::RawDevelopmentRecipe recipe;Check(p.SeedViewportDependency(source,recipe,2,2),"seed success");p.ExecutePrecisionPredicate(raster);Check(p.m_GraphFloat32Targets&&!p.m_RawDevelopStageImageCache.empty(),"seed protects ordinary graph");p.Resize(3,3);p.ExecutePrecisionPredicate(raster);Check(p.m_GraphFloat32Targets,"resize preserves seed floor");p.DestroyRawDevelopStageCache();p.ExecutePrecisionPredicate(raster);Check(!p.m_GraphFloat32Targets,"dependency release permits downgrade");
p.storeFails=true;Check(!p.SeedViewportDependency(source,recipe,2,2),"seed store failure");p.ExecutePrecisionPredicate(raster);Check(!p.m_GraphFloat32Targets,"failed seed creates no floor");p.storeFails=false;GLHelpers::fail=true;Check(!p.SeedViewportDependency(source,recipe,2,3),"seed copy allocation failure");GLHelpers::fail=false;p.ExecutePrecisionPredicate(raster);Check(!p.m_GraphFloat32Targets,"allocation failure no floor");
p.ExecutePrecisionPredicate(math);p.Clear();Check(!p.m_GraphFloat32Targets,"clear resets after teardown");Check(deleted[15]==1&&deleted[16]==1,"owned source/external released once by Clear");unsigned char rgba[4]={1,2,3,255};p.LoadSourceFromPixels(rgba,1,1,4);p.ExecutePrecisionPredicate(raster);Check(!p.m_GraphFloat32Targets,"ordinary reload16");p.ExecutePrecisionPredicate(math);GLHelpers::fail=true;unsigned char two[8]={};p.LoadSourceFromPixels(two,2,1,4);GLHelpers::fail=false;Check(!p.m_GraphFloat32Targets,"failed source allocation automatically clears precision");RenderPipeline raw;Graph rawGraph;rawGraph.nodes.push_back({RenderGraphNodeKind::RawDevelopment});raw.ExecutePrecisionPredicate(rawGraph);Check(raw.m_GraphFloat32Targets,"RAW node remains32");rawGraph.nodes.clear();rawGraph.rawLayerBackgroundNodeId=4;raw.ExecutePrecisionPredicate(rawGraph);Check(raw.m_GraphFloat32Targets,"RAW background remains32");std::cout<<"Exact-method fake-GL precision/lifecycle/seed/ownership tests passed\n";}
'''
args.output.write_text(pre+estimate+cls+predicate+'}};\n'+methods+main)
