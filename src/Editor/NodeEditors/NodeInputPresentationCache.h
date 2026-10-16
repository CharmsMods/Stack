#pragma once
#include "Editor/Internal/RawLab/RawLabCurveEditor.h"
#include <tuple>

namespace Stack::Editor {
// Small derived UI values for one authored node. No recipes, source pixels or
// worker resources are retained. NodeEditorState owns document/node lifetime.
struct NodeInputPresentationCache {
    using DescriptorKey = std::tuple<std::uint64_t,std::uint64_t,std::uint64_t,
        std::uint64_t,std::size_t,int,int>;
    using HistogramKey = std::tuple<std::uint64_t,bool,int,float,float,float>;
    DescriptorKey descriptorKey{};
    NodeMath::ValueDescriptor descriptor;
    bool descriptorValid = false;
    HistogramKey histogramKey{};
    NodeMath::ValueDescriptor histogramDescriptor;
    RawLabInternal::RawLabGraphHistogram histogram;
    std::uint64_t histogramRevision = 0;
    bool histogramValid = false;
};
}
