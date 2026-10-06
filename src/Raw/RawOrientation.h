#pragma once

#include <array>

namespace Raw {

// LibRaw sizes.flip uses dcraw's three-bit transform, not TIFF/EXIF's 1..8.
// Inverse of LibRaw's TIFF decoder table "50132467" indexed by EXIF & 7.
inline constexpr int ExifOrientationFromLibRawFlip(int flip) {
    constexpr std::array<int, 8> orientations { 1, 2, 4, 3, 5, 8, 6, 7 };
    return flip >= 0 && flip < static_cast<int>(orientations.size())
        ? orientations[flip] : 1;
}

// Compose a user-facing clockwise rotation after the camera's EXIF transform.
// Both the output coordinates and the sensor coordinates are top-down here.
inline constexpr int RotateExifOrientationClockwise(int orientation, int degrees) {
    constexpr std::array<int, 4> rotations { 1, 6, 3, 8 };
    constexpr std::array<int, 4> mirroredRotations { 2, 7, 4, 5 };
    const int steps = ((degrees / 90) % 4 + 4) % 4;
    for (int base = 0; base < 4; ++base) {
        if (orientation == rotations[base]) return rotations[(base + steps) % 4];
        if (orientation == mirroredRotations[base]) return mirroredRotations[(base + steps) % 4];
    }
    return rotations[steps];
}

} // namespace Raw
