#!/usr/bin/env python3
"""Run production decode/cache blocks with a counted decoder stand-in.

Checks cancellation/cache admission and metadata reuse, not LibRaw, GL or UI timing.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile


def generate(root):
    development = (root / 'src/Renderer/Internal/RenderPipelineGraphRawDevelopmentNode.cpp').read_text()
    stages = (root / 'src/Renderer/Internal/RenderPipelineGraphRawStages.cpp').read_text()
    latch = development[development.index('    bool canceled = false;', development.index('RenderPipeline::RenderRawDevelopmentGraphNode')):development.index('    const auto recordFailure', development.index('RenderPipeline::RenderRawDevelopmentGraphNode'))]
    source = development[development.index('    const std::string cacheKeyPath =', development.index('RenderPipeline::RenderRawDevelopmentGraphNode')):development.index('    const Raw::RawImageData* rawData =', development.index('RenderPipeline::RenderRawDevelopmentGraphNode'))]
    stage_latch = stages[stages.index('    bool canceled = false;'):stages.index('    stage.rawSource =')]
    stage_decode = stages[stages.index('        Raw::RawImageData loadedRaw;'):stages.index('\n    }\n\n    const bool rawDataHasPixels')]
    execution = (root / 'src/Renderer/Internal/RenderPipelineGraphExecution.cpp').read_text()
    metadata_branch = execution[execution.index('            if (node.rawDevelopment.embeddedRawData'):execution.index('\n        const auto inputsIt')]
    metadata_branch = metadata_branch[:metadata_branch.rfind('        }')]
    return r'''
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <iostream>
namespace Raw {
struct RawImageData { int pixels = 0; };
struct RawLoader {
    static inline int calls = 0, canceledCalls = 0;
    static bool LoadFile(const std::string&, RawImageData& out,
                         const std::function<bool()>& cancel = {}) {
        ++calls;
        if (cancel && cancel()) { ++canceledCalls; return false; }
        out.pixels = 73;
        return true;
    }
};
}
namespace Stack::Renderer::RawDevelopmentCache {
std::string BuildSourceDataIdentity(const std::string& source) { return source; }
}
void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
namespace MetadataCheck {
namespace Raw {
struct RawMetadata { int width=0, height=0; std::string error; };
struct RawImageData { RawMetadata metadata; };
struct RawLoader {
    static inline int calls=0;
    static bool LoadMetadata(const std::string&, RawMetadata& out) {
        ++calls; out.width=640; out.height=480; return true;
    }
};
}
struct Node {
    struct {
        std::shared_ptr<const Raw::RawImageData> embeddedRawData;
        std::shared_ptr<const Raw::RawMetadata> sourceMetadata;
        struct { struct { std::string sourcePath="input.raw"; } source; } recipe;
    } rawDevelopment;
};
bool Resolve(const Node& node, int& width, int& height) {
    std::string outError;
    auto acceptRawMetadata = [&](const Raw::RawMetadata& metadata) {
        width=metadata.width; height=metadata.height;
        return width>0 && height>0;
    };
''' + metadata_branch + r'''
    return false;
}
void Check() {
    Node node;
    auto metadata=std::make_shared<Raw::RawMetadata>();
    metadata->width=1200; metadata->height=800;
    node.rawDevelopment.sourceMetadata=metadata;
    int width=0, height=0;
    Require(Resolve(node,width,height) && width==1200 && height==800,"bound geometry lost");
    Require(Raw::RawLoader::calls==0,"bound metadata reopened file");
    auto embedded=std::make_shared<Raw::RawImageData>();
    embedded->metadata.width=200; embedded->metadata.height=100;
    node.rawDevelopment.embeddedRawData=embedded;
    Require(Resolve(node,width,height) && width==200,"embedded geometry precedence lost");
    Require(Raw::RawLoader::calls==0,"embedded geometry reopened file");
    node.rawDevelopment.embeddedRawData.reset();
    for (int mode=0; mode<3; ++mode) {
        node.rawDevelopment.sourceMetadata=metadata;
        metadata->width=1200; metadata->error.clear();
        if (mode==0) node.rawDevelopment.sourceMetadata.reset();
        if (mode==1) metadata->error="unavailable";
        if (mode==2) metadata->width=0;
        Require(Resolve(node,width,height) && width==640 && height==480,
            "missing/error/invalid metadata did not fall back");
        Require(Raw::RawLoader::calls==mode+1,"metadata fallback count mismatch");
    }
}
}
struct Pipeline {
    std::function<bool()> m_ShouldCancelRender;
    std::unordered_map<std::string, std::shared_ptr<const Raw::RawImageData>> m_RawSharedSourceData;
    // Extract only mid-decode admission, omitting the entrypoint's earlier
    // cancellation check so the one-shot signal occurs inside the decoder.
    int Open(const std::string& sourcePath) {
        int result = 0;
        struct { std::string source; } recipe { sourcePath };
        std::shared_ptr<const Raw::RawImageData> embeddedRawData;
''' + latch + source + r'''
        return sourceLease->pixels;
    }
    bool Stage(Raw::RawImageData& rawData, std::string& cachedPath, const std::string& key) {
        struct { std::string sourcePath; operator bool() const { return false; } } stage { key };
        const auto& cacheKeyPath = key;
''' + stage_latch + stage_decode + r'''
        return true;
    }
};
int main() {
    MetadataCheck::Check();
    Pipeline p;
    Require(p.Open("A") == 73, "cold A decode failed");
    const auto acceptedA = p.m_RawSharedSourceData.at("A");
    for (int i=0; i<7; ++i) p.Open("resident" + std::to_string(i));
    Require(p.m_RawSharedSourceData.size() == 8, "cache setup failed");
    const auto accepted = p.m_RawSharedSourceData;
    // A -> canceled B -> A must preserve all existing cache owners, even at
    // the eviction threshold, and must not admit B's incomplete payload.
    bool cancelOnce = true;
    p.m_ShouldCancelRender = [&] { bool value=cancelOnce; cancelOnce=false; return value; };
    const int beforeCancel = Raw::RawLoader::calls;
    Require(p.Open("B") == 0, "canceled B was accepted");
    Require(Raw::RawLoader::calls == beforeCancel + 1, "decoder not called");
    Require(Raw::RawLoader::canceledCalls == 1, "decoder did not receive cancellation");
    Require(p.m_RawSharedSourceData == accepted, "cancel cached or evicted data");
    p.m_ShouldCancelRender = {};
    const int beforeWarm = Raw::RawLoader::calls;
    Require(p.Open("A") == 73 && p.m_RawSharedSourceData.at("A") == acceptedA, "A owner changed");
    Require(Raw::RawLoader::calls == beforeWarm, "warm A decoded again");
    Require(p.Open("B") == 73, "retry B failed");
    Require(Raw::RawLoader::calls == beforeWarm + 1, "retry did not decode");
    const int beforeWarmB = Raw::RawLoader::calls;
    Require(p.Open("B") == 73 && Raw::RawLoader::calls == beforeWarmB, "warm B decoded again");
    Raw::RawImageData raw { 91 };
    std::string path = "accepted-A";
    int checks = 0;
    // The first check enters the stage; the decoder's second check cancels.
    // A later false signal must not make the canceled candidate reusable.
    p.m_ShouldCancelRender = [&] { return ++checks == 2; };
    Require(!p.Stage(raw, path, "B"), "canceled stage accepted");
    Require(Raw::RawLoader::canceledCalls == 2, "stage decoder did not receive cancellation");
    Require(raw.pixels == 91 && path == "accepted-A", "stage overwrote accepted A");
    p.m_ShouldCancelRender = {};
    Require(p.Stage(raw, path, "B") && raw.pixels == 73 && path == "B", "stage retry failed");
    std::cout << "RAW decode cancellation/cache and metadata reuse checks passed\n";
}
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cxx', default='c++')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    with tempfile.TemporaryDirectory(prefix='stack-raw-decode-cancel-') as directory:
        source = Path(directory) / 'check.cpp'
        binary = Path(directory) / 'check'
        source.write_text(generate(root))
        subprocess.run([args.cxx, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary)], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    main()
