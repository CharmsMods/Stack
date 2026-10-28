#!/usr/bin/env python3
"""Generate focused CPU checks of production Gallery preview resolution/state.

Texture/cache and catalog lookups are boundary doubles. Actual hover/selection
resolution, workspace preview state and filmstrip image resolver are extracted;
this does not render pixels or run the native Before/After inspection renderer.
"""
import argparse
from pathlib import Path


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    base = root / 'src/Editor/Internal/RawLab'
    workspace = (base / 'EditorModuleRawLabGalleryWorkspace.cpp').read_text()
    start = workspace.index('    const auto* requestedSource =')
    end = workspace.index('    const auto drawPreview =', start)
    state = workspace[start:end]
    filmstrip = (base / 'EditorModuleRawLabFilmstripPreview.cpp').read_text()
    start = filmstrip.index('    struct PreviewImage {')
    end = filmstrip.index('    const PreviewImage current =', start)
    resolver = filmstrip[start:end]
    overlay = filmstrip[start:filmstrip.index('    const float opacity =', end)]
    # Project-only hover must reach both consumers rather than preserve a prior
    # source hover or gate a valid saved cover off before its resolver runs.
    gallery = (base / 'EditorModuleRawLabGallery.cpp').read_text()
    hover = gallery.split('if ((compactFilmstrip || m_PermanentGalleryWorkspace) && tileHovered)', 1)[1].split('if (gridSourceStack)', 1)[0]
    assert hover.count('!hoverKey.empty() || !activeProjectPath.empty()') == 2
    host = (root / 'src/Editor/Internal/EditorModuleRawWorkspaceLab.cpp').read_text()
    target = host.split('const bool filmstripHoverTarget =', 1)[1].split(';', 1)[0]
    assert '!m_RawWorkspaceLabFilmstripHoverProjectPath.empty()' in target
    generated = r'''
#include "Editor/Internal/RawLab/RawLabGalleryPreviewState.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
namespace Raw {struct RawMetadata {};}
namespace Stack::RawWorkspace {
enum class ThumbnailStatus {Ready};
struct SourceRecord {std::string relativePathKey; struct {std::filesystem::path absolutePath;ThumbnailStatus status;} thumbnail;};
}
namespace ImGui {double time=1;struct IO {float DeltaTime=.016f;} io;double GetTime(){return time;} IO& GetIO(){return io;}}
void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
struct ProjectView {std::filesystem::path projectPath,coverThumbnailCachePath;std::string projectId,referenceSourceKey;};
struct Fixture {
    struct {std::string selectedSourceKey;} m_RawWorkspace;
    struct Project {std::string rawSourceKey;} project;
    Project* m_Project=&project;
    std::string m_RawWorkspaceLabFilmstripHoverSourceKey;
    std::filesystem::path m_RawWorkspaceLabFilmstripHoverProjectPath,m_RawWorkspaceLabFocusedProjectPath;
    std::string m_RawWorkspaceLabFilmstripPreviousHoverSourceKey;
    std::filesystem::path m_RawWorkspaceLabFilmstripPreviousHoverProjectPath;
    float m_RawWorkspaceLabFilmstripHoverBlend=1.f;
    struct {std::vector<ProjectView> projects;} gallery;
    std::vector<Stack::RawWorkspace::SourceRecord> sources;
    std::unordered_map<std::string,unsigned int> textures;
    Stack::Editor::RawLabInternal::GalleryPreviewState preview;
    const Stack::RawWorkspace::SourceRecord* FindRawWorkspaceSourceByKey(const std::string& key) {
        for (const auto& source:sources) if(source.relativePathKey==key)return &source;
        return nullptr;
    }
    const auto& GetRawWorkspaceGalleryPresentation(){return gallery;}
    unsigned int GetRawWorkspaceThumbnailTexture(const Stack::RawWorkspace::SourceRecord& source,int* width,int* height,bool) {
        *width=400;*height=300;return textures[source.thumbnail.absolutePath.generic_string()];
    }
    std::shared_ptr<const Raw::RawMetadata> GetRawWorkspaceThumbnailMetadata(const std::string&){return {};}
    struct Result {unsigned int current,previous;bool available;};
    Result overlayResult{};
    Result step() {
        const std::string workspaceKey="scope";
''' + state + r'''
        return {current.texture,previous.texture,requested.available};
    }
    Result filmstrip(const std::string& key,const std::filesystem::path& path) {
''' + resolver + r'''
        const auto result=resolveImage(key,path);
        return {result.texture,0,result.available};
    }
    void overlay() {
        overlayResult={};
''' + overlay + r'''
        overlayResult={current.texture,previous.texture,current.available};
    }
};
int main() try {
    Fixture f;
    f.sources.push_back({"raw",{"raw-thumb",Stack::RawWorkspace::ThumbnailStatus::Ready}});
    f.gallery.projects={{"A","cover-A","A",""},{"B","cover-B","B",""},{"missing","","missing",""},
                        {"fallback","","fallback","raw"}};
    f.textures={{"raw-thumb",1},{"cover-A",2},{"cover-B",3}};
    f.m_RawWorkspace.selectedSourceKey="raw";
    Require(f.step().current==1,"standalone original preview regressed");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="A";
    Require(f.step().current==2 && f.preview.sourceKey.empty() && f.preview.projectPath=="A",
        "project-only hover did not use saved cover independently of selected original");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
    f.m_RawWorkspace.selectedSourceKey.clear();f.m_RawWorkspaceLabFocusedProjectPath="B";
    Require(f.step().current==3 && f.preview.projectPath=="B","project-only selection did not resolve cover");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="A";
    Require(f.step().current==2,"rapid project switch did not update image");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="B";f.textures["cover-B"]=0;
    Require(f.step().current==2 && f.preview.projectPath=="A","pending cover lost displayed image identity");
    f.textures["cover-B"]=3;
    Require(f.step().current==3 && f.preview.projectPath=="B","ready project cover was not adopted");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="A";
    Require(f.step().current==2,"return to project A failed");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="B";f.textures["cover-B"]=0;
    Require(f.step().current==2 && f.preview.projectPath=="A","loading B mislabeled A's pixels");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="missing";
    const auto unavailable=f.step();
    Require(!unavailable.current && !unavailable.previous && !unavailable.available &&
        f.preview.sourceKey.empty() && f.preview.projectPath.empty() &&
        f.preview.previousSourceKey.empty() && f.preview.previousProjectPath.empty(),
        "unavailable target retained an unrelated previous image/fade");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
    f.m_RawWorkspaceLabFocusedProjectPath="missing";f.m_RawWorkspace.selectedSourceKey="raw";
    Require(!f.step().current,"project-only selection borrowed an unrelated selected original");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="fallback";
    Require(f.step().current==1 && f.preview.projectPath=="fallback","source-backed project fallback regressed");
    const auto projectCover=f.filmstrip("", "A");
    Require(projectCover.current==2 && projectCover.available,"filmstrip cover required an original");
    Require(f.filmstrip("raw", "fallback").current==1,"filmstrip source fallback regressed");
    Require(!f.filmstrip("", "missing").available,"filmstrip unavailable project became an original");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="B";
    f.m_RawWorkspaceLabFilmstripPreviousHoverProjectPath="A";
    f.m_RawWorkspaceLabFilmstripHoverBlend=.5f;
    f.overlay();
    Require(f.overlayResult.previous==2 && f.overlayResult.available &&
        f.m_RawWorkspaceLabFilmstripHoverBlend==0.f,"filmstrip loading cover lost previous preview");
    f.m_RawWorkspaceLabFilmstripHoverProjectPath="missing";
    f.overlay();
    Require(!f.overlayResult.current && !f.overlayResult.previous &&
        f.m_RawWorkspaceLabFilmstripPreviousHoverProjectPath.empty() &&
        f.m_RawWorkspaceLabFilmstripPreviousHoverSourceKey.empty() &&
        f.m_RawWorkspaceLabFilmstripHoverBlend==1.f,"filmstrip unavailable target kept previous fade");
    std::cout<<"Gallery project-only hover/selection, cover loading, rapid switches and unavailable fallback passed.\n";
} catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
'''
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(generated)


if __name__ == '__main__':
    main()
