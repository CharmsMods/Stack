#include "App/UiPerformanceProbe.h"
#include "Editor/EditorModule.h"
#include "RawLabGalleryDetails.h"
#include "RawLabUiSupport.h"
#include "App/AppHeaderStyle.h"
#include "App/WorkspaceInputScope.h"
#include "Library/LibraryControls.h"
#include "Editor/UI/WindowDrawOpacity.h"
#include "Utils/HoverTextScope.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

#include <imgui.h>
#include <imgui_internal.h>

void EditorModule::RenderRawWorkspaceLabGalleryWorkspace(const ImVec2& size) {
    ImGuiExtras::HoverTextScope noHoverText;
    const bool transitioning=m_PermanentGalleryWorkspace && m_GalleryNavigationMotion &&
        m_GalleryNavigationMotion->FoldersMoving();
    const bool folders=m_PermanentGalleryWorkspace && IsRawWorkspaceFolderBrowserOpen();
    const ImVec2 origin=ImGui::GetCursorScreenPos();
    if(!folders || transitioning) {
        Stack::Workspace::InputScope suppress(transitioning,false);
        ImGui::BeginChild("GalleryNavigationContent",size,ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground|ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse);
        RenderRawWorkspaceGalleryContentSurface(size);
        if(transitioning)Stack::Editor::ApplyWindowDrawOpacity(ImGui::GetCurrentWindow(),
            1.f-m_GalleryNavigationMotion->FolderAmount());
        ImGui::EndChild();
    }
    if(folders || transitioning) {
        ImGui::SetNextWindowPos(origin);ImGui::SetNextWindowSize(size);
        ImGui::SetNextWindowViewport(ImGui::GetWindowViewport()->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,0.f);
        ImGui::Begin("GallerySourceFolderSurface",nullptr,ImGuiWindowFlags_NoDecoration|
            ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground|
            ImGuiWindowFlags_NoFocusOnAppearing|(transitioning?ImGuiWindowFlags_NoInputs:0));
        Stack::Workspace::InputScope suppress(transitioning,false);
        if(m_GalleryNavigationMotion)ImGui::PushStyleVar(ImGuiStyleVar_Alpha,
            ImGui::GetStyle().Alpha*m_GalleryNavigationMotion->FolderAmount());
        RenderRawGallerySourceFolders(size);
        if(m_GalleryNavigationMotion)ImGui::PopStyleVar();
        ImGui::End();ImGui::PopStyleVar(2);
    }
    if (Stack::UiPerformance::Probe::Active()) {
        namespace P=Stack::UiPerformance::Probe;
        P::Drawn(P::Visual::GalleryMode,P::Key(m_RawWorkspaceLabUi.galleryWorkspaceGrid?1.f:2.f,
            m_GalleryNavigationMotion ? m_GalleryNavigationMotion->FolderAmount() : (folders?1.f:0.f)));
        P::Motion(P::Visual::GalleryMode,transitioning || m_RawWorkspaceGalleryLayoutAnimation.Active());
    }
}

void EditorModule::RenderRawWorkspaceGalleryContentSurface(const ImVec2& size) {
    m_RawGalleryPresentationFrame=ImGui::GetFrameCount();
    const bool grid = m_RawWorkspaceLabUi.galleryWorkspaceGrid;
    const bool previewHoverArmed = m_RawWorkspaceGalleryPreviewHovered;
    m_RawWorkspaceGalleryPreviewHovered = false;
    const float unit = ImGui::GetFontSize() / 13.0f;
    const bool showProperties = m_RawWorkspaceLabUi.galleryPropertiesOpen;
    const float target = showProperties ? 1.f : 0.f;
    if (m_RawGalleryPropertiesAmount < 0.f)
        m_RawGalleryPropertiesAmount = target;
    m_RawGalleryPropertiesMotion.Update(m_RawGalleryPropertiesAmount,target,.24f,
        ImGui::GetTime(),ImGui::GetFrameCount(),Stack::LibraryUi::ReducedMotion());
    const float propertiesAmount = m_RawGalleryPropertiesAmount;
    const float fullDetailsWidth = std::min(320.0f * ImGui::GetFontSize() / 16.f,
        std::max(1.0f, size.x * 0.34f));
    const float detailsWidth = fullDetailsWidth * propertiesAmount;
    if(Stack::UiPerformance::Probe::Active()) {
        namespace P=Stack::UiPerformance::Probe;
        P::Drawn(P::Visual::Controls,P::Key(detailsWidth));
        P::Motion(P::Visual::Controls,propertiesAmount!=target);
    }
    // Return the inspector lane and its gutter to the grid as the panel closes.
    const float gridGutter = grid ? std::clamp(size.x * .025f, 12.f * unit, 36.f * unit) : 0.f;
    const float imageWidth = std::max(1.0f, size.x - (detailsWidth + gridGutter * propertiesAmount));

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
    // This is one docked workspace surface. Inner layout children must not
    // inherit rounded, separately shaded window chrome from the app theme.
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, GetWorkspaceBaseColor());
    ImGui::BeginChild("RawGalleryWorkspace", size, false,
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    if (grid) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f * unit, 12.0f * unit));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(16.0f, 16.0f));
        auto* host = ImGui::GetCurrentWindow();
        auto& scrollState = host->StateStorage;
        const auto timeKey = ImGui::GetID("GridScrollTime");
        const auto posKey = ImGui::GetID("GridScrollPosition");
        const float now = static_cast<float>(ImGui::GetTime());
        const float age = now-scrollState.GetFloat(timeKey,-100.f);
        const float opacity = std::clamp((2.f-age)/.18f,0.f,1.f);
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,{0,0,0,0});
        for (auto color : {ImGuiCol_ScrollbarGrab,ImGuiCol_ScrollbarGrabHovered,ImGuiCol_ScrollbarGrabActive}) {
            auto ink = ImGui::GetStyleColorVec4(color); ink.w*=opacity; ImGui::PushStyleColor(color,ink);
        }
        ImGui::BeginChild("Grid", ImVec2(imageWidth, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
            ImGuiWindowFlags_NoScrollWithMouse);
        RenderRawWorkspaceLabGalleryContent(false);
        // Gaps retain the last hover; leaving the grid restores selection.
        if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
            m_RawWorkspaceLabFilmstripHoverSourceKey.clear();
            m_RawWorkspaceLabFilmstripHoverProjectPath.clear();
            m_RawWorkspaceLabFilmstripHoverSuppressed = false;
        } else m_RawWorkspaceLabFilmstripHoverFrame = ImGui::GetFrameCount();
        const auto* body = ImGui::GetCurrentWindow();
        if (scrollState.GetFloat(posKey,body->Scroll.y)!=body->Scroll.y ||
            (ImGui::GetIO().MouseWheel!=0.f && ImGui::IsWindowHovered())) scrollState.SetFloat(timeKey,now);
        scrollState.SetFloat(posKey,body->Scroll.y);
        ImGui::EndChild();
        ImGui::PopStyleColor(4);
        ImGui::PopStyleVar(2);
    }

    // Grid thumbnails already own their cache requests. With properties fully
    // closed there is no preview consumer to resolve, upload or format for.
    if (grid && propertiesAmount == 0.f) {
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(2);
        return;
    }

    // Resolve after the grid handles input, since its actions may change selection.
    auto& preview = m_RawWorkspaceGalleryPreview;
    const std::string workspaceKey = m_RawWorkspace.workspaceRoot.lexically_normal().generic_string();
    if (preview.workspaceKey != workspaceKey) {
        preview = {};
        preview.workspaceKey = workspaceKey;
    }
    // Hover drives browsing even while another image is selected. Edit actions
    // capture their selection separately and never use this preview identity.
    const auto* requestedSource = m_RawWorkspaceLabFilmstripHoverSourceKey.empty() ? nullptr :
        FindRawWorkspaceSourceByKey(m_RawWorkspaceLabFilmstripHoverSourceKey);
    std::filesystem::path requestedProject = m_RawWorkspaceLabFilmstripHoverProjectPath;
    if (!requestedSource && requestedProject.empty()) {
        requestedProject = m_RawWorkspaceLabFocusedProjectPath;
        // A mixed selection may retain an unrelated original. Resolve a
        // focused project's own reference below instead of borrowing that image.
        requestedSource = !requestedProject.empty() || m_RawWorkspace.selectedSourceKey.empty() ? nullptr :
            FindRawWorkspaceSourceByKey(m_RawWorkspace.selectedSourceKey);
    }
    if (!requestedSource && !requestedProject.empty()) {
        const auto& projects = GetRawWorkspaceGalleryPresentation().projects;
        const auto project = std::find_if(projects.begin(), projects.end(), [&](const auto& item) {
            return item.projectPath == requestedProject;
        });
        if (project != projects.end()) {
            requestedSource = FindRawWorkspaceSourceByKey(project->referenceSourceKey);
            if (!requestedSource && project->projectIndex < m_RawWorkspace.sourceSetProjects.size()) {
                const auto& card = m_RawWorkspace.sourceSetProjects[project->projectIndex];
                requestedSource = card.thumbnailOriginal ? card.thumbnailOriginal.get() :
                    card.linkedOriginal ? card.linkedOriginal.get() : card.masterSource.get();
            }
        }
    }
    if (!requestedSource && requestedProject.empty() && !m_Project->rawSourceKey.empty())
        requestedSource = FindRawWorkspaceSourceByKey(m_Project->rawSourceKey);

    struct Thumbnail {
        unsigned int texture = 0;
        std::shared_ptr<const Raw::RawMetadata> metadata;
        int width = 0;
        int height = 0;
        bool available = false;
        bool edited = false;
        bool ready() const { return texture != 0 && width > 0 && height > 0; }
    };
    const auto resolve = [&](const Stack::RawWorkspace::SourceRecord* source,
                             const std::filesystem::path& projectPath) {
        Thumbnail result;
        if (projectPath.empty()) {
            if (!source) return result;
            result.available = true;
            result.texture = GetRawWorkspaceThumbnailTexture(*source, &result.width, &result.height, true);
            result.metadata = GetRawWorkspaceThumbnailMetadata(source->relativePathKey);
            return result;
        }
        Stack::RawWorkspace::SourceRecord thumbnailSource;
        result.available = source != nullptr;
        bool projectFound = false;
        if (!projectPath.empty()) {
            const auto& gallery = GetRawWorkspaceGalleryPresentation();
            const auto normalizedProjectPath = projectPath.lexically_normal();
            const auto project = std::find_if(gallery.projects.begin(), gallery.projects.end(),
                [&](const auto& candidate) {
                    return !candidate.projectPath.empty() &&
                        candidate.projectPath.lexically_normal() == normalizedProjectPath;
                });
            if (project != gallery.projects.end()) {
                projectFound = true;
                if (!source && project->projectIndex < m_RawWorkspace.sourceSetProjects.size()) {
                    const auto& card = m_RawWorkspace.sourceSetProjects[project->projectIndex];
                    source = card.thumbnailOriginal ? card.thumbnailOriginal.get() :
                        card.linkedOriginal ? card.linkedOriginal.get() : card.masterSource.get();
                }
                result.available = source || !project->coverThumbnailCachePath.empty() ||
                    (m_GalleryComparisonEnabled && UsesGalleryPresetPreviews());
                result.edited = !project->coverThumbnailCachePath.empty() ||
                    (m_GalleryComparisonEnabled && UsesGalleryPresetPreviews());
                thumbnailSource.relativePathKey = project->projectCoverKey;
                thumbnailSource.thumbnail.absolutePath = project->coverThumbnailCachePath;
                thumbnailSource.thumbnail.status = Stack::RawWorkspace::ThumbnailStatus::Ready;
            }
        }
        if (!result.available) return result;
        result.texture = GetRawWorkspaceThumbnailTexture(result.edited ? thumbnailSource : *source, &result.width, &result.height, true);
        if (source) {
            const auto original = GetRawWorkspaceThumbnailTexture(*source,nullptr,nullptr,true);
            if (original && original == result.texture) result.edited = false;
            result.metadata = GetRawWorkspaceThumbnailMetadata(source->relativePathKey);
        } else if (projectFound) {
            const auto cover = m_RawWorkspaceThumbnailTextures.find(thumbnailSource.relativePathKey);
            if (cover != m_RawWorkspaceThumbnailTextures.end() && cover->second.transparentCover &&
                !(m_GalleryComparisonEnabled && UsesGalleryPresetPreviews())) result.available = false;
        }
        return result;
    };
    const Thumbnail requested = resolve(requestedSource, requestedProject);
    const std::string requestedSourceKey = requestedSource ? requestedSource->relativePathKey : std::string{};
    // Retain the old image only while a resolvable thumbnail loads. A missing
    // original with no saved cover is not a pending image from the old project.
    if (!requested.available) {
        preview = {};
        preview.workspaceKey = workspaceKey;
    }
    const auto* displayedSource = preview.sourceKey.empty() ? nullptr : FindRawWorkspaceSourceByKey(preview.sourceKey);
    if (!displayedSource && preview.projectPath == requestedProject) displayedSource = requestedSource;
    if (!displayedSource && preview.projectPath.empty()) {
        preview.sourceKey.clear();
        preview.previousSourceKey.clear();
        preview.blend = 1.0f;
    }
    const Thumbnail displayed = displayedSource == requestedSource && preview.projectPath == requestedProject
        ? requested : resolve(displayedSource, preview.projectPath);
    // Keep the previous image and its properties while an uncached thumbnail loads.
    if (requested.ready() && (preview.sourceKey != requestedSourceKey ||
            preview.projectPath != requestedProject)) {
        const double now = ImGui::GetTime();
        const bool rapidSwitch = preview.blend < 1.0f ||
            (preview.lastChangedAt >= 0.0 && now - preview.lastChangedAt < .16);
        const bool sameAspect = displayed.ready() && std::abs(
            static_cast<float>(requested.width) / requested.height -
            static_cast<float>(displayed.width) / displayed.height) < .001f;
        // Rapid sweeps switch at full opacity. Restarting an unfinished fade
        // from an older image makes the preview jump backwards and pulse.
        // Different aspect ratios also switch directly, keeping letterbox
        // changes out of the dissolve.
        if (!rapidSwitch && sameAspect) {
            preview.previousSourceKey = preview.sourceKey;
            preview.previousProjectPath = preview.projectPath;
            preview.blend = 0.0f;
        } else {
            preview.previousSourceKey.clear();
            preview.previousProjectPath.clear();
            preview.blend = 1.0f;
        }
        preview.sourceKey = requestedSourceKey;
        preview.projectPath = requestedProject;
        preview.lastChangedAt = now;
        displayedSource = requestedSource;
    }
    if (preview.blend < 1.0f) {
        preview.blend = std::min(1.0f, preview.blend +
            std::clamp(ImGui::GetIO().DeltaTime, 0.0f, 0.05f) / 0.12f);
        if (preview.blend >= 1.0f) {
            preview.previousSourceKey.clear();
            preview.previousProjectPath.clear();
        }
    }
    const Thumbnail current = displayedSource == requestedSource && preview.projectPath == requestedProject
        ? requested : displayed;
    const Thumbnail previous = preview.previousSourceKey.empty() && preview.previousProjectPath.empty() ? Thumbnail{} :
        resolve(FindRawWorkspaceSourceByKey(preview.previousSourceKey), preview.previousProjectPath);
    const auto drawPreview = [&](const ImVec2& available) {
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 extent(origin.x + available.x, origin.y + available.y);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec4 background = GetWorkspaceBaseColor();
        draw->AddRectFilled(origin, extent, ImGui::GetColorU32(background));
        const auto imageBounds = [&](const Thumbnail& thumbnail) {
            const ImVec2 imageSize = Stack::Editor::RawLabInternal::FitLabImage(
                static_cast<float>(thumbnail.width), static_cast<float>(thumbnail.height),
                ImVec2(std::max(1.0f, available.x - 32.0f * unit),
                       std::max(1.0f, available.y - 24.0f * unit)));
            const ImVec2 minimum(origin.x + (available.x - imageSize.x) * 0.5f,
                origin.y + (available.y - imageSize.y) * 0.5f);
            return std::make_pair(minimum, ImVec2(minimum.x + imageSize.x, minimum.y + imageSize.y));
        };
        const auto drawImage = [&](const Thumbnail& thumbnail, float alpha) {
            if (!thumbnail.ready() || alpha <= 0.0f) return;
            const auto [minimum, maximum] = imageBounds(thumbnail);
            draw->AddImage(
                static_cast<ImTextureID>(static_cast<intptr_t>(thumbnail.texture)), minimum, maximum,
                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)));
            if (!grid && ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(minimum, maximum)) {
                m_RawWorkspaceGalleryPreviewHovered = previewHoverArmed || ImGui::GetIO().MouseDelta.y < -0.5f;
            }
        };
        // Dissolve the new image over an opaque old image to avoid a dark dip.
        drawImage(previous, 1.0f);
        Thumbnail full = current;
        if (full.ready() && requestedSource && displayedSource == requestedSource &&
            requestedProject.empty() && preview.projectPath.empty()) {
            const ImVec2 area(std::max(1.f, available.x - 32.f * unit), std::max(1.f, available.y - 24.f * unit));
            if (auto detail = GetGalleryOriginalDetail(*requestedSource, area, full.width, full.height))
                full.texture = detail;
        }
        drawImage(full, previous.ready() ? preview.blend : 1.0f);
        if(full.ready() && ImGui::IsWindowHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            const auto bounds=imageBounds(full);
            if(ImGui::IsMouseHoveringRect(bounds.first,bounds.second)) {
                // Open the displayed identity, even while another hover target loads.
                if(!preview.projectPath.empty())RequestOpenRawWorkspaceProjectFromGallery(preview.projectPath);
                else if(!preview.sourceKey.empty())SelectRawWorkspaceSourceForGallery(preview.sourceKey,false,false,true);
                return;
            }
        }
        if (grid) {
            ImGui::Dummy(available);
            return;
        }
        ImGui::InvisibleButton("PreviewBrowse", available);
        const bool hovered = ImGui::IsItemHovered();
        auto* storage = ImGui::GetStateStorage();
        const ImGuiID wheelKey = ImGui::GetID("PreviewBrowseWheel");
        if (!hovered) storage->SetFloat(wheelKey, 0.f);
        if (hovered && ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY) &&
            !ImGui::IsAnyItemActive() && ImGui::GetIO().MouseWheel != 0.f) {
            const float accumulated = storage->GetFloat(wheelKey) + ImGui::GetIO().MouseWheel;
            const int steps = static_cast<int>(std::clamp(accumulated, -100.f, 100.f));
            storage->SetFloat(wheelKey, accumulated - steps);
            if (steps == 0) return;
            // Resolve browsing order only on wheel input, using the same cached
            // filtered stacks as the filmstrip, including their member images.
            struct Target { std::string source; std::filesystem::path project; std::size_t slot; };
            std::vector<Target> targets;
            const bool projectsOnly = m_RawWorkspaceLabUi.galleryNavigationMode ==
                RawGalleryNavigationMode::ProjectRoot && m_RawWorkspaceGalleryContentMode !=
                Stack::RawWorkspace::GalleryContentMode::Gallery;
            if (projectsOnly) {
                const auto& presentation = GetRawWorkspacePanelGalleryPresentation();
                std::size_t slot = 0;
                for (const auto& family : ResolveRawWorkspaceGalleryProjectStacks()) {
                    for (const auto index : family.projectIndices) {
                        const auto& project = presentation.projects[index];
                        targets.push_back({project.referenceSourceKey, project.projectPath, slot});
                    }
                    ++slot;
                }
            } else {
                std::size_t slot = 0;
                for (const auto& stack : ResolveRawWorkspaceVisibleGalleryStacks()) {
                    for (const auto& key : stack.sourceKeys) targets.push_back({key, {}, slot});
                    ++slot;
                }
            }
            if (targets.empty()) return;
            const std::string& sourceKey = requestedSource ? requestedSource->relativePathKey : preview.sourceKey;
            const auto found = std::find_if(targets.begin(), targets.end(), [&](const Target& item) {
                return requestedProject.empty() ? item.project.empty() && item.source == sourceKey
                    : item.project == requestedProject;
            });
            const int index = found == targets.end() ? 0 : static_cast<int>(found - targets.begin());
            const int next = std::clamp(index - steps, 0, static_cast<int>(targets.size()) - 1);
            if (found != targets.end() && next == index) return;
            const auto chosen = targets[next];
            if (chosen.project.empty()) {
                m_RawWorkspaceLabSelectedProjectPaths.clear();
                m_RawWorkspaceLabFocusedProjectPath.clear();
                m_RawWorkspaceLabFocusedProjectName.clear();
                SelectRawWorkspaceSourceForGallery(chosen.source, false, false, false);
            } else {
                m_RawWorkspaceLabSelectedProjectPaths.assign(1, chosen.project);
                m_RawWorkspaceLabFocusedProjectPath = chosen.project;
                m_RawWorkspaceLabProjectSelectionAnchor = chosen.project;
                const auto& presentation = GetRawWorkspacePanelGalleryPresentation();
                for (const auto& family : ResolveRawWorkspaceGalleryProjectStacks()) {
                    for (const auto member : family.projectIndices) {
                        const auto& project = presentation.projects[member];
                        if (project.projectPath != chosen.project) continue;
                        m_RawWorkspaceLabFocusedProjectName = project.projectName;
                        for (const auto sibling : family.projectIndices)
                            m_RawWorkspaceGalleryProjectCoverIds[presentation.projects[sibling].projectCoverKey] = project.projectCoverKey;
                    }
                }
                m_RawWorkspace.selectedSourceKeys.clear();
                m_RawWorkspace.selectedSourceKey.clear();
                InvalidateRawWorkspaceGalleryPresentation(false);
            }
            auto& slots = m_RawWorkspaceGalleryGridStacks.filmstripSlots;
            if (slots.enabled && chosen.slot < slots.count) {
                // Each legal stop has its own visible span. Choose the one
                // whose center is closest to the selected image's center.
                const float center = slots.Position(chosen.slot) + slots.ItemWidth(chosen.slot) * .5f + slots.gap * .5f;
                std::size_t best = 0;
                float distance = std::numeric_limits<float>::max();
                for (std::size_t stop = 0; stop <= slots.lastStop; ++stop) {
                    const float error = std::abs(slots.Position(stop) + slots.spans[stop] * .5f - center);
                    if (error < distance) { distance = error; best = stop; }
                }
                m_RawWorkspaceLabFilmstripScrollTargetX = slots.Position(best);
            }
            m_RawWorkspaceLabFilmstripHoverSourceKey = targets[next].source;
            m_RawWorkspaceLabFilmstripHoverProjectPath = targets[next].project;
            m_RawWorkspaceLabFilmstripHoverSuppressed = false;
            m_RawWorkspaceLabFilmstripHoverFrame = ImGui::GetFrameCount();
        }
    };
    const auto drawEmpty = [&] {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f * unit, 8.0f * unit));
        if (requested.available) {
            ImGui::TextDisabled("Loading preview...");
        } else if (!requestedProject.empty()) {
            ImGui::TextDisabled("No saved cover or original image is available.");
        } else if (m_RawWorkspace.workspaceRoot.empty()) {
            ImGui::TextUnformatted("Browse RAW images");
            ImGui::TextWrapped("Open a folder to browse its images and camera properties.");
            if (ImGui::Button("Open RAW Folder", ImVec2(160.0f * unit, 32.0f * unit)))
                OpenRawWorkspaceFolderDialog();
        } else if (IsRawWorkspaceScanBusy()) {
            ImGui::TextDisabled("Scanning RAW folder...");
        } else if (m_RawWorkspace.sources.empty()) {
            ImGui::TextWrapped("No RAW images in this folder.");
            if (ImGui::Button("Choose another folder")) OpenRawWorkspaceFolderDialog();
        } else {
            ImGui::TextDisabled("Hover an image to preview it.");
        }
        ImGui::PopStyleVar();
    };
    if (!grid) {
        ImGui::BeginChild("Image", ImVec2(imageWidth, 0.0f), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (current.ready()) drawPreview(ImGui::GetContentRegionAvail());
        else {
            ImGui::SetCursorPos(ImVec2(20.0f * unit, 20.0f * unit));
            drawEmpty();
        }
        ImGui::EndChild();
    }
    if (detailsWidth > .5f) {
        ImGui::SameLine(0.0f, gridGutter * propertiesAmount);
        ImGui::BeginChild("PropertiesClip", ImVec2(detailsWidth, 0), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        Stack::Workspace::InputScope propertiesInput(!showProperties, false);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("Properties", ImVec2(fullDetailsWidth, 0.0f), ImGuiChildFlags_None,
            ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        std::function<void(const ImVec2&)> comparison;
        if (!preview.projectPath.empty() && current.ready()) comparison = [&](const ImVec2& available) {
            const auto original = resolve(displayedSource,{});
            const auto origin = ImGui::GetCursorScreenPos();
            const float slot = std::max(1.f,(available.y-12.f)*.5f);
            const auto drawComparison = [&](const Thumbnail& photo, const char* label, const char* unavailable, float y) {
                auto* draw = ImGui::GetWindowDrawList();
                draw->AddText({origin.x+12.f, y},ImGui::GetColorU32(ImGuiCol_TextDisabled),label);
                const float labelHeight = ImGui::GetFontSize()+4.f;
                const float room = std::max(1.f,slot-labelHeight);
                if (photo.ready()) {
                    const float scale = std::min(std::max(1.f,available.x-24.f)/photo.width,room/photo.height);
                    const ImVec2 minimum(origin.x+(available.x-photo.width*scale)*.5f,y+labelHeight+(room-photo.height*scale)*.5f);
                    draw->AddImageRounded((ImTextureID)(intptr_t)photo.texture,minimum,
                        {minimum.x+photo.width*scale,minimum.y+photo.height*scale},{0,1},{1,0},IM_COL32_WHITE,4.f);
                } else draw->AddText({origin.x+12.f,y+labelHeight},ImGui::GetColorU32(ImGuiCol_TextDisabled),
                    unavailable);
            };
            drawComparison(current.edited ? current : Thumbnail{},"Edited","Edited preview unavailable",origin.y);
            drawComparison(original,"Original",displayedSource ? "Loading original..." : "Original unavailable",origin.y+slot+8.f);
            ImGui::Dummy(available);
        };
        if (current.ready())
            RenderRawGalleryProperties(displayedSource, current.metadata, preview.projectPath,
                [&](const ImVec2& available) { drawPreview(available); },comparison);
        else RenderRawGalleryProperties(displayedSource, current.metadata,
            current.ready() ? preview.projectPath : requestedProject, {});
        Stack::Editor::ApplyWindowDrawOpacity(ImGui::GetCurrentWindow(), propertiesAmount);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::EndChild();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

bool EditorModule::HasUiPresentationMotion(int frame) const {
    return m_RawGalleryFolderCards.Active(frame) || (m_RawGalleryPresentationFrame==frame &&
        (m_RawGalleryPropertiesMotion.Active() || m_RawWorkspaceGalleryLayoutAnimation.Active() ||
         m_RawGallerySelectionMotion[0].Active() || m_RawGalleryGroupsMoving ||
         std::abs(m_RawWorkspaceLabGalleryScrollTargetY-m_RawWorkspaceLabGalleryScrollCurrentY)>.05f)) ||
        (m_RawWorkspaceGalleryGridStacks.filmstripScroll.frame==frame &&
         m_RawWorkspaceGalleryGridStacks.filmstripScroll.glide.Active());
}
