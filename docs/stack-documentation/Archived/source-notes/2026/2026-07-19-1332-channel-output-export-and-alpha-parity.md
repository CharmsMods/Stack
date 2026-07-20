# Source Note: Channel Output Export And Alpha Parity

- Session ID: idea-20260719-1332-channel-output-export-and-alpha-parity
- Received: 2026-07-19 13:32
- Source Kind: pasted-text

## Original Text

### Partial Image component semantics

Selected context:

> If only R and B are connected, Stack should describe the result as containing
> R and B, with G absent. The GPU may still use an RGBA texture internally and
> place zero in its G component, but that storage detail must not falsely tell
> downstream nodes that a real G Channel exists.

User annotation:

> Yes, perfect.

Selected context:

> A genuinely connected constant-zero G Channel remains distinguishable from
> no G Channel.

User annotation:

> Also correct and perfect

### Alpha Channel parity

Selected context:

> Logically, this is an actual Alpha Channel: it has an extent, can branch, and
> can pass through other nodes. Internally, Stack normally would not allocate a
> full-resolution texture containing millions of ones. It can retain one Value
> and broadcast it wherever the Alpha Channel is sampled. A texture only needs
> to be materialized if some later operation genuinely requires one.

User annotation:

> Yes, I was just thinking about the fact that even though we refer to the
> alpha channel as alpha, it's also just a channel, just like the RG and B
> channels, which means we can edit it and do things to it. I wanted to preserve
> that flexibility and not treat the alpha channel like it's something
> different than an RG or B channel.

### Output inspection versus grayscale export

Selected context:

> My resulting recommendation is a hybrid:
> Output stores the authored Channel View, defaults to Neutral, and controls
> both normal viewport display and export.
> The viewport can additionally offer a clearly marked temporary Inspect As
> override for debugging.
> That temporary override never changes export or the saved Output
> interpretation.
> Wires do not own this setting.

User annotation:

> This sounds good, but I think that in order to export the grayscale version
> of a mask, the user needs to just plug it in all three mask places instead of
> only one. We don't need to try to help the user there again, because this is
> making it a little bit more technically correct.
