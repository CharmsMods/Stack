# Source Note: Output Scenarios And Explicit Alpha

- Session ID: idea-20260718-1801-output-scenarios-and-explicit-alpha
- Received: 2026-07-18 18:01
- Source Kind: pasted-text

## Original Text

### Single-Channel Output interpretation

Selected context:

> 1. Where should single-Channel Output interpretation live?
> Today, dropping an extracted red Channel onto Output connects it specifically
> to R, so it displays red. It does not default to grayscale.
>
> Which behavior do you want?
>
> A — Saved on Output (Recommended): Output stores Channel View: Neutral / Red /
> Green / Blue. Neutral is the default. The viewport and exports from that
> Output match. The upstream Channel remains unchanged.
>
> B — Viewport only: The viewport can inspect the Channel as Neutral/R/G/B, but
> export requires an explicit Image Combine. The viewport might therefore
> differ from export.
>
> C — Stored on the connection: The wire into Output stores Neutral/R/G/B. This
> is branch-specific but makes important behavior easier to overlook.

User annotation:

> Can you give different user scenarios for this, where each one would have
> different effects on the outcome of the situation the user might be in, to
> help me understand this better?

### Unconnected Image Combine inputs

Selected context:

> What should unconnected Image Combine inputs mean?
> Current Stack lets R, G, B, and A remain unconnected. Missing RGB becomes 0;
> missing alpha becomes 1.
>
> Which contract do you want?
>
> A — R/G/B required: Image Combine produces no valid Image until all three are
> connected. Alpha remains optional and defaults to 1.
>
> B — Visible constant defaults (Recommended): Unconnected R/G/B visibly
> default to 0; A defaults to 1. Connecting only R creates a valid pure-red
> Image. The missing planes still exist—they are simply constant-value planes
> rather than connected Channel textures.
>
> C — Preview only: Missing RGB is temporarily zero-filled in the viewport, but
> export is blocked until R/G/B are connected.
>
> I recommend B because it is mathematically explicit, flexible, matches the
> current shader, and fits Stack’s permissive approach. The node UI must visibly
> show those defaults so they are not hidden behavior.

User annotation:

> I think that non-connected connection points for the combine should mean no
> output for that channel. If the user only connects a red and a blue, the
> output should only have a red and a blue channel output together. This will
> mean we'll need to have a little more complex set of rules for what we can
> connect where. We'll need to update the node line connection point text info
> markers to help display this info in the appropriate places.
>
> This will also mean that we'll need to treat the absence of a channel as all
> zeros. In the case of the alpha channel missing a connection, instead of just
> internally treating it as a 1 or 255, I'm not sure if it should be a solid
> texture or solid scalar. What I basically want is to have something connected
> to the alpha automatically if nothing is connected beforehand, or at least
> when the user connects RGB to the combine node and then proceeds to connect
> the output to something else. A new node will automatically spawn and connect
> itself to the alpha input of the combine node.
>
> I'm not sure if this new node should be an auto-generated texture with the
> same resolution as the current resolution of the different channels the
> image is receiving on its color channels. I'm not sure if it should just be
> something that goes along the lines of using the number 1, replicating that
> for every pixel value for the alpha channel, and genuinely treating it like
> an alpha channel. I do want to treat it like an actual channel where we can
> plug this also into something else and then have nodes in between the
> auto-generated node and the alpha input.
