#include "Raw/MaskAuthoring.h"
#include "Raw/StrokeValueMatcher.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <climits>

namespace Stack::MaskAuthoring {
namespace {
using Field = RawAssetCapture::PreviewField;
using Color = std::array<float,3>;
using Matcher = RawAssetCapture::StrokeValueMatcher;
float Unit(float value) { return std::clamp(value,0.f,1.f); }
float Smooth(float value) { const float t=Unit(value); return t*t*(3-2*t); }
Point Pixel(Point uv,const Field& f) { return {uv[0]*(f.width-1),uv[1]*(f.height-1)}; }
int Index(Point p,const Field& f) {return int(std::round(p[1]))*f.width+int(std::round(p[0]));}
Color Read(const Field& f,int index) {
    const auto* p=&f.values[size_t(index)*3];return {p[0],p[1],p[2]};
}
bool Valid(Color c) {return std::isfinite(c[0]) && std::isfinite(c[1]) && std::isfinite(c[2]);}
void Check(const std::function<bool()>& canceled) {
    if(canceled && canceled())throw std::runtime_error("Mask canceled.");
}
struct Segment {
    Point a,b;float dx,dy,length2;
    Segment(Point start,Point end):a(start),b(end),dx(b[0]-a[0]),dy(b[1]-a[1]),length2(dx*dx+dy*dy){}
    float Along(float x,float y) const {return length2>0?((x-a[0])*dx+(y-a[1])*dy)/length2:0;}
    float Distance(float x,float y,float fraction) const {return std::hypot(x-a[0]-fraction*dx,y-a[1]-fraction*dy);}
};
template<class Visit>
void Box(const Field& f,Point low,Point high,const std::function<bool()>& cancel,Visit visit) {
    const int x0=std::max(0,int(std::floor(low[0]))),x1=std::min(f.width-1,int(std::ceil(high[0])));
    const int y0=std::max(0,int(std::floor(low[1]))),y1=std::min(f.height-1,int(std::ceil(high[1])));
    for(int y=y0;y<=y1;++y){Check(cancel);for(int x=x0;x<=x1;++x)visit(x,y,y*f.width+x);}
}
template<class Visit>
void Path(const Field& f,const Mark& m,Visit visit) {
    for(size_t i=0;i<m.points.size();++i){
        Segment s(Pixel(m.points[i?i-1:0],f),Pixel(m.points[i],f));
        const int steps=std::max(1,int(std::ceil(std::max(std::abs(s.dx),std::abs(s.dy)))));
        for(int j=0;j<=steps;++j){float t=float(j)/steps;visit(Index({s.a[0]+s.dx*t,s.a[1]+s.dy*t},f));}
    }
}
float Feather(float normalized,float feather) {
    return feather>0?Smooth((1-normalized)/feather):float(normalized<=1);
}
void Paint(const Field& f,const Mark& m,std::vector<float>& paint,const std::function<bool()>& cancel) {
    const float r=std::max(.5f,m.radius*std::min(f.width,f.height));
    if(m.tool==Tool::Brush || m.tool==Tool::Erase) {
        for(size_t i=0;i<m.points.size();++i){
            Segment s(Pixel(m.points[i?i-1:0],f),Pixel(m.points[i],f));
            Box(f,{std::min(s.a[0],s.b[0])-r,std::min(s.a[1],s.b[1])-r},
                {std::max(s.a[0],s.b[0])+r,std::max(s.a[1],s.b[1])+r},cancel,[&](int x,int y,int index){
                const float w=Feather(s.Distance(float(x),float(y),Unit(s.Along(float(x),float(y))))/r,m.feather);
                paint[index]=m.tool==Tool::Erase?std::min(paint[index],1-w):std::max(paint[index],w);
            });
        }
        return;
    }
    Segment s(Pixel(m.points.front(),f),Pixel(m.points.back(),f));
    const bool linear=m.tool==Tool::Linear;
    const float rx=std::max(.5f,std::abs(s.dx)),ry=std::max(.5f,std::abs(s.dy));
    Box(f,linear?Point{0,0}:Point{s.a[0]-rx,s.a[1]-ry},
        linear?Point{float(f.width-1),float(f.height-1)}:Point{s.a[0]+rx,s.a[1]+ry},cancel,[&](int x,int y,int index){
        const float u=(x-s.a[0])/rx,v=(y-s.a[1])/ry;
        const float value=linear?Smooth(1-s.Along(float(x),float(y))):
            Feather(m.tool==Tool::Radial?std::hypot(u,v):std::max(std::abs(u),std::abs(v)),m.feather);
        paint[index]=std::max(paint[index],value);
    });
}
struct Profile {
    Segment line;std::array<Color,17> colors;
    // Adjacent cross sections follow this boundary in each tangent direction.
    std::array<std::vector<float>,2> offsets;
    float step=1;
};
float Difference(Color a,Color b) {
    const float l=(std::asinh(a[0]/.18f)-std::asinh(b[0]/.18f))*.22f;
    return l*l+(a[1]-b[1])*(a[1]-b[1])+(a[2]-b[2])*(a[2]-b[2]);
}
void FollowBoundary(Profile& p,const Field& f) {
    const float length=std::sqrt(p.line.length2),nx=p.line.dx/length,ny=p.line.dy/length;
    p.step=std::max(2.f,length*.16f);
    const float contrast=Difference(p.colors.front(),p.colors.back());
    for(int side=0;side<2;++side){
        auto& offsets=p.offsets[side];offsets.push_back(0);
        if(contrast<.0001f)continue;
        const float sign=side?1.f:-1.f;
        for(int k=1;k<=32;++k){
            float best=std::numeric_limits<float>::infinity(),shift=0;
            for(int j=-3;j<=3;++j){
                const float offset=offsets.back()+j*p.step*.35f;
                const float dx=-ny*k*p.step*sign+nx*offset,dy=nx*k*p.step*sign+ny*offset;
                float score=0;
                for(int n:{0,4,8,12,16}){
                    Point xy{p.line.a[0]+dx+p.line.dx*n/16.f,p.line.a[1]+dy+p.line.dy*n/16.f};
                    if(xy[0]<0 || xy[1]<0 || xy[0]>f.width-1 || xy[1]>f.height-1){score=INFINITY;break;}
                    auto c=Read(f,Index(xy,f));if(!Valid(c)){score=INFINITY;break;}
                    score+=Difference(c,p.colors[n]);
                }
                score+=j*j*.00002f;
                if(score<best){best=score;shift=offset;}
            }
            if(best>std::clamp(contrast*.7f,.0025f,.06f))break;
            offsets.push_back(shift);
        }
    }
}
float GuidedCoverage(const Profile& p,float x,float y,Color c,float initial,float full) {
    const float length=std::sqrt(p.line.length2);
    const float tangent=(-(x-p.line.a[0])*p.line.dy+(y-p.line.a[1])*p.line.dx)/length;
    const float k=std::abs(tangent)/p.step;
    const auto& offsets=p.offsets[tangent>=0];
    if(k>=offsets.size())return initial;
    const size_t lo=size_t(k),hi=std::min(lo+1,offsets.size()-1);
    const float shift=offsets[lo]+(offsets[hi]-offsets[lo])*(k-lo);
    const float along=p.line.Along(x,y)-shift/length;
    if(along<-.2f || along>1.2f)return initial;
    // Continuous fit to the measured color profile. Interpolate between
    // adjacent observations so the guide cannot introduce discrete gray bands.
    float best=INFINITY,weightedPosition=0,totalAffinity=0;
    for(int j=0;j<16;++j) {
        Color a=p.colors[j],b=p.colors[j+1];
        float ab2=0,dot=0;
        for(int ch=0;ch<3;++ch){float v=b[ch]-a[ch];ab2+=v*v;dot+=(c[ch]-a[ch])*v;}
        const float fraction=Unit((dot+.0004f*(along*16-j))/(ab2+.0004f));
        for(int ch=0;ch<3;++ch)a[ch]+=(b[ch]-a[ch])*fraction;
        const float t=(j+fraction)/16,delta=t-along;
        const float cost=Difference(c,a)+delta*delta*.012f;
        // Average plausible positions when the same color occurs twice along a
        // blurry edge. Picking a single winner introduces contours at the tie.
        const float affinity=1.f/(.0004f+cost);
        totalAffinity+=affinity*affinity;
        weightedPosition+=affinity*affinity*t;
        best=std::min(best,cost);
    }
    const float endFade=Smooth(float(offsets.size())-k);
    const float location=.65f*weightedPosition/totalAffinity+.35f*Unit(along);
    const float normalFade=Smooth((along+.2f)*5)*Smooth((1.2f-along)*5);
    const float trust=normalFade*endFade*Smooth(1-best/.02f);
    return initial+(full*(1-Smooth(location))-initial)*trust;
}
}
// Visit every covered pixel. Keep actual observed colors for matching, bounded
// by merging perceptually adjacent bins when a sample spans many thousands of colors.
void CaptureEvidence(const Field& f,Recipe& recipe,const std::function<bool()>& cancel) {
    for(auto& m:recipe.marks) {
        if(!m.samples.empty() || m.points.empty())continue;
        if(m.tool==Tool::Edge){
            const Segment line(Pixel(m.points.front(),f),Pixel(m.points.back(),f));
            if(line.length2<1)continue;
            for(int i=0;i<17;++i){const float t=i/16.f;
                auto c=Read(f,Index({line.a[0]+line.dx*t,line.a[1]+line.dy*t},f));
                if(!Valid(c)){m.samples.clear();break;}m.samples.push_back(c);}
            continue;
        }
        if(m.tool!=Tool::Values && m.tool!=Tool::Circle)continue;
        int shift=0;
        std::unordered_map<uint64_t,Color> representatives;
        const auto key=[&](Color c){
            const auto code=[&](float v){return uint64_t(std::clamp(v,0.f,65535.f))>>shift;};
            return (code(std::asinh(c[0]/.18f)*256+32768)<<32) |
                (code(c[1]*2048+32768)<<16) | code(c[2]*2048+32768);
        };
        auto sample=[&](int i){auto c=Read(f,i);if(!Valid(c))return;
            representatives.try_emplace(key(c),c);
            if(representatives.size()>4096){++shift;
                auto previous=std::move(representatives);representatives.clear();
                for(const auto& item:previous)representatives.try_emplace(key(item.second),item.second);}
        };
        if(m.tool==Tool::Values)Path(f,m,sample);
        else {const Point a=Pixel(m.points.front(),f);const float r=std::max(.5f,m.radius*std::min(f.width,f.height));
            Box(f,{a[0]-r,a[1]-r},{a[0]+r,a[1]+r},cancel,[&](int x,int y,int i){if(std::hypot(x-a[0],y-a[1])<=r)sample(i);});}
        for(const auto& item:representatives)m.samples.push_back(item.second);
        std::sort(m.samples.begin(),m.samples.end());
    }
}
Assets::Record Evaluate(const Field& f,const Recipe& recipe,const std::function<bool()>& cancel) {
    if(f.width<1 || f.height<1 || size_t(f.width)*f.height!=f.values.size()/3 ||
        f.values.size()%3 || f.values.size()/3>size_t(INT_MAX)) throw std::runtime_error("Invalid mask image.");
    const int count=f.width*f.height;
    Matcher included(true,true,2.f),protectedColors(true,true,2.f),transition(true,true,2.f);
    std::vector<int> seeds;
    std::vector<Profile> profiles;
    bool values=false,hasInclusion=false,hasProtection=false,hasTransition=false;
    Recipe measured=recipe;
    CaptureEvidence(f,measured,cancel);
    for(const auto& m:measured.marks) {
        Check(cancel);if(m.points.empty())continue;
        if(m.tool==Tool::Values) {
            values=true;for(auto c:m.samples)included.Add(c);
            Path(f,m,[&](int i){seeds.push_back(i);});
        } else if(m.tool==Tool::Circle) {
            const Point center=Pixel(m.points.front(),f);
            for(auto c:m.samples) {
                if(m.role==Role::Include){included.Add(c);hasInclusion=true;}
                else if(m.role==Role::Protect){protectedColors.Add(c);hasProtection=true;}
                else {transition.Add(c);hasTransition=true;}
            }
            if(m.role==Role::Include)seeds.push_back(Index(center,f));
        } else if(m.tool==Tool::Edge) {
            Profile p{Segment(Pixel(m.points.front(),f),Pixel(m.points.back(),f)),{}};
            if(p.line.length2<1)continue;
            if(m.samples.size()==17){std::copy(m.samples.begin(),m.samples.end(),p.colors.begin());FollowBoundary(p,f);profiles.push_back(std::move(p));}
        }
    }
    included.Build();protectedColors.Build();transition.Build();
    auto data=std::make_shared<Assets::FloatData>();data->width=f.width;data->height=f.height;
    data->channels=1;data->units="coverage";auto& out=data->values;out.assign(count,0);
    if(values) {
        for(int i=0;i<count;++i){
            if(i%f.width==0)Check(cancel);auto c=Read(f,i);
            if(Valid(c))out[i]=hasProtection && protectedColors.Coverage(c)>.985f?0.f:included.Coverage(c);
        }
        if(!recipe.wholeImage) {
            std::vector<unsigned char> visited(count);
            std::vector<int> queue;
            auto add=[&](int i){if(!visited[i] && out[i]>.015f){visited[i]=1;queue.push_back(i);}};
            for(int i:seeds)add(i);
            while(!queue.empty()){
                Check(cancel);const int i=queue.back();queue.pop_back();
                if(i%f.width)add(i-1);if(i%f.width+1<f.width)add(i+1);
                if(i>=f.width)add(i-f.width);if(i<count-f.width)add(i+f.width);
            }
            for(int i=0;i<count;++i)if(!visited[i])out[i]=0;
        }
    }
    for(const auto& m:recipe.marks)if(!m.points.empty() && m.tool>=Tool::Brush && m.tool<=Tool::Rectangle)Paint(f,m,out,cancel);
    // Resolve uncertain colors from the nearby sides. Preserve the footprint
    // and use unmodified neighbors, so traversal order cannot move the edge.
    const auto footprint=(hasTransition || !profiles.empty())?out:std::vector<float>{};
    for(int y=0;y<f.height;++y) {
        Check(cancel);
        for(int x=0;x<f.width;++x){
            const int i=y*f.width+x;auto c=Read(f,i);
            if(!Valid(c)){out[i]=0;continue;}
            if(out[i]<=0)continue;
            const float protect=hasProtection?protectedColors.Coverage(c):0;
            if(protect>=1.f){out[i]=0;continue;}
            float weight=out[i]*(1-protect);
            const float unknown=hasTransition?transition.Coverage(c):0;
            if(unknown>0) {
                float yes=0,no=0,nearby=0;
                for(int d:{1,2,4,8})for(auto dir:{Point{-1,0},Point{1,0},Point{0,-1},Point{0,1}}) {
                    int nx=std::clamp(x+int(dir[0])*d,0,f.width-1),ny=std::clamp(y+int(dir[1])*d,0,f.height-1);
                    const int n=ny*f.width+nx;auto v=Read(f,n);if(!Valid(v))continue;
                    const float affinity=1.f/(d*(.001f+Difference(v,c)));
                    const float p=hasProtection?protectedColors.Coverage(v):0;
                    const float knownPaint=hasInclusion?included.Coverage(v):0.f;
                    const float paintConfidence=std::max(knownPaint,1-transition.Coverage(v));
                    yes+=affinity*(1-p)*paintConfidence*footprint[n];no+=affinity*p;
                    nearby=std::max(nearby,footprint[n]);
                }
                if(yes+no>0)weight+=(nearby*yes/(yes+no)-weight)*unknown;
            }
            for(const auto& p:profiles) {
                const float t=Unit(p.line.Along(float(x),float(y)));
                const int fullX=std::clamp(int(std::round(x-t*p.line.dx)),0,f.width-1);
                const int fullY=std::clamp(int(std::round(y-t*p.line.dy)),0,f.height-1);
                const float resolved=GuidedCoverage(p,float(x),float(y),c,weight,std::max(footprint[i],footprint[fullY*f.width+fullX]));
                weight+=(resolved-weight)*Smooth((1-protect)*4);
            }
            out[i]=Unit(weight);
        }
    }
    Assets::Record asset;asset.kind=Assets::Kind::Mask;asset.data=data;return asset;
}
}

