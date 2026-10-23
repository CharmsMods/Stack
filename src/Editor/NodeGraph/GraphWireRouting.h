#pragma once
#include "NodeGraphTypes.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Stack::GraphRouting {
using EditorNodeGraph::Vec2;
using EditorNodeGraph::GraphRect;
struct Obstacle { int id; GraphRect bounds; };
struct Connection { std::string key; Vec2 source,target; };
struct Scene { std::uint64_t revision=0; std::vector<Obstacle> obstacles; std::vector<Connection> connections; };
struct Path { std::vector<Vec2> points; GraphRect bounds; float length=0; };
struct Result { std::uint64_t revision=0; std::unordered_map<std::string,Path> paths; std::string error; };

// Stateful geometry backend. Call only from its single owning worker (or a
// headless command). No editor, pixels, GL, timer or authored mutation enters it.
class Router {
public:
    explicit Router(const std::atomic<bool>* cancelled=nullptr);
    ~Router();
    Result Update(const Scene& scene);
private:
    struct State;
    std::unique_ptr<State> m_State;
};
// Shared arc-length coordinates for rendering, labels, picking and tracing.
Path MakePath(std::vector<Vec2> points);
Vec2 PointAt(const Path& path,float fraction,Vec2* tangent=nullptr);
float DistanceToPath(const Path& path,Vec2 point);
}
