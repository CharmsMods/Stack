#pragma once

namespace Stack::Renderer {
// Per-pipeline scalar diagnostics only. The owner invalidates on content
// mutation, including a new render/adoption reusing the same GL texture name.
template<class Stats> class OutputTextureStatsCache {
public:
    void Invalidate() noexcept { m_Ready=false; }
    template<class Read> Stats Get(unsigned int texture,int width,int height,Read read) {
        if (!texture || width<=0 || height<=0) return {};
        if (m_Ready && texture==m_Texture && width==m_Width && height==m_Height) return m_Stats;
        m_Ready=false;
        bool completed=false;
        Stats stats=read(completed);
        // A completed transparent image has no valid samples but is stable.
        // Allocation/GL/readback failures remain retryable on the next query.
        if (completed) {
            m_Stats=stats; m_Texture=texture; m_Width=width; m_Height=height; m_Ready=true;
        }
        return stats;
    }
private:
    bool m_Ready=false;
    unsigned int m_Texture=0;
    int m_Width=0,m_Height=0;
    Stats m_Stats{};
};
} // namespace Stack::Renderer
