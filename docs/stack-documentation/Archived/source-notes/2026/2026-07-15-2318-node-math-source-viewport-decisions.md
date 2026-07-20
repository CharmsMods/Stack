# Source Note: Node Math Source And Viewport Decisions

- Session ID: idea-20260715-2318-node-math-source-viewport-decisions
- Received: 2026-07-15 23:18
- Source Kind: pasted-text

## Original Text

# Response annotations:
Each item contains text selected from an earlier Codex response and may include a user comment. Use every selection as context and address every comment in your response.
<response-annotations>
[{"text":"When an image has an embedded color profile, I recommend Stack attach that information to the image without changing its pixels. If an image has no profile, Stack would label its color state as Unknown until the user explicitly assigns one or converts it. Is that the behavior you want?","annotation":"Yes, this sounds perfect."},{"text":"For the viewport, which direction do you want?Recommended: show the graph’s direct result by default, but provide an optional, clearly enabled preview transform that is off by default and never affects exports. The footer would show the buffer, preview, and output states.Strict graph-only: never provide an outside-the-graph preview transform. If the user wants a display conversion, they must add it as a graph node.","annotation":"Yes, we should show the graph's direct result by default, but let's not do the optional preview transform. We can just show what the current working color space is that's connected to an output."}]
</response-annotations>

## My request for Codex:

