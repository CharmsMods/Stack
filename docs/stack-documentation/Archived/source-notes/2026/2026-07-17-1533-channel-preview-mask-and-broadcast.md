# Source Note: Channel Preview, Mask, And Broadcast

- Session ID: idea-20260717-1533-channel-preview-mask-and-broadcast
- Received: 2026-07-17 15:33
- Source Kind: pasted-text

## Original Text

### Vector and coordinate explanation

Selected context:

> Vectors and coordinates. A Value is the broad category. Boolean, Number, Vector, and Coordinate are different Value subtypes. A Coordinate `(0.25, 0.75)` means “this location.” A Vector `(0.25, 0.75)` means “move or point this way by this amount.”

User annotation:

> Okay, I think this makes more sense to me now.

### Mask as a Channel with permissive values

Selected context:

> You are correct. A Channel is essentially a two-dimensional sheet of numbers: one number at every pixel. A mask has exactly that mathematical shape. What makes it a mask is its role: it usually means application strength or selection coverage; it normally expects values from 0 to 1; it benefits from mask previews; it connects naturally to mask inputs; a receiving node may clamp or interpret it according to its mask policy. So the emerging direction is: Mask is a Channel with mask semantics, not a separate primary data category.

User annotation:

> Okay, I think this makes more English-language sense to me now, and I see what you're saying. It usually means application strength or selection coverage, which is just normally how I use masks anyway, and that makes sense because that's how a lot of people use them. Normally, expecting values from 0 to 1.
>
> Again, this part we don't necessarily need to enforce. We can have some warnings, but we're just doing math here, and we can display weird stuff if we need to on the viewport.

### Four Image Combine inputs and optional alpha

Selected context:

> Three channels forming an Image. A standalone Channel begins as Neutral. Neutral preview repeats its value across display R, G, and B, producing grayscale. A channel can optionally be previewed or tagged as Red, Green, Blue, or Alpha. An Image Combine node has three ordered display-component inputs. Connecting any three Channels to those inputs creates an Image. The input socket position assigns how that channel contributes to the resulting Image. Alpha is an optional fourth input.

User annotation:

> Yeah, and this is great. This is exactly what I meant, and you're spot-on with one small correction: where you say an image combined node has three order display component inputs, I think it should have four. We just don't need to connect the alpha if we don't need to, because an end result rendered PNG also can't have an alpha of something other than just a 2D plane of 1s or 255. This you basically said, but I want to just reiterate this.

### Display cause and effect

Selected context:

> One small terminology clarification: Stack does not directly control physical monitor subpixels. It outputs red, green, and blue framebuffer components, and the operating system/display pipeline ultimately drives the monitor. But your conceptual model is right.

User annotation:

> Yes, I understand that Stack doesn't directly control the monitor's pixels, but what I was getting at is that when you output those colors, that's what happens. It's just cause and effect.

### Neutral and component previews

Selected context:

> Neutral preview: value → R, G, and B equally. Red preview: value → R; G and B are zero. Green preview: value → G; R and B are zero. Blue preview: value → B; R and G are zero.

User annotation:

> This is absolutely perfect. This is exactly what I was trying to describe and get at.

### Role assignment and preview UI

Selected context:

> The main unresolved question is where role assignment belongs. My recommendation is to separate two things: Image Combine socket assigns the channel’s actual role in the resulting Image. Channel preview setting controls how a standalone channel is visualized without permanently changing its meaning.

User annotation:

> On the issue of when to assign neutral, red, green, or blue, and when to preview which one and what is automatically chosen as the default, I think if we have a single channel, it would be good to preview it, default to neutral, and just display it as a grayscale image in the viewport if we're connecting it to an output node. This would follow from our table above, but I want you to walk me through some options we have or ideas for where we can assign what it actually wants to be manually or what should be manually, so the user can do that.

Selected context:

> Potential UI pieces include: Neutral/R/G/B/A preview selector in a Channel inspector; role badges on pins and wires; clearly labeled Image Combine sockets; draggable or swappable component assignments; grayscale as the default standalone Channel preview.

User annotation:

> If you could explain this and talk to me about it in a more conversational tone with language that derives itself from the actual design of the UI, that would be helpful so I could understand how we could design this interaction for setting what masks are viewed as

### Per-pixel amount and masks

Selected context:

> Suppose Multiply has an Amount input that explicitly accepts either a Value or Channel. If you connect a Number Value, every pixel uses the same number. If you connect a Gradient Channel, each pixel uses the amount stored at its own position.

User annotation:

> This specific part here sounds a lot like just general masking, where we apply different strengths of a transform to different pixels on the image based on their location on the mask overlay. I see how it's a little bit different because we're also allowing for a uniform amount as well, but this is what it made me think of as it went through my head.

Selected context:

> Amount: Value or Channel

User annotation:

> Yeah, we could definitely do "value/channel". As a label on the node next to the connection point for that, where it would accept a value or a channel, this would work a lot like a mask would. I'm actually curious to know if there's any mathematical difference between this and a mask, and if we can just combine the two since they seem to appear to have the same outcome. Again, I want to make sure we're not just duplicating our UI and labels and making everything cluttered.

### Fill Channel confusion

Selected context:

> Number Value 0.5 → Fill Channel → Gaussian Blur / Field Mean / Image Combine. Here the Fill Channel node is useful because the graph is creating a spatial Channel that can be reused like other per-pixel data.

User annotation:

> I really didn't understand this. I'm not sure if you're trying to describe a hypothetical chain on a node graph to get at a problem here, but I would appreciate an explanation of what this is.

### Defer next decisions

Selected context:

> Next decisions: Should Image Combine sockets assign roles by position? What should the UI call a mask? Does the broadcasting rule feel right? Should Value subtypes be separate browser nodes?

User annotation:

> Let's look at these after we've gone over the current stuff.

## Request

> Keep documentation updated along with your responses.

