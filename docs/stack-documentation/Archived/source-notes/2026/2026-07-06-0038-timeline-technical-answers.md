# Source Note: timeline technical answers

- Session ID: idea-20260706-0038-timeline-technical-answers
- Received: 2026-07-06 00:38
- Source Kind: pasted-text

## Original Text

As for what the timeline row should be anchored to internally, yes, it should be anchored to a final output node which gets its final form from what we define as a connected chain.

You're asking: When one upstream chain splits into two outputs, do keyframes on shared upstream nodes affect both outputs? The answer to this is, I believe, yes, because if we have input image, then brightness, then that splits into branch one that has contrast and branch two that has Gaussian blur, and they both connect to output nodes. Adjusting the brightness node that's at the beginning, before the split, will adjust the inputs to both the contrast and the Gaussian branches. Meaning that inherently the data changes from wherever we're adjusting forward.

Stack should definitely automatically create timeline rows per output object chain. From the graph structure, it shouldn't have to be a manual thing for the user. Yes, we want to be able to animate everything in the end for question four. We really want to be able to keyframe pretty much anything.

I don't want to have the mindset of v1, v2, v3, and so on, where you're asking, "Should the first version support this and the second version support that?" What we're doing is creating documentation for the end goal.

For where you're asking, "Should we support keyframe framing all eligible node parameters?" since we still have to have a timeline for when we implement things, I want to do it probably more by node group. We could get a certain amount of nodes done to have all of their values for keyframing supported. We can do a group at a time, but we don't want to apply random, arbitrary, adjustable values to nodes and then wire that into the timeline. We want to be actually keyframing and interpolating the actual adjustable sliders or values on each node.

For your last three questions, 6 to 8, these are going to need more explanation from you in order for me to understand how to give a better answer. Specifically, for the first two, how should framing evaluation work technically? I don't really understand this, and I need you to make it easier to understand, wording-wise. I need you to give me some pros and cons for question 7, and I also need you to look into the legal dependencies of question 8, because Stack currently does not bundle libraw with Stack. Instead, it's a separate DLL that Stack can't load if it's found externally in order to process raw images, as an example. This is done because of the type of licensing libraw has, because we're not allowed to bundle it with the application, or we're not allowed to bundle it inside the EXE.

After you make the updates I've talked about so far, then you can go ahead and work on explaining and researching anything you need to for the last three questions.
