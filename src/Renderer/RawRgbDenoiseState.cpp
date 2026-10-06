#include "Renderer/RawRgbDenoiseState.h"

#include <chrono>

RawRgbDenoiseState::~RawRgbDenoiseState() {
    CancelAndWait();
}

bool RawRgbDenoiseState::IsCompletionReady() const {
    return pending && future.valid() &&
        future.wait_for(std::chrono::milliseconds(0)) ==
            std::future_status::ready;
}

void RawRgbDenoiseState::RequestCancellation() noexcept {
    if (cancel) {
        cancel->store(true, std::memory_order_relaxed);
    }
    deferred = false;
}

void RawRgbDenoiseState::CancelAndWait() noexcept {
    RequestCancellation();
    if (future.valid()) {
        try {
            future.wait();
            (void)future.get();
        } catch (...) {
            // Provider errors cannot escape owner teardown.
        }
    }
    pending = false;
    modelFingerprint = 0;
    applicationFingerprint = 0;
    cancel.reset();
}
