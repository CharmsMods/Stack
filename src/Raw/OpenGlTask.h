#pragma once

#include <functional>
#include <string>

namespace Raw {

// Processing code never owns a window-system context. Hosts provide this
// blocking handoff so compute work executes on the existing shared OpenGL
// worker context and remains safe beside interactive rendering.
using OpenGlTask = std::function<bool(std::string&)>;
using OpenGlTaskExecutor = std::function<bool(OpenGlTask, std::string&)>;

} // namespace Raw
