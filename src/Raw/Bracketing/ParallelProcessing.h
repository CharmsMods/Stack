#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <thread>
#include <vector>

namespace Raw::Bracketing {
namespace ParallelProcessingDetail {
class JoinWorkers {
public:
    explicit JoinWorkers(std::vector<std::thread>& workers) : workers_(workers) {}
    ~JoinWorkers() { Join(); }

    void Join() {
        for(auto& worker:workers_) if(worker.joinable()) worker.join();
    }

private:
    std::vector<std::thread>& workers_;
};
}

// Tasks own disjoint output indices and return false on a reported failure.
// Callbacks may run concurrently. Every started worker is joined before a
// failure/cancellation returns or an exception reaches the requesting thread.
template<class Task>
bool ProcessParallelIndices(std::size_t count,std::size_t workerCount,
    const std::function<bool()>& shouldCancel,Task&& task) {
    if(!count) return true;
    workerCount=std::max<std::size_t>(1,std::min(count,workerCount));
    std::atomic<std::size_t> next{0};
    std::atomic<bool> stopped{false};
    std::atomic_flag exceptionRecorded=ATOMIC_FLAG_INIT;
    std::exception_ptr exception;
    const auto recordException=[&]() noexcept {
        // Only the winning worker writes the pointer. It is read only after
        // joining, without allocating an error string on a failing worker.
        if(!exceptionRecorded.test_and_set(std::memory_order_relaxed))
            exception=std::current_exception();
        stopped.store(true,std::memory_order_relaxed);
    };
    const auto run=[&]() noexcept {
        try {
            while(!stopped.load(std::memory_order_relaxed)) {
                if(shouldCancel&&shouldCancel()) {
                    stopped.store(true,std::memory_order_relaxed);break;
                }
                const auto index=next.fetch_add(1,std::memory_order_relaxed);
                if(index>=count) break;
                if(!task(index)) {
                    stopped.store(true,std::memory_order_relaxed);break;
                }
            }
        } catch(...) {recordException();}
    };
    std::vector<std::thread> workers;
    ParallelProcessingDetail::JoinWorkers joiner(workers);
    workers.reserve(workerCount-1);
    try {
        for(std::size_t i=1;i<workerCount&&!stopped.load(std::memory_order_relaxed);++i)
            workers.emplace_back(run);
    } catch(...) {recordException();}
    run();
    joiner.Join();
    if(exception) std::rethrow_exception(exception);
    return !stopped.load(std::memory_order_relaxed)&&!(shouldCancel&&shouldCancel());
}
} // namespace Raw::Bracketing
