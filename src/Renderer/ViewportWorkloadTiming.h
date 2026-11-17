#pragma once
#include "Renderer/ViewportPreviewPolicy.h"

namespace Stack::Renderer {
// Completed warm work, expressed as planned regional and complete-input pixels.
// Zoom changes these features, not the compatible graph/kernel identity. All
// fitting happens on arrival of evidence; draw-time estimates are bounded reads.
class ViewportWorkloadTiming {
public:
    using Clock = ViewportPreviewTimingCurve::Clock;
    void Record(std::size_t identity, std::uint64_t regional, std::uint64_t complete,
        double ms, bool cold = false, Clock::time_point now = Clock::now()) {
        if (!identity || cold || !(regional || complete) || !std::isfinite(ms) || ms <= 0) return;
        if (contexts.size() >= 64 && !contexts.count(identity)) {
            const auto oldest = std::min_element(contexts.begin(), contexts.end(),
                [](const auto& a, const auto& b) { return a.second.updated < b.second.updated; });
            contexts.erase(oldest);
        }
        auto& context = contexts[identity];
        if (now < context.updated) return;
        auto& points = context.points;
        points.erase(std::remove_if(points.begin(), points.end(), [&](const auto& p) {
            return now - p.at > ViewportPreviewTimingCurve::MaximumAge;
        }), points.end());
        auto same = std::find_if(points.begin(), points.end(), [&](const auto& p) {
            return p.regional == regional && p.complete == complete;
        });
        if (same != points.end()) {
            // Three consistent changes invalidate the previous load regime.
            const int direction = ms > same->ms * 1.5 ? 1 : ms * 1.5 < same->ms ? -1 : 0;
            same->drift = direction && same->direction == direction ? same->drift + 1 : (direction ? 1 : 0);
            same->direction = direction;
            if (same->drift >= 3) { points.clear(); }
            else if (direction) return;
            else { same->ms += .25 * (ms - same->ms); same->at = now; Fit(context, now); return; }
        }
        if (points.size() >= 24) points.erase(std::min_element(points.begin(), points.end(),
            [](const auto& a, const auto& b) { return a.at < b.at; }));
        points.push_back({regional, complete, ms, now});
        Fit(context, now);
    }
    ViewportPreviewTimingEstimate Estimate(std::size_t identity, std::uint64_t regional,
        std::uint64_t complete, Clock::time_point now = Clock::now()) const {
        const auto found = contexts.find(identity);
        if (found == contexts.end() || !(regional || complete)) return {};
        const auto& c = found->second;
        const Point* nearest = nullptr;
        double distance = std::numeric_limits<double>::infinity();
        bool allFresh = true;
        for (const auto& p : c.points) {
            const bool fresh = now >= p.at && now - p.at <= ViewportPreviewTimingCurve::MaximumAge;
            allFresh &= fresh;
            if (!fresh) continue;
            if (p.regional == regional && p.complete == complete) return {p.ms,true,true,false};
            // An unobserved dependency family cannot be predicted by shrinking
            // an unrelated sample, even when its total pixel count is similar.
            if ((!p.regional && regional) || (!p.complete && complete)) continue;
            const double d = std::abs(std::log((regional+1.0)/(p.regional+1.0))) +
                std::abs(std::log((complete+1.0)/(p.complete+1.0)));
            if (d < distance) { distance=d; nearest=&p; }
        }
        if (!nearest) return {};
        const double dr=regional/1e6-c.origin[0], dc=complete/1e6-c.origin[1];
        const bool supported=c.rank==2 || (c.rank==1 &&
            std::abs(dr*c.direction[1]-dc*c.direction[0]) <= 1e-6*std::max(1.0,std::hypot(dr,dc)));
        if (c.fitted && supported && allFresh && regional <= c.maxRegional * 2.0 && complete <= c.maxComplete * 2.0) {
            const double ms = c.coefficients[0] + c.coefficients[1] * (regional/1e6) +
                c.coefficients[2] * (complete/1e6) + c.residual;
            if (std::isfinite(ms) && ms > 0) return {ms,true,false,true};
        }
        const double growth = std::max({1.0, regional/double(std::max<std::uint64_t>(1,nearest->regional)),
            complete/double(std::max<std::uint64_t>(1,nearest->complete))});
        return {nearest->ms*growth,true,false,true};
    }
    void Reset() { contexts.clear(); }
private:
    struct Point {
        std::uint64_t regional=0,complete=0;
        double ms=0;
        Clock::time_point at{};
        int drift=0,direction=0;
    };
    struct Context {
        std::vector<Point> points;
        Clock::time_point updated{};
        std::array<double,3> coefficients{};
        double residual=0,maxRegional=0,maxComplete=0;
        bool fitted=false;
        int rank=0;
        std::array<double,2> origin{},direction{};
    };
    std::map<std::size_t,Context> contexts;
    static void Fit(Context& c, Clock::time_point now) {
        c.updated=now; c.fitted=false; c.residual=0;
        c.maxRegional=c.maxComplete=0;
        for (const auto& p:c.points) {
            c.maxRegional=std::max(c.maxRegional,double(p.regional));
            c.maxComplete=std::max(c.maxComplete,double(p.complete));
        }
        // Solve all nonnegative subsets of fixed + regional + complete cost.
        // Rank-deficient evidence stays conservative until sizes/zoom provide
        // independent observations of every active feature.
        double best=std::numeric_limits<double>::infinity();
        for (unsigned mask=1;mask<8;++mask) {
            int columns[3]{},n=0;
            for(int j=0;j<3;++j) if(mask&(1u<<j)) columns[n++]=j;
            if(c.points.size()<std::size_t(n+1))continue;
            double a[3][4]{};
            for(const auto& p:c.points) {
                const double x[]{1,p.regional/1e6,p.complete/1e6};
                for(int i=0;i<n;++i) {
                    for(int j=0;j<n;++j)a[i][j]+=x[columns[i]]*x[columns[j]];
                    a[i][n]+=x[columns[i]]*p.ms;
                }
            }
            bool valid=true;
            for(int i=0;i<n;++i) {
                int pivot=i;
                for(int j=i+1;j<n;++j)if(std::abs(a[j][i])>std::abs(a[pivot][i]))pivot=j;
                if(std::abs(a[pivot][i])<1e-9){valid=false;break;}
                for(int j=0;j<=n;++j)std::swap(a[i][j],a[pivot][j]);
                const double divisor=a[i][i];
                for(int j=i;j<=n;++j)a[i][j]/=divisor;
                for(int k=0;k<n;++k)if(k!=i){const double factor=a[k][i];for(int j=i;j<=n;++j)a[k][j]-=factor*a[i][j];}
            }
            if(!valid)continue;
            std::array<double,3> beta{};
            for(int i=0;i<n;++i){beta[columns[i]]=a[i][n];valid &= a[i][n]>=0;}
            if(!valid)continue;
            double error=0,residual=0;
            for(const auto& p:c.points){const double e=p.ms-beta[0]-beta[1]*p.regional/1e6-beta[2]*p.complete/1e6;error+=e*e;residual=std::max(residual,e);}
            if(error<best){best=error;c.coefficients=beta;c.residual=residual;}
        }
        // A lower-rank fit is usable only on its observed feature line. For
        // example a constant complete-input size cannot identify how its cost
        // changes, and proportional sizes alone cannot identify a new crop.
        c.rank=0;
        c.origin={c.points.front().regional/1e6,c.points.front().complete/1e6};
        for(const auto& p:c.points) {
            const double dr=p.regional/1e6-c.origin[0],dc=p.complete/1e6-c.origin[1];
            const double length=std::hypot(dr,dc);
            if(length<1e-6)continue;
            if(!c.rank){c.direction={dr/length,dc/length};c.rank=1;}
            else if(std::abs(dr*c.direction[1]-dc*c.direction[0])>1e-6*std::max(1.0,length))c.rank=2;
        }
        c.fitted=c.points.size()>=3 && c.rank && std::isfinite(best);
    }
};
}
