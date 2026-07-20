# Source Note: Channel UI Confirmations And Uniform Reuse

- Session ID: idea-20260717-1632-channel-ui-confirmations-and-uniform-reuse
- Received: 2026-07-17 16:32
- Source Kind: pasted-text

## Original Text

### Standalone Channel viewing

Selected context:

> How channel viewing could work. When a single Channel reaches an Output, Stack should display it as Neutral by default: Neutral: copied equally into display R, G, and B—grayscale. Red: sent to display R; G and B are zero. Green: sent to display G; R and B are zero. Blue: sent to display B; R and G are zero. I think the cleanest UI separates viewing from actual image construction: You connect a Channel directly to Output. It appears as grayscale automatically. The Output inspector or viewport toolbar offers View Channel As: Neutral / Red / Green / Blue. Changing that option only changes the viewport. It does not alter the Channel or attach permanent RGB meaning to it. When you connect that Channel to the R, G, B, or A socket of Image Combine, that socket assigns its real role in the resulting Image.

User annotation:

> This honestly sounds perfect.

### Image Combine as the role-assignment boundary

Selected context:

> When you connect that Channel to the R, G, B, or A socket of Image Combine, that socket assigns its real role in the resulting Image.

User annotation:

> This is a great idea for a possibility of when it could actually change its role.

### Contextual role badges

Selected context:

> Role badges on wires and sockets could reinforce this visually: A standalone wire says Channel. A mask connection says Channel · Mask. Once connected to Image Combine’s green socket, that connection can show a small G badge. The upstream Channel itself remains reusable and neutral.

User annotation:

> This also seems perfect.

### Per-pixel Amount versus Mask

Selected context:

> Is a per-pixel Amount Channel just a mask? They have the same underlying shape—a Channel—but they enter the operation at different stages. A per-pixel amount changes the operation’s parameter: `output(x,y) = operation(input(x,y), amountChannel(x,y))`. An ordinary mask blends between the original and an already-processed result: `processed = operation(input, fixedAmount)` and `output = mix(input, processed, mask)`. For Exposure, those become: per-pixel exposure: `output = input × 2 ^ exposureChannel`; masked fixed exposure: `processed = input × 2 ^ fixedExposure`, then `output = mix(input, processed, mask)`. Those generally produce different results.

User annotation:

> Oh, this actually makes a lot more sense now. We're either changing a parameter or we're blending between the full value and the original image.

### Reusing a Value instead of materializing a Channel

Selected context:

> For example, a 4×4 Channel would conceptually contain sixteen copies of 0.5. But Stack normally should not need to create that texture physically. A shader can simply reuse the single Value for every pixel.

User annotation:

> Oh, okay, so you're saying, I guess, this sounds a lot like an optimization, performance-wise and memory-wise.

## Request

> Go ahead and update the documentation, and then after that's completed reliably, tell me what we need to answer questions on next.
