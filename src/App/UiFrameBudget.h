#pragma once
#include <algorithm>

namespace Stack::App {
// Background completions should leave most of a high-refresh frame for input,
// drawing and presentation. Image processing quality is unrelated to this limit.
inline double MainThreadCompletionBudgetMs(int displayRefreshRate) {
    const int rate = displayRefreshRate > 0 ? displayRefreshRate : 60;
    return std::min(1.0, 100.0 / static_cast<double>(rate));
}
}
