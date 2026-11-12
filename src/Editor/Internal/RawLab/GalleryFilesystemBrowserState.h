#pragma once
#include "GalleryFilesystemBrowser.h"
#include "Animation/FrameTween.h"
#include "Search/FolderIndex.h"
#include "GalleryBrowserMotion.h"
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <unordered_map>

namespace Stack::Editor::RawLabInternal {
// UI-owned model and worker mailbox. Workers publish immutable listings; only
// the UI thread changes expansion, history, selection or animation state.
struct GalleryFilesystemBrowser::State {
    enum class Kind { Folder, Desktop, Downloads, Pictures, Drive, Removable, Network, Optical, Image };
    struct Node {
        std::filesystem::path path;
        std::wstring key, sortKey;
        std::string label, pathLabel, error;
        std::vector<std::shared_ptr<Node>> children;
        std::size_t folderChildren=0;
        Kind kind=Kind::Folder;
        unsigned branch=0;
        bool expanded=false, loaded=false, pending=false, matches=true;
        float reveal=0, hover=0;
        Animation::FrameTween disclosureMotion, hoverMotion;
        unsigned texture=0;
        int previewWidth=0, previewHeight=0, previewFrame=-1;
        bool previewRequested=false;
        bool drive=false,capacityKnown=false,systemDrive=false;
        float gridScroll=0;
        float usedFraction=0;
        std::string capacityLabel;
    };
    struct Result { std::shared_ptr<Node> node; std::vector<std::shared_ptr<Node>> children; std::string error; };
    struct Row { std::shared_ptr<Node> node; int depth; bool message=false; };
    std::vector<std::shared_ptr<Node>> roots;
    std::vector<std::shared_ptr<Node>> drives, shortcuts;
    char search[256]{};
    std::wstring searchKey;
    std::vector<std::shared_ptr<Node>> searchResults;
    std::shared_ptr<Search::FolderIndex> searchIndex;
    std::uint64_t searchRequest=0;
    bool searchPending=false,searchScheduled=false,searchLimited=false,searchRefreshing=false;
    std::string searchStatus;
    double searchAt=0;
    std::unordered_map<std::wstring, std::shared_ptr<Node>> listings;
    std::shared_ptr<Node> directory, selection;
    bool resetGridScroll = false;
    std::string addressError;
    std::vector<Row> rows;
    std::mutex mutex;
    std::vector<Result> results;
    struct Preview { std::shared_ptr<Node> node; std::vector<unsigned char> pixels; int width=0,height=0; };
    std::vector<Preview> previews;
    std::atomic<bool> previewReady{false};
    std::vector<std::shared_ptr<Node>> previewNodes;
    int previewPending=0;
    bool publishNavigation=false, closeRequested=false;
    std::optional<std::filesystem::path> openImage;
    float tileSize=156.f;
    GalleryBrowserMotion motion;
    ImRect gridBounds{},navigationTile{};
    std::wstring returnFolder;
    bool locateReturn=false;
    std::wstring publishedKey;
    std::atomic<bool> ready{false};
    std::atomic<bool> closed{false};
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
    void Merge(std::vector<std::shared_ptr<Node>>& existing,std::vector<std::shared_ptr<Node>> incoming);
    void Request(const std::shared_ptr<State>& self,const std::shared_ptr<Node>& node);
    void Start(const std::shared_ptr<State>& self);
    void Reveal(const std::shared_ptr<State>& self);
    void Pump(const std::shared_ptr<State>& self);
    void Navigate(const std::filesystem::path& path);
    void FilterChanged();
    void Collapse();
    void RequestPreview(const std::shared_ptr<State>& self,const std::shared_ptr<Node>& node);
    void PumpPreviews();
    void ReleasePreviews();
    void SearchChanged();
    void PumpSearch(const std::shared_ptr<State>& self);
};
}
