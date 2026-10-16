#pragma once

#include "Raw/RawImageData.h"

#include <memory>
#include <string>

namespace Raw {
// Header-only source facts for lazy development. The bounded cache shares
// immutable metadata by path and observed file revision, never project state.
// An unreadable file returns no lease. Probe errors remain in the metadata.
std::shared_ptr<const RawMetadata> AcquireSourceMetadata(const std::string& path);
}
