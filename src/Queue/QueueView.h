#pragma once

#include "Queue/RenderQueueModel.h"
#include "imgui.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace StackAppearance { struct SurfaceColors; }

namespace Stack::Queue {

enum class ViewCommand { None, OpenGallery, ChooseDestination, Export, StopAfterCurrent,
    SelectAll, SelectNone, ClearCompleted, Include, Remove, RevealOutput, RevealDestination };

struct ViewAction {
    ViewCommand command = ViewCommand::None;
    std::uint64_t id = 0;
    bool included = false;
};

struct ExportPresentation {
    std::string destination;
    std::string status;
    bool busy = false;
    bool stopping = false;
    bool available = false;
    const StackAppearance::SurfaceColors* primaryColors = nullptr;
};

// Prepared presentation only. The Queue model owns jobs; the module executes
// returned intents. No disk, decoding, model mutation or editor access here.
class QueueView {
public:
    using Thumbnail = std::function<void(const Item&, ImVec2, ImVec2)>;
    void Prepare(const std::vector<Item>& items, std::uint64_t revision);
    ViewAction RenderNavigator(const std::vector<Item>& items);
    ViewAction RenderWorkspace(const std::vector<Item>& items,
        const ExportPresentation& output, const Thumbnail& thumbnail);
    void Reset();

private:
    struct Row {
        std::uint64_t id = 0;
        std::string name, kind, location, output;
        std::filesystem::path outputPath;
    };
    void PrepareFilter(const std::vector<Item>& items);
    std::vector<Row> m_Rows;
    std::vector<std::size_t> m_Visible;
    std::array<std::size_t, 5> m_Counts{};
    std::array<std::string, 5> m_CountLabels;
    std::string m_Summary, m_ExportLabel;
    std::uint64_t m_Revision = 0;
    std::uint64_t m_DetailItem = 0;
    std::size_t m_DetailIndex = 0;
    std::size_t m_Ready = 0;
    int m_Filter = 0, m_PreparedFilter = -1;
};

} // namespace Stack::Queue
