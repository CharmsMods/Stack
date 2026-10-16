#include "Queue/QueueModule.h"

namespace Stack::Queue {
void QueueModule::RenderSectionPanel() {
    const auto& items = FrameItems();
    Execute(m_View.RenderNavigator(items));
}
} // namespace Stack::Queue
