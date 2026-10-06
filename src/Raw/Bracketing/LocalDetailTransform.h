#pragma once
#include <vector>

namespace Raw::Bracketing {
// Orthonormal, separable DCT. The same basis serves analysis and synthesis.
class LocalDetailTransform {
public:
    explicit LocalDetailTransform(unsigned size);
    unsigned Size() const {return m_Size;}
    std::vector<double> Forward(const std::vector<double>&) const;
    std::vector<double> Inverse(const std::vector<double>&) const;
private:
    unsigned m_Size;
    std::vector<double> m_Basis;
};
unsigned LocalDetailBand(unsigned x,unsigned y,unsigned size);
double LocalDetailWindow(unsigned x,unsigned y);
}
