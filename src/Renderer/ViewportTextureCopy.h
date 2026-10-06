#pragma once
namespace Stack::Renderer {
// The caller owns the returned texture. This GPU-only copy preserves GL state
// and full-float RAW precision when the input uses RGBA32F.
unsigned int CopyViewportTexture(unsigned int source, int sourceWidth, int sourceHeight, int width, int height);
}
