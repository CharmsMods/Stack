#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

namespace Raw::Bracketing {
struct ExposureEdge {std::size_t a=0,b=0;double ev=0,variance=.0004;};

// Estimate all relative log gains jointly. Fixed entries include the persisted
// origin and user corrections. The inverse normal matrix supplies uncertainty
// in EV squared rather than assuming an exact scale after calibration.
inline bool SolveExposureGraph(const std::vector<ExposureEdge>& edges,
    const std::vector<bool>& fixed,std::vector<double>& ev,std::vector<double>& variance) {
    const auto n=ev.size();if(fixed.size()!=n) return false;
    auto reached=fixed;
    for(std::size_t pass=0;pass<n;++pass) for(const auto& edge:edges) {
        if(edge.a>=n||edge.b>=n||!(edge.variance>0)||!std::isfinite(edge.ev)) return false;
        if(reached[edge.a]||reached[edge.b]) reached[edge.a]=reached[edge.b]=true;
    }
    if(std::find(reached.begin(),reached.end(),false)!=reached.end()) return false;
    std::vector<int> index(n,-1);unsigned count=0;
    for(std::size_t i=0;i<n;++i) if(!fixed[i]) index[i]=count++;
    variance.assign(n,0);if(!count) return true;
    const unsigned columns=2*count+1;
    for(unsigned iteration=0;iteration<4;++iteration) {
        std::vector<double> matrix(static_cast<std::size_t>(count)*columns);
        for(const auto& edge:edges) {
            const double residual=ev[edge.b]-ev[edge.a]-edge.ev;
            const double robust=iteration?std::min(1.,2.5*std::sqrt(edge.variance)/std::max(1e-12,std::abs(residual))):1.;
            const double weight=robust/edge.variance;
            const double target=edge.ev+(fixed[edge.a]?ev[edge.a]:0)-(fixed[edge.b]?ev[edge.b]:0);
            for(const auto node:{edge.a,edge.b}) if(index[node]>=0) {
                const unsigned row=index[node];const double sign=node==edge.b?1:-1;
                matrix[row*columns+2*count]+=weight*sign*target;
                matrix[row*columns+row]+=weight;
                const auto other=node==edge.a?edge.b:edge.a;
                if(index[other]>=0) matrix[row*columns+index[other]]-=weight;
            }
        }
        for(unsigned i=0;i<count;++i) matrix[i*columns+count+i]=1;
        for(unsigned pivot=0;pivot<count;++pivot) {
            unsigned best=pivot;
            for(unsigned row=pivot+1;row<count;++row)
                if(std::abs(matrix[row*columns+pivot])>std::abs(matrix[best*columns+pivot])) best=row;
            if(std::abs(matrix[best*columns+pivot])<1e-16) return false;
            for(unsigned col=0;col<columns;++col) std::swap(matrix[pivot*columns+col],matrix[best*columns+col]);
            const double diagonal=matrix[pivot*columns+pivot];
            for(unsigned col=0;col<columns;++col) matrix[pivot*columns+col]/=diagonal;
            for(unsigned row=0;row<count;++row) if(row!=pivot) {
                const double factor=matrix[row*columns+pivot];
                for(unsigned col=0;col<columns;++col) matrix[row*columns+col]-=factor*matrix[pivot*columns+col];
            }
        }
        for(std::size_t node=0;node<n;++node) if(index[node]>=0) {
            ev[node]=matrix[index[node]*columns+2*count];
            variance[node]=std::max(0.,matrix[index[node]*columns+count+index[node]]);
            if(!std::isfinite(ev[node])||!std::isfinite(variance[node])) return false;
        }
    }
    return true;
}
}
