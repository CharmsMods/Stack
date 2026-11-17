#include "Renderer/ViewportSectionStore.h"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_WIN32)
#include <windows.h>
#else
#include <cstdio>
#include <sys/statvfs.h>
#include <unistd.h>
#endif
#include <algorithm>
#include <atomic>
#include <limits>
#include <mutex>

namespace Stack::Renderer {
namespace {
// Shared across projects and both views. Charge the allocated vector capacity,
// and keep that charge until the last upload lease releases its pixels.
std::atomic<std::uint64_t> retainedBytes{0};
constexpr std::uint64_t RamLimit = 256ull * 1024 * 1024;
bool Reserve(std::uint64_t bytes) {
    auto used = retainedBytes.load(std::memory_order_relaxed);
    while (bytes <= RamLimit && used <= RamLimit - bytes)
        if (retainedBytes.compare_exchange_weak(used, used + bytes, std::memory_order_relaxed)) return true;
    return false;
}
std::uint64_t CapacityBytes(const ViewportSectionStore::Pixels& payload) {
    constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
    const auto a = std::uint64_t(payload.pixels.capacity()), b = std::uint64_t(payload.hdr.capacity());
    if (a > maximum / sizeof(float) || b > maximum / sizeof(float) - a) return maximum;
    return (a + b) * sizeof(float);
}
}

struct ViewportSectionStore::Impl {
    struct Entry {
        PixelLease payload;
        std::uint64_t offset = 0;
        bool onDisk = false, hasHdr = false;
    };
    ViewportSectionIndex index;
    std::vector<Entry> entries;
    // Only payload writers and disk readers acquire this. UI metadata queries
    // use the independently published index, never a lock held across I/O.
    mutable std::mutex ioMutex;
    std::atomic<std::uint64_t> diskBytes{0};
    std::uint64_t ramBytes = 0, ramAllowance = RamLimit;
#if defined(_WIN32)
    HANDLE file = INVALID_HANDLE_VALUE;
    std::wstring directory;
#else
    // CPU-only tools can exercise the real spill path without a GL context.
    std::FILE* file = nullptr;
#endif
    Impl(int width, int height) : index(width, height), entries(index.Count()) {}
    ~Impl() {
#if defined(_WIN32)
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
#else
        if (file) std::fclose(file);
#endif
    }
    bool Open(std::string& error) {
#if defined(_WIN32)
        if (file != INVALID_HANDLE_VALUE) return true;
        wchar_t tempDirectory[MAX_PATH + 1]{}, path[MAX_PATH + 1]{};
        const auto length = GetTempPathW(MAX_PATH, tempDirectory);
        if (!length || length >= MAX_PATH || !GetTempFileNameW(tempDirectory, L"stk", 0, path)) {
            error = "Cannot create temporary native-image cache."; return false;
        }
        directory = tempDirectory;
        file = CreateFileW(path, GENERIC_READ | GENERIC_WRITE | DELETE, 0, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            DeleteFileW(path); error = "Cannot open temporary native-image cache."; return false;
        }
#else
        if (file) return true;
        file = std::tmpfile();
        if (!file) { error = "Cannot create temporary native-image cache."; return false; }
#endif
        return true;
    }
    bool HasSpace(std::uint64_t bytes) const {
#if defined(_WIN32)
        ULARGE_INTEGER available{};
        return GetDiskFreeSpaceExW(directory.c_str(), &available, nullptr, nullptr) &&
            available.QuadPart >= bytes + RamLimit;
#else
        struct statvfs info{};
        if (fstatvfs(fileno(file), &info) != 0) return false;
        return static_cast<long double>(info.f_bavail) * info.f_frsize >= bytes + RamLimit;
#endif
    }
    bool Transfer(bool write, std::uint64_t offset, void* data, std::uint64_t bytes, std::string& error) const {
#if defined(_WIN32)
        LARGE_INTEGER position; position.QuadPart = static_cast<LONGLONG>(offset);
        if (!SetFilePointerEx(file, position, nullptr, FILE_BEGIN)) {
#else
        if (offset > std::uint64_t(std::numeric_limits<off_t>::max()) ||
            fseeko(file, static_cast<off_t>(offset), SEEK_SET) != 0) {
#endif
            error = "Cannot seek native-image cache."; return false;
        }
        auto* cursor = static_cast<unsigned char*>(data);
        while (bytes) {
            const auto count = std::min<std::uint64_t>(bytes, 16u * 1024u * 1024u);
#if defined(_WIN32)
            DWORD done = 0;
            const bool ok = write ? WriteFile(file, cursor, DWORD(count), &done, nullptr) :
                ReadFile(file, cursor, DWORD(count), &done, nullptr);
#else
            const auto done = write ? std::fwrite(cursor, 1, std::size_t(count), file) :
                std::fread(cursor, 1, std::size_t(count), file);
            const bool ok = std::ferror(file) == 0;
#endif
            if (!ok || done != count) {
                error = write ? "Cannot write native-image cache (check free disk space)." :
                    "Cannot read native-image cache.";
                return false;
            }
            cursor += done; bytes -= done;
        }
#if !defined(_WIN32)
        // Publish only after buffered write errors can be observed. No fsync:
        // this is an ephemeral cache, not a durable project save.
        if (write && std::fflush(file) != 0) {
            error = "Cannot write native-image cache (check free disk space)."; return false;
        }
#endif
        return true;
    }
};

ViewportSectionStore::Pixels::~Pixels() {
    // Free the backing allocation before making its budget available again.
    std::vector<float>().swap(pixels);
    std::vector<float>().swap(hdr);
    if (retainedBytes) Stack::Renderer::retainedBytes.fetch_sub(retainedBytes, std::memory_order_relaxed);
}
ViewportSectionStore::ViewportSectionStore(int width, int height, std::uint64_t ramAllowance)
    : m(std::make_unique<Impl>(width, height)) {
    m->ramAllowance = std::min(ramAllowance, RamLimit);
}
ViewportSectionStore::~ViewportSectionStore() = default;
int ViewportSectionStore::Count() const { return m->index.Count(); }
ImageRegion ViewportSectionStore::Bounds(int index) const { return m->index.Bounds(index); }
ImageRegion ViewportSectionStore::Raster(int index) const {
    auto r = Bounds(index); if (!r.Valid()) return r;
    const int right = std::min(r.fullWidth, r.x + r.width + 4), bottom = std::min(r.fullHeight, r.y + r.height + 4);
    r.x = std::max(0, r.x - 4); r.y = std::max(0, r.y - 4);
    r.width = right - r.x; r.height = bottom - r.y; return r;
}
bool ViewportSectionStore::Has(int index) const { return m->index.Has(index); }
bool ViewportSectionStore::Complete() const { return m->index.Complete(); }
bool ViewportSectionStore::Covers(const ImageRegion& visible) const { return m->index.Covers(visible); }
int ViewportSectionStore::NextMissing(const ImageRegion& visible, bool whole) const {
    return m->index.NextMissing(visible, whole);
}
bool ViewportSectionStore::Store(int index, std::vector<float> pixels, std::vector<float> hdr, std::string& error) {
    const auto r = Raster(index); const std::size_t values = std::size_t(r.width) * r.height * 4;
    if (!r.Valid() || pixels.size() != values || (!hdr.empty() && hdr.size() != values)) {
        error = "Invalid native section payload."; return false;
    }
    std::lock_guard lock(m->ioMutex);
    if (Has(index)) return true;
    auto payload = std::make_shared<Pixels>();
    payload->pixels = std::move(pixels); payload->hdr = std::move(hdr);
    const auto capacity = CapacityBytes(*payload);
    const std::uint64_t bytes = (payload->pixels.size() + payload->hdr.size()) * sizeof(float);
    auto& entry = m->entries[index];
    if (capacity <= m->ramAllowance && m->ramBytes <= m->ramAllowance - capacity && Reserve(capacity)) {
        payload->retainedBytes = capacity;
        entry.payload = std::move(payload);
        m->ramBytes += capacity;
    } else {
        if (!m->Open(error)) return false;
        if (!m->HasSpace(bytes)) {
            error = "Insufficient free disk space for native image cache."; return false;
        }
        const auto offset = m->diskBytes.load(std::memory_order_relaxed);
        if (!m->Transfer(true, offset, payload->pixels.data(), payload->pixels.size() * sizeof(float), error) ||
            (!payload->hdr.empty() && !m->Transfer(true, offset + payload->pixels.size() * sizeof(float),
                payload->hdr.data(), payload->hdr.size() * sizeof(float), error))) return false;
        entry.offset = offset; entry.onDisk = true; entry.hasHdr = !payload->hdr.empty();
        m->diskBytes.store(offset + bytes, std::memory_order_release);
    }
    // Release/acquire publication makes the installed entry immutable and
    // visible to lock-free metadata readers and RAM borrowers.
    m->index.Publish(index);
    return true;
}
ViewportSectionStore::PixelLease ViewportSectionStore::LoadShared(int index, std::string& error) const {
    if (!Has(index)) { error = "Native section is not available."; return {}; }
    const auto& entry = m->entries[index];
    if (!entry.onDisk) return entry.payload;
    const auto r = Raster(index); const std::size_t values = std::size_t(r.width) * r.height * 4;
    auto payload = std::make_shared<Pixels>();
    payload->pixels.resize(values); payload->hdr.resize(entry.hasHdr ? values : 0);
    std::lock_guard lock(m->ioMutex);
    if (!m->Transfer(false, entry.offset, payload->pixels.data(), values * sizeof(float), error) ||
        (entry.hasHdr && !m->Transfer(false, entry.offset + values * sizeof(float),
            payload->hdr.data(), values * sizeof(float), error))) return {};
    return payload;
}
bool ViewportSectionStore::Load(int index, std::vector<float>& pixels, std::vector<float>& hdr, std::string& error) const {
    const auto payload = LoadShared(index, error);
    if (!payload) return false;
    // Preserve callers' previous pixels if allocation or a disk read fails.
    auto nextPixels = payload->pixels, nextHdr = payload->hdr;
    pixels.swap(nextPixels); hdr.swap(nextHdr); return true;
}
std::uint64_t ViewportSectionStore::DiskBytes() const { return m->diskBytes.load(std::memory_order_acquire); }
std::uint64_t ViewportSectionStore::RetainedRamBytes() { return retainedBytes.load(std::memory_order_relaxed); }
}
