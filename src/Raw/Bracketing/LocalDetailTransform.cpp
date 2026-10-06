#include "LocalDetailTransform.h"
#include <cmath>
#include <stdexcept>

namespace Raw::Bracketing {
LocalDetailTransform::LocalDetailTransform(unsigned size):m_Size(size),m_Basis(size*size) {
    if(size!=16&&size!=32)throw std::invalid_argument("Unsupported local detail patch size");
    for(unsigned k=0;k<size;++k)for(unsigned x=0;x<size;++x)
        m_Basis[k*size+x]=std::sqrt((k?2.:1.)/size)*std::cos(3.14159265358979323846*(x+.5)*k/size);
}
std::vector<double> LocalDetailTransform::Forward(const std::vector<double>& input) const {
    const auto n=m_Size;if(input.size()!=n*n)throw std::invalid_argument("Invalid local detail patch");
    std::vector<double> temporary(n*n),output(n*n);
    for(unsigned y=0;y<n;++y)for(unsigned k=0;k<n;++k)for(unsigned x=0;x<n;++x)
        temporary[y*n+k]+=m_Basis[k*n+x]*input[y*n+x];
    for(unsigned k=0;k<n;++k)for(unsigned x=0;x<n;++x)for(unsigned y=0;y<n;++y)
        output[k*n+x]+=m_Basis[k*n+y]*temporary[y*n+x];
    return output;
}
std::vector<double> LocalDetailTransform::Inverse(const std::vector<double>& input) const {
    const auto n=m_Size;if(input.size()!=n*n)throw std::invalid_argument("Invalid local detail coefficients");
    std::vector<double> temporary(n*n),output(n*n);
    for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x)for(unsigned k=0;k<n;++k)
        temporary[y*n+x]+=m_Basis[k*n+y]*input[k*n+x];
    for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x)for(unsigned k=0;k<n;++k)
        output[y*n+x]+=m_Basis[k*n+x]*temporary[y*n+k];
    return output;
}
unsigned LocalDetailBand(unsigned x,unsigned y,unsigned size) {
    const double radius=std::hypot(double(x),double(y));
    const unsigned scale=radius<size*.24?0:radius<size*.5?1:2;
    return 3*scale+(x>y*1.8?0:y>x*1.8?1:2);
}
double LocalDetailWindow(unsigned x,unsigned y) {
    const auto raised=[](unsigned p){const double s=std::sin(3.14159265358979323846*(p+.5)/16);return s*s;};
    return raised(x)*raised(y);
}
}
