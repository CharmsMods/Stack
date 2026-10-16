#pragma once
#include <windows.h>
#include <shobjidl.h>
#include <string>

namespace Stack::BurstShell {
inline constexpr DWORD MaximumSelectionFiles = 100000;
inline constexpr size_t MaximumSelectionBytes = 16u * 1024u * 1024u;

std::wstring ModulePath(HMODULE module);
// Called only after explicit invocation, off Explorer's menu/UI thread. Writes
// the selected paths verbatim; validation and grouping belong to StackBurst.
HRESULT LaunchSelection(HMODULE module, IShellItemArray* selection) noexcept;
}
