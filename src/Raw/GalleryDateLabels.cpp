#include "GalleryDateLabels.h"
#include <chrono>
#include <ctime>
#include <unordered_map>
namespace Stack::RawWorkspace {
namespace {
int Day(int year,unsigned month,unsigned day) {
    year-=month<=2;const int era=(year>=0?year:year-399)/400;
    const unsigned y=year-era*400;
    return era*146097+int(y*365+y/4-y/100+(153*(month+(month>2?-3:9))+2)/5+day-1);
}
struct Calendar {
    std::chrono::steady_clock::time_point next{};
    std::tm now{};
    int day=0,week=0,offset=0;
    std::uint64_t revision=0;
    std::unordered_map<std::int64_t,GalleryDateLabel> labels;
    void Update() {
        const auto tick=std::chrono::steady_clock::now();
        if(tick<next)return;next=tick+std::chrono::seconds(1);
        const auto time=std::time(nullptr);std::tm local{};localtime_s(&local,&time);
        const int current=Day(local.tm_year+1900,local.tm_mon+1,local.tm_mday);
        std::tm utc{};gmtime_s(&utc,&time);
        const int zone=(current-Day(utc.tm_year+1900,utc.tm_mon+1,utc.tm_mday))*1440+
            (local.tm_hour-utc.tm_hour)*60+local.tm_min-utc.tm_min;
        if(!revision || current!=day || zone!=offset) {
            offset=zone;
            now=local;day=current;week=day-(local.tm_wday+6)%7;++revision;labels.clear();
        }
    }
};
Calendar& Clock(){static Calendar state;return state;}
}
std::uint64_t GalleryCalendarRevision(){auto& clock=Clock();clock.Update();return clock.revision;}
const GalleryDateLabel& FriendlyGalleryDate(std::int64_t timestamp) {
    auto& clock=Clock();if(!clock.revision)clock.Update();
    if(const auto found=clock.labels.find(timestamp);found!=clock.labels.end())return found->second;
    if(clock.labels.size()>8192)clock.labels.clear();
    GalleryDateLabel result{"unknown","Date unknown",{}};
    std::tm local{};const auto time=static_cast<std::time_t>(timestamp);
    if(timestamp>0 && localtime_s(&local,&time)==0) {
        const int day=Day(local.tm_year+1900,local.tm_mon+1,local.tm_mday);
        char exact[64]{};std::strftime(exact,sizeof(exact),"%B %d, %Y",&local);result.exact=exact;
        if(day>clock.day) {result.key="future";result.label="Future dates";}
        else if(day==clock.day) {result.key="day-"+std::to_string(day);result.label="Today";}
        else if(day==clock.day-1) {result.key="day-"+std::to_string(day);result.label="Yesterday";}
        else if(day>=clock.week) {result.key="week-"+std::to_string(clock.week);result.label="Earlier this week";}
        else if(day>=clock.week-7) {result.key="week-"+std::to_string(clock.week-7);result.label="Last week";}
        else {
            result.key="month-"+std::to_string(local.tm_year)+"-"+std::to_string(local.tm_mon);
            if(local.tm_year==clock.now.tm_year && local.tm_mon==clock.now.tm_mon)result.label="Earlier this month";
            else {char month[48]{};std::strftime(month,sizeof(month),local.tm_year==clock.now.tm_year?"%B":"%B %Y",&local);result.label=month;}
        }
    }
    return clock.labels.emplace(timestamp,std::move(result)).first->second;
}
}
