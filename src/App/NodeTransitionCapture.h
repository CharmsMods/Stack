#pragma once
#include <string>
struct ImDrawData;
class EditorModule;
namespace Stack::UiDiagnostics {
// Opt-in, bounded diagnostic; normal application frames do no readback or IO.
void ArmNodeOpenCapture(const std::string& document, const std::string& node);
void CaptureNodeOpenIfDue(EditorModule& editor, const ImDrawData& data);
}
