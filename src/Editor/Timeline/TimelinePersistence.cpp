#include "TimelinePersistence.h"

#include "Editor/Timeline/TimelineFrameProducer.h"

#include <algorithm>

namespace Stack::Timeline {
namespace {

constexpr int kTimelinePersistenceSchemaVersion = 1;

const char* TimelineInterpolationToken(TimelineInterpolation interpolation) {
    switch (interpolation) {
        case TimelineInterpolation::Hold: return "hold";
        case TimelineInterpolation::Linear: return "linear";
    }
    return "linear";
}

TimelineInterpolation TimelineInterpolationFromToken(const std::string& token) {
    if (token == "hold") {
        return TimelineInterpolation::Hold;
    }
    return TimelineInterpolation::Linear;
}

} // namespace

nlohmann::json SerializeTimelineDocument(const TimelineDocumentState& document) {
    const TimelineFrameRequest request = NormalizeTimelineFrameRequest(
        document.currentFrame,
        document.durationFrames,
        document.framesPerSecond);

    nlohmann::json tracks = nlohmann::json::array();
    for (const TimelineTrack& track : document.animation.tracks) {
        if (!IsValidTarget(track.target) || track.keyframes.empty()) {
            continue;
        }

        nlohmann::json keyframes = nlohmann::json::array();
        for (const TimelineKeyframe& keyframe : track.keyframes) {
            if (keyframe.frame < 0) {
                continue;
            }

            keyframes.push_back({
                { "frame", std::clamp(keyframe.frame, 0, request.durationFrames - 1) },
                { "value", keyframe.value },
                { "interpolation", TimelineInterpolationToken(keyframe.interpolation) }
            });
        }

        if (keyframes.empty()) {
            continue;
        }

        tracks.push_back({
            { "target", {
                { "nodeId", track.target.nodeId },
                { "parameterId", track.target.parameterId }
            } },
            { "keyframes", std::move(keyframes) }
        });
    }

    return {
        { "schemaVersion", kTimelinePersistenceSchemaVersion },
        { "currentFrame", request.frame },
        { "durationFrames", request.durationFrames },
        { "framesPerSecond", request.framesPerSecond },
        { "tracks", std::move(tracks) }
    };
}

TimelineDocumentState DeserializeTimelineDocument(
    const nlohmann::json& value,
    const TimelineTargetValidator& targetValidator) {
    TimelineDocumentState document;
    if (!value.is_object()) {
        return document;
    }

    const int schemaVersion = value.value("schemaVersion", 0);
    if (schemaVersion <= 0 || schemaVersion > kTimelinePersistenceSchemaVersion) {
        return document;
    }

    const TimelineFrameRequest request = NormalizeTimelineFrameRequest(
        value.value("currentFrame", document.currentFrame),
        value.value("durationFrames", document.durationFrames),
        value.value("framesPerSecond", document.framesPerSecond));
    document.currentFrame = request.frame;
    document.durationFrames = request.durationFrames;
    document.framesPerSecond = request.framesPerSecond;

    const nlohmann::json tracks = value.value("tracks", nlohmann::json::array());
    if (!tracks.is_array()) {
        return document;
    }

    for (const nlohmann::json& trackJson : tracks) {
        if (!trackJson.is_object()) {
            continue;
        }

        const nlohmann::json targetJson = trackJson.value("target", nlohmann::json::object());
        if (!targetJson.is_object()) {
            continue;
        }

        AnimatableParameterTarget target;
        target.nodeId = targetJson.value("nodeId", -1);
        target.parameterId = targetJson.value("parameterId", std::string());
        if (!IsValidTarget(target)) {
            continue;
        }
        if (targetValidator && !targetValidator(target)) {
            continue;
        }

        const nlohmann::json keyframesJson = trackJson.value("keyframes", nlohmann::json::array());
        if (!keyframesJson.is_array()) {
            continue;
        }

        for (const nlohmann::json& keyframeJson : keyframesJson) {
            if (!keyframeJson.is_object()) {
                continue;
            }

            const int frame = keyframeJson.value("frame", -1);
            if (frame < 0) {
                continue;
            }

            SetOrReplaceKeyframe(
                document.animation,
                target,
                std::clamp(frame, 0, document.durationFrames - 1),
                keyframeJson.value("value", 0.0f),
                TimelineInterpolationFromToken(keyframeJson.value("interpolation", std::string("linear"))));
        }
    }

    return document;
}

} // namespace Stack::Timeline
