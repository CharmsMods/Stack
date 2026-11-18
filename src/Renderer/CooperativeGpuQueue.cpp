#include "Renderer/CooperativeGpuQueue.h"

namespace Stack::Renderer {

CooperativeGpuQueue::~CooperativeGpuQueue() {
    // Deleting a sync does not discard GPU work. The final worker completion
    // fence covers remaining commands even on cancellation or an exception.
    for (auto fence : m_Batches) if (fence) glDeleteSync(fence);
}

bool CooperativeGpuQueue::Checkpoint(const std::function<bool()>& shouldCancel) {
    if (shouldCancel && shouldCancel()) return false;
    const int next = m_Batches[0] ? 1 : 0;
    m_Batches[next] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
    if (!m_Batches[next]) {
        // A failed fence allocation must not remove the submission bound.
        glFinish();
        if (m_Batches[0]) glDeleteSync(m_Batches[0]);
        m_Batches[0] = nullptr;
        return true;
    }
    // Submit the next batch before waiting on the older one so GPU execution
    // can continue while the worker wakes. Both syncs remain owned on errors.
    glFlush();
    if (next == 1) {
        GLenum status = glClientWaitSync(m_Batches[0], 0, 0);
        while (status == GL_TIMEOUT_EXPIRED) {
            if (shouldCancel && shouldCancel()) return false;
            status = glClientWaitSync(m_Batches[0], 0, 1000000u);
        }
        if (status == GL_WAIT_FAILED) {
            // Preserve the submission bound on a driver synchronization error.
            // This recovery is exclusively on the worker's owning GL context.
            glFinish();
        }
        glDeleteSync(m_Batches[0]);
        m_Batches[0] = m_Batches[1];
        m_Batches[1] = nullptr;
    }
    if (shouldCancel && shouldCancel()) return false;
    return true;
}

} // namespace Stack::Renderer
