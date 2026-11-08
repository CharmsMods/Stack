#pragma once
#include <atomic>

namespace Async::UiInteraction {
inline std::atomic<unsigned> owners{0};
inline bool Active() { return owners.load(std::memory_order_relaxed)!=0; }
// App-owned lifetime. Only the UI thread updates its lease; workers read the
// aggregate hint at existing optional job/batch boundaries.
class Lease {
public:
    ~Lease() { Clear(); }
    Lease()=default;
    Lease(const Lease&)=delete;
    Lease& operator=(const Lease&)=delete;
    void Update(bool input,bool moving,double now) {
        if(input || moving) until_=now+.150;
        const bool next=moving || now<until_;
        if(next!=held_) {
            if(next) owners.fetch_add(1,std::memory_order_relaxed);
            else owners.fetch_sub(1,std::memory_order_relaxed);
            held_=next;
        }
    }
    void Clear() {
        if(held_)owners.fetch_sub(1,std::memory_order_relaxed);
        held_=false;until_=0;
    }
private:
    double until_=0;
    bool held_=false;
};
}
