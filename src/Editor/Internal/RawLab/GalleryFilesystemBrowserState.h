#pragma once
#include "GalleryFilesystemBrowser.h"
#include "Animation/FrameTween.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace Stack::Editor::RawLabInternal {
// UI-owned model and worker mailbox. Workers publish immutable listings; only
// the UI thread changes expansion, history, selection or animation state.
struct GalleryFilesystemBrowser::State {
    enum class Kind { Folder, Desktop, Pictures, Drive, Removable, Network };
    struct Node {
        std::filesystem::path path;
        std::wstring key, sortKey;
        std::string label, error;
        std::vector<std::shared_ptr<Node>> children;
        Kind kind=Kind::Folder;
        unsigned branch=0;
        bool expanded=false, loaded=false, pending=false, matches=true;
        float reveal=0, hover=0;
        Animation::FrameTween disclosureMotion, hoverMotion;
    };
    struct Result { std::shared_ptr<Node> node; std::vector<std::shared_ptr<Node>> children; std::string error; };
    struct Row { std::shared_ptr<Node> node; int depth; bool message=false; };
    std::vector<std::shared_ptr<Node>> roots;
    std::vector<Row> rows;
    std::mutex mutex;
    std::vector<Result> results;
    std::atomic<bool> ready{false};
    bool dirty=true, started=false, rootsPending=false;
    bool revealRequested=false, scrollToCurrent=false, colors=true, showFilter=false;
    std::string locationsError;
    char address[2048]{}, filter[256]{};
    std::wstring currentKey, filterKey;
    std::filesystem::path current;
    std::vector<std::filesystem::path> ancestors, history;
    std::vector<std::string> ancestorLabels;
    std::size_t historyIndex=0;
    std::optional<std::size_t> requestedHistory;
    std::shared_ptr<Node> scrollAnchor;
    float anchorOffset=0;
    int visibleFirst=0, visibleLast=-1;
    bool anchorMessage=false;
    std::optional<float> restoreRow;
    static std::wstring Key(const std::filesystem::path& path);
    static std::shared_ptr<Node> Make(const std::filesystem::path& path,const std::string& name={},Kind kind=Kind::Folder);
    static void Merge(std::vector<std::shared_ptr<Node>>& existing,std::vector<std::shared_ptr<Node>> incoming);
    void Request(const std::shared_ptr<State>& self,const std::shared_ptr<Node>& node);
    void Start(const std::shared_ptr<State>& self);
    void Reveal(const std::shared_ptr<State>& self);
    void Pump(const std::shared_ptr<State>& self);
    void Navigate(const std::filesystem::path& path);
    void FilterChanged();
    void Collapse();
};
}
