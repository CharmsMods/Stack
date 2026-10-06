#pragma once

#include <memory>

namespace Async {

// Own this alongside the UI state that receives an asynchronous completion.
// Begin, Cancel, destruction, and completion callbacks run on the main thread.
// Workers may check tickets to skip work, but must never use them as a lock
// that keeps the owning UI object alive on another thread.
class MainThreadRequestScope {
public:
    using Ticket = std::weak_ptr<const bool>;

    MainThreadRequestScope() = default;
    MainThreadRequestScope(const MainThreadRequestScope&) = delete;
    MainThreadRequestScope& operator=(const MainThreadRequestScope&) = delete;

    Ticket Begin() {
        m_Current = std::make_shared<const bool>(true);
        return m_Current;
    }

    void Cancel() noexcept { m_Current.reset(); }

private:
    std::shared_ptr<const bool> m_Current;
};

} // namespace Async
