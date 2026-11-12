#include "NameMatch.h"
#include <algorithm>
#include <array>
namespace Stack::Search::Detail {
PreparedName PrepareName(const std::wstring& folded) {
    PreparedName result;std::wstring word;
    for(auto c:folded) {
        if((c>=L'a' && c<=L'z') || (c>=L'0' && c<=L'9') || c>=128) {result.compact+=c;word+=c;}
        else if(!word.empty()){result.words.push_back(std::move(word));word.clear();}
    }
    if(!word.empty())result.words.push_back(std::move(word));
    return result;
}
namespace {
int Distance(const std::wstring& a,const std::wstring& b,int limit) {
    if(a.size()>64 || b.size()>64 || std::abs(int(a.size())-int(b.size()))>limit)return limit+1;
    std::array<int,65> previous{},before{},row{};
    for(int j=0;j<=int(b.size());++j)previous[j]=j;
    for(int i=1;i<=int(a.size());++i) {
        row.fill(limit+1);row[0]=i;int best=limit+1;
        for(int j=std::max(1,i-limit);j<=std::min(int(b.size()),i+limit);++j) {
            row[j]=std::min({previous[j]+1,row[j-1]+1,previous[j-1]+(a[i-1]!=b[j-1])});
            if(i>1 && j>1 && a[i-1]==b[j-2] && a[i-2]==b[j-1])row[j]=std::min(row[j],before[j-2]+1);
            best=std::min(best,row[j]);
        }
        if(best>limit)return limit+1;
        before=previous;previous=row;
    }
    return previous[b.size()];
}
}
int MatchName(const std::wstring& name,const PreparedName& prepared,const std::wstring& query,const PreparedName& q) {
    if(name==query)return 0;
    if(name.find(query)!=std::wstring::npos)return 1;
    if(!q.compact.empty() && prepared.compact.find(q.compact)!=std::wstring::npos)return 2;
    if(q.words.empty() || q.words.size()>8)return -1;
    int score=3;
    for(const auto& token:q.words) {
        if(prepared.compact.find(token)!=std::wstring::npos)continue;
        const int limit=token.size()>=7?2:token.size()>=3?1:0;
        if(!limit)return -1;
        int best=limit+1;
        for(const auto& word:prepared.words)best=std::min(best,Distance(token,word,limit));
        // Also tolerate a typo in a name whose separators were omitted.
        best=std::min(best,Distance(token,prepared.compact,limit));
        if(best>limit)return -1;score+=best;
    }
    return score;
}
}
