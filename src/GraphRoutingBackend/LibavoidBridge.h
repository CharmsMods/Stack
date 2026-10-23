#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef _WIN32
#ifdef STACK_AVOID_BUILD
#define STACK_AVOID_API __declspec(dllexport)
#else
#define STACK_AVOID_API __declspec(dllimport)
#endif
#else
#define STACK_AVOID_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
// No STL objects, exceptions or allocator ownership cross this DLL boundary.
typedef struct StackAvoidPoint { float x, y; } StackAvoidPoint;
typedef struct StackAvoidObstacle { int id; StackAvoidPoint minimum, maximum; } StackAvoidObstacle;
typedef struct StackAvoidConnection { uint64_t id; StackAvoidPoint source, target; } StackAvoidConnection;
typedef int (*StackAvoidCancelled)(void* context);
typedef int (*StackAvoidPathSink)(void* context, uint64_t id, const StackAvoidPoint* points, size_t count);
STACK_AVOID_API void* StackAvoidCreate(StackAvoidCancelled cancelled, void* context, char* error, size_t capacity);
STACK_AVOID_API void StackAvoidDestroy(void* router);
STACK_AVOID_API int StackAvoidUpdate(void* router,
    const StackAvoidObstacle* obstacles, size_t obstacleCount,
    const StackAvoidConnection* connections, size_t connectionCount,
    StackAvoidPathSink sink, void* context, char* error, size_t capacity);
#ifdef __cplusplus
}
#endif
