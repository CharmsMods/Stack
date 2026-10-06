#pragma once
#include <chrono>
#include <map>
#include <string>

namespace Raw::Bracketing {
class ProcessingTimer {
public:
    ProcessingTimer(std::map<std::string,double>& values,std::string name)
        : values_(values),name_(std::move(name)),start_(Clock::now()) {}
    ~ProcessingTimer() { values_[name_]+=std::chrono::duration<double>(Clock::now()-start_).count(); }
    ProcessingTimer(const ProcessingTimer&)=delete;
private:
    using Clock=std::chrono::steady_clock;
    std::map<std::string,double>& values_;
    std::string name_;
    Clock::time_point start_;
};
}
