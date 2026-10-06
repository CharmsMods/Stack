#pragma once

#include "Renderer/GLLoader.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace Raw {

enum class RawGpuImageFamily : std::uint8_t {
    Presentation = 0,
    AuxiliaryOverlay,
    Count
};

struct RawGpuResidencySnapshot {
    std::uint64_t totalBytes = 0;
    std::array<std::uint64_t,
        static_cast<std::size_t>(RawGpuImageFamily::Count)> familyBytes {};
};

// Immutable, reference-counted ownership for GL images crossing the RAW
// worker/UI context boundary. The GL name is deleted exactly once when the
// last lease is released; callers that deliberately transfer the name into a
// different owner must use ReleaseTextureName() while the lease is unique.
class RawGpuImageLease {
public:
    RawGpuImageLease() = default;

    static RawGpuImageLease AdoptOwned(
        unsigned int texture,
        int width,
        int height,
        RawGpuImageFamily family,
        std::uint32_t bytesPerPixel = 8u) {
        RawGpuImageLease lease;
        if (texture == 0 || width <= 0 || height <= 0) {
            return lease;
        }
        const std::uint64_t bytes =
            static_cast<std::uint64_t>(width) *
            static_cast<std::uint64_t>(height) *
            static_cast<std::uint64_t>(bytesPerPixel);
        lease.m_State = std::make_shared<State>(
            texture, width, height, family, bytes);
        return lease;
    }

    unsigned int Texture() const {
        return m_State ? m_State->texture : 0u;
    }
    int Width() const { return m_State ? m_State->width : 0; }
    int Height() const { return m_State ? m_State->height : 0; }
    std::uint64_t Bytes() const { return m_State ? m_State->bytes : 0u; }
    RawGpuImageFamily Family() const {
        return m_State
            ? m_State->family
            : RawGpuImageFamily::Presentation;
    }
    long UseCount() const { return m_State.use_count(); }
    explicit operator bool() const { return Texture() != 0; }

    void Reset() { m_State.reset(); }

    unsigned int ReleaseTextureName() {
        if (!m_State || !m_State.unique()) {
            return 0;
        }
        const unsigned int texture = m_State->texture;
        m_State->texture = 0;
        m_State->Unaccount();
        m_State.reset();
        return texture;
    }

    static RawGpuResidencySnapshot Residency() {
        RawGpuResidencySnapshot snapshot;
        snapshot.totalBytes = s_TotalBytes.load(std::memory_order_relaxed);
        for (std::size_t index = 0; index < snapshot.familyBytes.size(); ++index) {
            snapshot.familyBytes[index] =
                s_FamilyBytes[index].load(std::memory_order_relaxed);
        }
        return snapshot;
    }

private:
    struct State {
        State(
            unsigned int textureValue,
            int widthValue,
            int heightValue,
            RawGpuImageFamily familyValue,
            std::uint64_t bytesValue)
            : texture(textureValue),
              width(widthValue),
              height(heightValue),
              family(familyValue),
              bytes(bytesValue),
              accounted(true) {
            s_TotalBytes.fetch_add(bytes, std::memory_order_relaxed);
            s_FamilyBytes[static_cast<std::size_t>(family)].fetch_add(
                bytes, std::memory_order_relaxed);
        }

        ~State() {
            if (texture != 0) {
                glDeleteTextures(1, &texture);
                texture = 0;
            }
            Unaccount();
        }

        void Unaccount() {
            if (!accounted) return;
            accounted = false;
            s_TotalBytes.fetch_sub(bytes, std::memory_order_relaxed);
            s_FamilyBytes[static_cast<std::size_t>(family)].fetch_sub(
                bytes, std::memory_order_relaxed);
        }

        unsigned int texture = 0;
        int width = 0;
        int height = 0;
        RawGpuImageFamily family = RawGpuImageFamily::Presentation;
        std::uint64_t bytes = 0;
        bool accounted = false;
    };

    inline static std::atomic<std::uint64_t> s_TotalBytes { 0u };
    inline static std::array<std::atomic<std::uint64_t>,
        static_cast<std::size_t>(RawGpuImageFamily::Count)> s_FamilyBytes {};
    std::shared_ptr<State> m_State;
};

} // namespace Raw
