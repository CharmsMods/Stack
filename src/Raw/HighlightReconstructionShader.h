#pragma once

namespace Raw {
// Embedded with the RAW shader after its sensor sampling and mask helpers.
// Donors use calibrated sensor cells, not recursively developed/denoised RGB.
inline constexpr const char* kHighlightReconstructionShader = R"GLSL(
float highlightPeak(vec3 v) { return max(v.r, max(v.g, v.b)); }

vec3 reconstructHighlights(vec2 uv, vec3 inputRgb, vec3 clipMask) {
    float affected = highlightPeak(clipMask);
    if (uHighlightMode == 0 || uHdrVirtualAnchor != 0 ||
        uHighlightStrength <= 0.0 || affected <= 0.0) return inputRgb;

    vec3 wb = max(uWhiteBalance, vec3(0.000001));
    vec3 rgb = uWhiteBalanceBeforeDemosaic != 0 ? inputRgb : inputRgb * wb;
    vec3 reliable = vec3(1.0) - clipMask;
    vec3 hueSum = vec3(0.0), hueSquared = vec3(0.0);
    float mass = 0.0, donorConfidence = 0.0;
    ivec2 p = ivec2(clamp(uv * vec2(uVisibleSize), vec2(0.0), vec2(uVisibleSize - 1)));
    // Bilinear anchors avoid negative-lobe demosaic overshoot influencing the
    // color fit. They do not replace the selected demosaicer's output/detail.
    vec3 anchor = bilinearDemosaic(uCropOrigin + p, p);
    if (uWhiteBalanceBeforeDemosaic == 0) anchor *= wb;
    anchor = max(anchor, vec3(0.0));
    // Clipped inputs invalidate the negative-lobe correction of sharper kernels.
    // Fade to their non-ringing bilinear support only inside the clipping mask.
    vec3 originalRgb = rgb;
    rgb = mix(rgb, anchor, affected);
    if (uHighlightMode == 3 && dot(reliable, reliable) > 0.0001) {
        // Three bounded rings. No neighborhood work for healthy pixels, Off,
        // neutral/luminance modes, or fully clipped interiors.
        for (int ring = 0; ring < 3; ++ring) {
            int radius = 2 << ring;
            for (int direction = 0; direction < 8; ++direction) {
                ivec2 d = ivec2(direction == 0 || direction == 4 || direction == 6 ? -1 :
                               direction == 1 || direction == 5 || direction == 7 ? 1 : 0,
                               direction == 2 || direction == 4 || direction == 5 ? -1 :
                               direction == 3 || direction == 6 || direction == 7 ? 1 : 0);
                ivec2 cell = p + d * radius;
                if (any(lessThan(cell, ivec2(0))) || any(greaterThanEqual(cell + 1, uVisibleSize))) continue;
                vec3 donor = vec3(0.0), count = vec3(0.0);
                float risk = 0.0;
                for (int y = 0; y < 2; ++y) for (int x = 0; x < 2; ++x) {
                    ivec2 q = cell + ivec2(x, y);
                    int c = cfaAt(q);
                    donor[c] += max(0.0, rawNormalizedAt(uCropOrigin + q, q)) * wb[c];
                    count[c] += 1.0;
                    risk = max(risk, highlightPeak(clippedMaskAt(uCropOrigin + q, q)));
                }
                donor /= max(count, vec3(1.0));
                float peak = highlightPeak(donor);
                if (peak <= 0.000001 || risk >= 1.0) continue;
                vec3 hue = donor / peak;
                float denominator = dot(reliable * hue, hue);
                float scale = dot(reliable * anchor, hue) / max(denominator, 0.000001);
                vec3 predicted = hue * scale;
                vec3 error = abs(predicted - anchor) / max(anchor, vec3(0.02 * highlightPeak(anchor) + 0.000001));
                float mismatch = highlightPeak(error * reliable);
                // Dark surfaces and contradictory surviving colors cannot lend
                // hue across a highlight edge. Ratios, not donor detail, travel.
                float brightness = peak / max(highlightPeak(anchor), 0.000001);
                float weight = (1.0 - risk) * (1.0 - smoothstep(0.05, 0.20, mismatch));
                weight *= smoothstep(0.20, 0.60, brightness) / float(radius);
                weight *= 1.0 - smoothstep(4.0, 8.0, scale / max(highlightPeak(anchor), 0.000001));
                // Many nearly saturated neighbors are not independent evidence
                // of reliable color. Bound confidence by their best headroom,
                // not just the accumulated sample weight.
                donorConfidence = max(donorConfidence, weight * float(radius));
                hueSum += weight * hue;
                hueSquared += weight * hue * hue;
                mass += weight;
            }
        }
    }
    vec3 repaired = rgb;
    float confidence = 0.0;
    if (mass > 0.000001) {
        vec3 hue = hueSum / mass;
        vec3 spread = sqrt(max(vec3(0.0), hueSquared / mass - hue * hue));
        float scale = dot(reliable * anchor, hue) / max(dot(reliable * hue, hue), 0.000001);
        vec3 predicted = max(anchor, hue * scale);
        confidence = donorConfidence * smoothstep(0.0, 0.25, dot(reliable, reliable)) * smoothstep(0.0, 0.75, mass) *
            (1.0 - smoothstep(0.03, 0.15, highlightPeak(spread)));
        repaired = mix(rgb, predicted, clipMask);
    }
    // Neutrality is defined AFTER camera conversion. Equal camera channels are
    // generally not neutral, particularly under non-unit white balance.
    vec3 working = uUseCameraTransform != 0 ? uCameraToWorking * repaired : repaired;
    float brightness = max(0.0, highlightPeak(working));
    if (uHighlightMode == 2) brightness = max(0.0, dot(working, vec3(0.2126, 0.7152, 0.0722)));
    vec3 neutral = brightness * uHighlightNeutralCamera;
    vec3 result = mix(repaired, neutral, affected * (1.0 - confidence));
    result = mix(originalRgb, result, clamp(uHighlightStrength, 0.0, 1.0));
    return uWhiteBalanceBeforeDemosaic != 0 ? result : result / wb;
}
)GLSL";
} // namespace Raw
