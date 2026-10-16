#pragma once

#include <functional>
#include <string>

namespace Raw {

// Interactive renders and explicit user processing take precedence over
// maintenance compute at the next safe shared-context task boundary.
enum class OpenGlTaskPriority { Foreground, Background };

// Processing code never owns a window-system context. Hosts provide this
// blocking handoff so compute work executes on the existing shared OpenGL
// worker context and remains safe beside interactive rendering.
using OpenGlTask = std::function<bool(std::string&)>;
using OpenGlTaskExecutor = std::function<bool(OpenGlTask, std::string&)>;

} // namespace Raw
