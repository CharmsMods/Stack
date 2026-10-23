#pragma once
#include <string>
#include <vector>

namespace Raw::Bracketing {
struct ProcessingRequest;
struct PreparedDataset;
namespace LearnedAlignment {
inline constexpr const char* ModelSha256="cb8cfbf14c5e0f6734b64add383708b7ff68cc6089a0007c67165d4761346102";
// A pinned model and preprocessing contract are saved in analysis identity.
inline constexpr const char* Profile="sea-raft-m-cb8cfbf14c5e-4iters-fp32-rgb-area-p95-v1";
bool Generate(ProcessingRequest&,PreparedDataset&,std::vector<std::string>& diagnostics,std::string& error);
}
}
