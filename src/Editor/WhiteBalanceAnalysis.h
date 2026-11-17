#pragma once
#include "Raw/RawDevelopmentRecipe.h"
#include <memory>
#include <optional>
namespace Stack::Editor {
class WhiteBalanceAnalysis {
public:
    ~WhiteBalanceAnalysis();
    void Cancel();
    bool Start(std::shared_ptr<const Raw::RawImageData>, RawRecipe::RawDevelopmentRecipe,
        std::optional<std::array<float,2>> sample);
    bool Poll(std::array<float,3>& gains, std::string& error);
    bool Busy() const {return bool(job);}
private:
    struct Job;
    std::shared_ptr<Job> job;
};
}
