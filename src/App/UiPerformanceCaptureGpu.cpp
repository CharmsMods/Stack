#include "App/UiPerformanceCapture.h"
#include "Renderer/GLLoader.h"
void UiPerformanceCapture::BeginGpu() {
    gpuActive=false;
    if(!glGenQueries || !glQueryCounter || !glGetQueryObjectiv || !glGetQueryObjectui64v)return;
    for(auto& item:queries) if(item.pending) {
        int ready=GL_FALSE;glGetQueryObjectiv(item.end,GL_QUERY_RESULT_AVAILABLE,&ready);
        if(ready) {
            GLuint64 begin=0,end=0;glGetQueryObjectui64v(item.start,GL_QUERY_RESULT,&begin);
            glGetQueryObjectui64v(item.end,GL_QUERY_RESULT,&end);
            if(item.frame<frames.size())frames[item.frame].gpuMs=end>=begin?double(end-begin)/1e6:0;
            item.pending=false;
        }
    }
    query=(query+1)%int(queries.size());auto& item=queries[query];
    if(item.pending)return;
    if(!item.start){glGenQueries(1,&item.start);glGenQueries(1,&item.end);}
    item.frame=static_cast<std::size_t>(-1);glQueryCounter(item.start,GL_TIMESTAMP);gpuActive=true;
}
void UiPerformanceCapture::EndGpu() {
    if(gpuActive){glQueryCounter(queries[query].end,GL_TIMESTAMP);queries[query].pending=true;}
}
void UiPerformanceCapture::CloseGpu() {
    for(auto& item:queries){if(item.start)glDeleteQueries(1,&item.start);if(item.end)glDeleteQueries(1,&item.end);item={};}
}
