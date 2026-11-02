#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Raw {
// Coordinates span the complete, unrotated visible image. Only the nonempty
// rectangle is stored. A standalone master can supply its rectangle on demand.
struct DepthMask {
    std::string id, name;
    int width=0, height=0, x=0, y=0, w=0, h=0;
    using Bytes = std::vector<std::uint8_t>;
    std::shared_ptr<const Bytes> pixels;
    std::function<Bytes()> readPixels;
    bool Valid() const {
        return id.size()==64 && id.find_first_not_of("0123456789abcdef")==std::string::npos &&
            !name.empty() && name.size()<=128 && width>0 && height>0 && width<=2048 && height<=2048 &&
            x>=0 && y>=0 && w>0 && h>0 && w<=width && h<=height && x<=width-w && y<=height-h;
    }
    std::shared_ptr<const Bytes> Read() const {
        if(!Valid()) throw std::runtime_error("Invalid depth mask bounds or identity.");
        auto data=pixels;
        if(!data && readPixels) data=std::make_shared<const Bytes>(readPixels());
        if(!data || data->size()!=std::size_t(w)*h) throw std::runtime_error("Incomplete depth mask.");
        return data;
    }
    std::uint8_t At(const Bytes& data,int col,int row) const {
        if(col<x || row<y || col>=x+w || row>=y+h)return 0;
        return data[std::size_t(row-y)*w+col-x];
    }
};
}
