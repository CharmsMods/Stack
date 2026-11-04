#pragma once
#ifdef _WIN32
#include <d3d11.h>
#include <memory>

namespace Stack::Hdr {
// A presenter owns this object on its GL context thread. The D3D and GL
// textures alias one single-sample FP16 allocation.
class SharedFrame {
public:
    static std::unique_ptr<SharedFrame> Open(ID3D11Device* device,ID3D11Texture2D* destination);
    ~SharedFrame();
    enum class Result { Ready, Pending, Failed };
    // A zero framebuffer means GL already drew the frame; retry only its D3D copy.
    Result Begin(unsigned& framebuffer);
    Result CopyTo(ID3D11DeviceContext* context,ID3D11Texture2D* destination);
    const char* Status() const;
private:
    struct State;
    explicit SharedFrame(std::unique_ptr<State> state);
    std::unique_ptr<State> state_;
};
}
#endif
