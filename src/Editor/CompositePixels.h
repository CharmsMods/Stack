#pragma once
#include <vector>
namespace Stack::EditorRendering {
struct PreparedCompositePixels {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
};

bool PrepareCompositePixels(std::vector<unsigned char> source, int width, int height, int padding, bool keepFullFrame, PreparedCompositePixels& output);
}
