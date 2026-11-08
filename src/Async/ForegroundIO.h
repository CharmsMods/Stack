#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <thread>
#include "UiInteraction.h"

namespace Async::ForegroundIO {
inline std::atomic<long long> demandUntil{0};
inline std::atomic<unsigned> readers{0}, tiles{0};
inline long long Now() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
inline void Demand() {demandUntil.store(Now()+350,std::memory_order_relaxed);}
class Read {
public:
    Read(){readers.fetch_add(1,std::memory_order_relaxed);}
    ~Read(){readers.fetch_sub(1,std::memory_order_relaxed);}
    Read(const Read&)=delete;Read& operator=(const Read&)=delete;
};
class TileRead {
public:
    TileRead(){tiles.fetch_add(1,std::memory_order_relaxed);}
    ~TileRead(){tiles.fetch_sub(1,std::memory_order_relaxed);}
    TileRead(const TileRead&)=delete;TileRead& operator=(const TileRead&)=delete;
};
inline bool WaitForOpen(const std::function<bool()>& canceled) {
    while(readers.load(std::memory_order_relaxed)) {
        if(canceled&&canceled())return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return !(canceled&&canceled());
}
// Only background discovery uses this cooperative boundary. Dedicated tile and
// foreground workers remain available while a discovery worker is parked.
inline bool WaitForIdle(const std::function<bool()>& canceled) {
    while(UiInteraction::Active()||readers.load(std::memory_order_relaxed)||tiles.load(std::memory_order_relaxed)||Now()<demandUntil.load(std::memory_order_relaxed)) {
        if(canceled&&canceled())return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    return !(canceled&&canceled());
}
}
