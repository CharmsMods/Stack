#pragma once

#include <memory>
#include <utility>
#include <vector>

namespace EditorNodeGraph {
// Runtime-only ownership of a published byte value. Uncertified callers keep
// the conservative content-comparison path. Every publisher must invalidate
// before changing bytes, or seed an owner proven equal to the published value.
class ImagePayloadByteStorage {
public:
    using Owner = std::shared_ptr<const std::vector<unsigned char>>;
    void Invalidate() noexcept { m_Owner.reset(); m_Certified = true; }
    void Seed(Owner owner) noexcept { m_Owner = std::move(owner); m_Certified = true; }
    // Adoption can receive a modified copy, or a restored snapshot already
    // seeded with its exact owner. Prove equality before retaining that owner.
    void Revalidate(const std::vector<unsigned char>& bytes) {
        if (m_Owner && *m_Owner != bytes) m_Owner.reset();
        m_Certified = true;
    }

    template<class Retain>
    Owner Get(const std::vector<unsigned char>& bytes, Retain&& retain) const {
        if (!m_Certified) return retain();
        if (bytes.empty()) return {};
        if (!m_Owner) m_Owner = retain();
        return m_Owner;
    }
private:
    bool m_Certified = false;
    mutable Owner m_Owner;
};
}
