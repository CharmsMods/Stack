#!/usr/bin/env python3
"""Generate a CPU regression from the production clipboard refresh method.

The OS clipboard, ImGui and bundle parser are stand-ins; this checks polling,
forced reads and session ownership, not Windows clipboard/JSON integration.
The production default argument and Gallery refresh call are executed too.
"""
import argparse
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source = (root / 'src/Editor/Internal/EditorModuleRawAttributeTransfer.cpp').read_text()
    start = source.index('void EditorModule::RefreshRawEditAttributeClipboardFromSystem(')
    end = source.index('\nvoid EditorModule::RequestPasteRawEditAttributesForGallerySelection', start)
    method = source[start:end]
    header = (root / 'src/Editor/EditorModule.h').read_text()
    declarations = re.findall(r'void RefreshRawEditAttributeClipboardFromSystem\([^;]+;', header)
    assert len(declarations) == 1
    actions = (root / 'src/Editor/Internal/RawWorkspace/EditorModuleRawGalleryActions.cpp').read_text()
    capture = actions.split('EditorModule::CaptureRawGalleryActionContext(', 1)[1].split('\nvoid EditorModule::', 1)[0]
    calls = re.findall(r'RefreshRawEditAttributeClipboardFromSystem\([^;]*;', capture)
    assert len(calls) == 1
    # Copy/session replacement must invalidate the passive observation. Keep
    # these production reset statements in an executable fixture operation.
    resets = re.findall(r'm_RawEditAttributeClipboardObservedText.clear\(\);\s*m_RawEditAttributeClipboardObservedSequence = 0;', source)
    assert len(resets) == 3
    preamble = r'''
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <iostream>
#include <stdexcept>
void Require(bool ok, const char* why) { if (!ok) throw std::runtime_error(why); }
namespace Async { bool IsBusy(bool busy) { return busy; } }
namespace PlatformHelpers {
    std::uint32_t sequence = 1;
    int probes = 0;
    std::uint32_t ClipboardSequenceNumber() { ++probes; return sequence; }
}
namespace ImGui {
    bool context = true, fail = false, changeDuringRead = false;
    int reads = 0;
    std::string text = "validA";
    void* GetCurrentContext() { return context ? &context : nullptr; }
    const char* GetClipboardText() {
        ++reads;
        if (changeDuringRead) ++PlatformHelpers::sequence;
        return fail ? nullptr : text.c_str();
    }
}
namespace nlohmann {
    struct json {
        std::string text;
        static json parse(const std::string& text, void*, bool) { return {text}; }
        bool is_discarded() const { return text.rfind("valid", 0) != 0; }
    };
}
namespace Stack::RawRecipe {
    using RawEditAttributeBundle = std::string;
    bool DeserializeRawEditAttributeBundle(const nlohmann::json& value, std::string& bundle, void*) {
        bundle = value.text; return true;
    }
}
struct EditorModule {
    bool m_RawEditAttributePastePopupRequested = false, m_RawEditAttributePasteTaskState = false;
    std::vector<int> m_RawEditAttributePasteTargets;
    std::string m_RawEditAttributeClipboardObservedText;
    std::optional<std::string> m_RawEditAttributeClipboard;
    std::uint32_t m_RawEditAttributeClipboardObservedSequence = 0;
'''
    fixture = r'''
int main() try {
    EditorModule editor;
    editor.CaptureControls(true);
    Require(ImGui::reads == 1 && *editor.m_RawEditAttributeClipboard == "validA", "initial passive read");
    for (int i = 0; i < 60; ++i) editor.CaptureControls(true);
    Require(ImGui::reads == 1, "unchanged Gallery frames reread clipboard");
    ImGui::text = "validB";
    editor.CaptureControls(false);
    Require(ImGui::reads == 2 && *editor.m_RawEditAttributeClipboard == "validB", "action capture must force read");
    ImGui::text = "validC";
    editor.RefreshRawEditAttributeClipboardFromSystem();
    Require(ImGui::reads == 3 && *editor.m_RawEditAttributeClipboard == "validC", "default paste call must force read");
    ++PlatformHelpers::sequence;
    ImGui::text = "validD";
    editor.CaptureControls(true);
    Require(ImGui::reads == 4 && *editor.m_RawEditAttributeClipboard == "validD", "new sequence ignored");
    PlatformHelpers::sequence = 0;
    editor.CaptureControls(true); editor.CaptureControls(true);
    Require(ImGui::reads == 6 && !editor.m_RawEditAttributeClipboardObservedSequence, "zero sequence cached");
    PlatformHelpers::sequence = 3;
    ImGui::changeDuringRead = true;
    editor.CaptureControls(true);
    Require(!editor.m_RawEditAttributeClipboardObservedSequence, "raced read cached");
    ImGui::changeDuringRead = false;
    editor.CaptureControls(true);
    Require(editor.m_RawEditAttributeClipboardObservedSequence == 4, "raced read not retried");
    ++PlatformHelpers::sequence;
    ImGui::fail = true;
    const int failedReads = ImGui::reads;
    editor.CaptureControls(true); editor.CaptureControls(true);
    Require(ImGui::reads == failedReads + 2 && !editor.m_RawEditAttributeClipboardObservedSequence, "failed read cached");
    ImGui::fail = false;
    editor.CaptureControls(true);
    Require(editor.m_RawEditAttributeClipboardObservedSequence == 5, "failed read not recovered");
    for (int owner = 0; owner < 3; ++owner) {
        editor.m_RawEditAttributePastePopupRequested = owner == 0;
        editor.m_RawEditAttributePasteTargets = owner == 1 ? std::vector<int>{1} : std::vector<int>{};
        editor.m_RawEditAttributePasteTaskState = owner == 2;
        const int reads = ImGui::reads, probes = PlatformHelpers::probes;
        ImGui::text = "validE";
        editor.RefreshRawEditAttributeClipboardFromSystem();
        Require(ImGui::reads == reads && PlatformHelpers::probes == probes &&
            *editor.m_RawEditAttributeClipboard == "validD", "paste session lost clipboard ownership");
    }
    editor.m_RawEditAttributePasteTaskState = false;
    editor.InvalidateObservation();
    editor.CaptureControls(true);
    Require(*editor.m_RawEditAttributeClipboard == "validE", "copy/session reset ignored");
    PlatformHelpers::sequence = UINT32_MAX;
    editor.CaptureControls(true);
    PlatformHelpers::sequence = 1;
    ImGui::text = "validF";
    editor.CaptureControls(true);
    Require(*editor.m_RawEditAttributeClipboard == "validF", "wrapped sequence ignored");
    ImGui::text = "validG"; // A full-wrap equality still must not suppress explicit paste.
    editor.RefreshRawEditAttributeClipboardFromSystem();
    Require(*editor.m_RawEditAttributeClipboard == "validG", "force suppressed by sequence equality");
    ++PlatformHelpers::sequence;
    ImGui::text = "ordinary clipboard text";
    editor.CaptureControls(true);
    Require(*editor.m_RawEditAttributeClipboard == "validG", "invalid clipboard erased retained bundle");
    ImGui::context = false;
    const int reads = ImGui::reads, probes = PlatformHelpers::probes;
    editor.RefreshRawEditAttributeClipboardFromSystem();
    Require(ImGui::reads == reads && PlatformHelpers::probes == probes, "clipboard queried without context");
    std::cout << "Clipboard passive refresh, forced actions, retries and ownership passed.\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
'''
    args.output.write_text(preamble + declarations[0] + '\n'
        + 'void CaptureControls(bool editControlsOnly) {' + calls[0] + '}\n'
        + 'void InvalidateObservation() {' + resets[0] + '}\n};\n'
        + method + fixture)


if __name__ == '__main__':
    main()
