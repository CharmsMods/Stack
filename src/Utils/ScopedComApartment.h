#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <objbase.h>

namespace Stack::Win32 {

class ScopedComApartment {
public:
    explicit ScopedComApartment(
        DWORD model = COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE) noexcept
        : m_Result(CoInitializeEx(nullptr, model)) {}

    ~ScopedComApartment() {
        // S_FALSE also acquires a COM initialization reference. A failed
        // changed-mode request does not, and must not uninitialize its caller.
        if (SUCCEEDED(m_Result)) CoUninitialize();
    }

    ScopedComApartment(const ScopedComApartment&) = delete;
    ScopedComApartment& operator=(const ScopedComApartment&) = delete;

    explicit operator bool() const noexcept { return SUCCEEDED(m_Result); }
    HRESULT Result() const noexcept { return m_Result; }

private:
    HRESULT m_Result;
};

} // namespace Stack::Win32
#endif
