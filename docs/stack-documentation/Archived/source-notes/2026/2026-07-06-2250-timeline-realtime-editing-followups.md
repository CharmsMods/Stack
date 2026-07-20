# Source Note: Timeline Realtime Editing Followups

- Session ID: idea-20260706-2250-timeline-realtime-editing-followups
- Received: 2026-07-06 22:50
- Source Kind: pasted-text

## Original Text

here are some more ideas i had


pressing space while the timeline is playing should alway stop the timeline first, then the user can press space again to resize whatever


pressing E to go to the left when the playhead is already at frame 1, should have it loop back around to the other side of the currently visible timeline

keyframes should be draggable

i am noticing that while the timeline is open, adjusting things on the timeline doensnt update it in the viewport unless i then press keyframe, i to know if the documentation says why or at all, or if this is just a side effect, because this is not good for realtime editing. lets say the user sets keyframe 1 to 5 in a value somewhere, and then keyframe 2 to 6, then the image viewport should show 6 and the rest of the chain, since that's the last value calculated. now the user should at this moment be able to keep changing values, and lets say that 6 they set was a gaussian blur, changing that value while the playhead is still over that keyframe, OR any other keyframe for the gaussian blur's horizontial row, should update whatever keyframe they're over with the value they're setting. if they're not over a keyframe, then nothing should be saved.
