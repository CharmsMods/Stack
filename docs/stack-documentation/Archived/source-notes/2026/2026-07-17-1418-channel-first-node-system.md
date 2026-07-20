# Source Note: Channel-First Node System

- Session ID: idea-20260717-1418-channel-first-node-system
- Received: 2026-07-17 14:18
- Source Kind: pasted-text

## Original Text

### Annotation: full-image connections and explicit channels

Selected context:

> There is not a separate “three-channel image socket” and “four-channel image socket.” Both are an Image socket. RGB/RGBA and alpha meaning are supposed to be carried in the semantic descriptor.

User annotation:

> I think this makes sense, but I think it would be good to maybe enforce the structure of the code where we really can access each channel individually, even when we have lines or connections that are full image connections. It might be good to add a default where, when a user imports an image into the graph, it's automatically split into all of its channels, and then those channels are connected to a combined node.

### Annotation: scalar and scalar-field naming

Selected context:

> A ScalarField means one number at every pixel location. That is different from a Scalar, which is one number for the entire graph.

User annotation:

> im not sure if these are the "professional" names in industry for this but i dont really like this naming scheme, "scalar" just seems weird, what else could we name these? I'm kind of trying to mentally add on to the whole idea of viewing Stack as collections of channels being manipulated. Mentally adding on how we can represent an entire texture with one number, or have one number for every pixel, which is basically also just the pixel's colors if that's how you view it in some ways.

### Annotation: masks and fewer mathematical categories

Selected context:

> A Mask is also a spatial single-value field, but with mask-like 0–1 meaning.

User annotation:

> See, we also have the name mask, and we have scalar field. It seems like we have a lot of different names for a bunch of different things. We could perhaps classify them in a lot fewer categories in terms of genuinely mathematically what they actually are and make things a lot easier to organize mentally for the user when they're trying to do things.

### Annotation: reliable rules for first-class values

Selected context:

> Boolean, integer, vector, matrix, coordinate, and curve values now exist in the authoring system, although most do not yet have many real consumers.

User annotation:

> It's totally fine that these don't have a lot of uses or connections or ways the user can actually implement them yet, but we'll need to make sure that they have reliable graph rules and structure. That way, it's easy to tell where things plug in, what things are, and what these different nodes actually do.

### Annotation: Divide correction

Selected context:

> There is also a current correctness problem in Divide: its shader calculates approximately:

User annotation:

> this should definately be fixed then

### Annotation: operation capability categories and performance

Selected context:

> A useful taxonomy is:
>
> Pointwise operations — Each output pixel depends only on the input at the same coordinate: Add, Exposure, contrast, transfer functions, premultiply.
>
> Neighborhood operations — Each output pixel samples nearby pixels: blur, sharpen, median, bilateral filtering, many denoisers.
>
> Geometry and resampling — Coordinates are changed: Reformat, crop, rotate, distortion, ripple, lens correction.
>
> Reductions and measurements — Many pixels become one value: Field Mean.
>
> Global transforms — The entire image participates: FFT and frequency filtering.
>
> Multi-image processing — Multiple frames contribute: HDR Merge and, eventually, MFSR.
>
> Specialized pipelines — RAW sensor processing is a different data domain with different storage and rules.

User annotation:

> see like, with stack knowing the difference between operations on pixels that are single pixel operations, vs operations that take into account multiple pixels and areas of the image, does this help performance wise, because it seems to add a layer of complexity that, if needed, is fine, and i want to understand, but im not trying to make stack smart just for the heck of it and "know the difference between things" just because we can then display it. i also do understand that there are a lot of different operations we can do mathwise on an image tho, so i guess we just need a really robust but language mechanically simple definition system to organize everything, so lets spend some time on that shall we?

### Annotation: whether non-RAW editing needs metadata

Selected context:

> Technical Image nodes
>
> These are the beginning of explicit color and alpha mathematics:
>
> Assign sRGB; Assign Linear sRGB; Assign Linear Display-P3; sRGB Decode; sRGB Encode; Linear sRGB → Linear Display-P3; Linear Display-P3 → Linear sRGB; Exposure EV; Premultiply; Unpremultiply.
>
> “Assign” changes the description without changing the pixels. “Convert” changes pixel values.

User annotation:

> See, this right here is what I was kind of saying earlier. It goes along with what I was saying earlier. Why would we need to assign a change to the metadata of an image just to display it? Do we actually need to use any metadata for non-raw editing anywhere?
>
> I've been told in the past that different operations, like brightness, saturation, contrast, and especially exposure, need a color space tag in order to work properly, but it's just doing straight math, isn't it? Math is neither wrong nor right. It's just whatever you put in, you get out. If we want to have it be correct, yes, then we can convert the color space and transform, but this doesn't need to be forced, does it?
>
> Go ahead and tell me if there's anything I'm missing, because I want to learn about this.

### Annotation: replacing the old layer-library mindset

Selected context:

> The older layer library
>
> The legacy/high-level registry currently has 55 descriptors, of which 52 are visible. The browser adds “Needs Fix” and “Experimental” to the names where appropriate.

User annotation:

> Stack actually came about after I wanted to copy the functionality of a browser-based, web page-based image editor version that I made a long time ago. This older layer library is the remnant of that.
>
> We don't necessarily need to keep how this is structured or organized. These effects and damage, blur and denoise, dither and stylization, and all that kind of stuff doesn't really need to stay as categories as it is. We can change that however we want, but I do want to make it so that we can definitely continue expanding the library of nodes that we have that do a bunch of different things, like we already have so far in the future.
>
> Keep them within our channel mindset and our connection points input mindset, where we have inputs for mask and inputs for our multiplier or scalar values that we're going to rename later. Just kind of have everything better defined and a little bit more down to earth while still being user-friendly language-wise, but user-friendly doesn't mean lie to the user or mislead them. In program creation, it is always important how you present information.

### Annotation: unifying legacy and new systems

Selected context:

> The important warning is that “Stable” is a registry lifecycle label, not mathematical certification. These layers come from an older, more opaque system. Their channel policies—Channel Safe, Channel Warning, Full Image Preferred, and Full Image Only—are useful guidance, but are not complete semantic contracts.

User annotation:

> Yeah, I think that because some of this stuff is older, we need to update it and kind of unify our entire system into one mindset, like I've been talking about so far, so that we don't have legacy stuff going on and legacy structure and legacy mindset.

### Annotation: visual cues and dependable channel rules

Selected context:

> Brightness, Contrast, Box Blur, Gaussian Blur, Flip, Median, Mean, and Pixelation are explicitly considered channel-safe. Saturation, Warmth, Color Grade, chroma operations, palette reconstruction, and View Transform depend on RGB or luminance relationships and are full-image-oriented. Spatial effects may technically work on one channel but can cause channel misregistration after recombination.

User annotation:

> This right here is a great example of how we would need some visual cues, feedback, and reliable and very definable graph rules for what we're allowed to plug in, where, for our edge cases of different math nodes needing more than one channel sometimes and depending on relationships between channels and numbers. This is something we need to really put some time and thought into making visually understandable for the user and having it actually be reliable under the surface.

### Annotation: understanding color-space preparation and automation

Selected context:

> What Stack currently does about color space
>
> A color value is supposed to carry color identity or primaries, transfer function, reference, and alpha convention. Stack currently has no hidden working color space and inserts no automatic conversion into the graph. An imported sRGB image therefore remains encoded unless the graph explicitly decodes it.

User annotation:

> I think my whole issue with the color space thing is partly that I just don't understand what a lot of these acronyms and names mean. I do have a surface-level understanding, but I don't know if it's better that we automatically add transfer nodes when we import images and stuff like that to visually help the user and their workflow. I don't know if color space doesn't actually mean something we're tacking on to every node. It's just how we prepare the data before we start doing stuff to it. Does that make sense to you, how I'm confused? I want the best quality, and I also want the most flexibility, but I want it to be easy for the user to use while still being professional.

### Annotation: fixing View Transform

Selected context:

> It does not apply the sRGB transfer function or perform an ICC monitor conversion. It is better understood as a scene-to-display-range tone mapper, not a complete color-management pipeline.

User annotation:

> So for this, what would you suggest we do to fix this?

### Annotation: alpha behavior for full-image math

Selected context:

> Alpha and compositing
>
> Alpha is now treated explicitly in some important places: Premultiply, Unpremultiply, Straight Source Over, Premultiplied Source Over, alpha-preserving color conversions and Exposure, and Channel Split/Combine.

User annotation:

> Since Alpha is technically its own channel, I think it might be good to have a setting in the graph settings pop-up that toggles on and off. When you plug a full image stream into a math node, it acts on the Alpha channel in the same way it acts on the rest of the RGB channels.
>
> I think this should be off by default because it's just easier. If it's on and we're being technically correct, we don't want to plug an image into a brightness node, then turn the brightness down, and connect that to an output. We then realize we've also turned the opacity down, and now our image is half transparent as well. That would be annoying.
>
> I think there should be an option, at least in case we want to do more explicit edits, or in case the user wants to do more explicit edits. This will just allow for some flexibility.

### New request: compound nodes, decomposition, and documentation organization

> Something else we really need to work on is the large system idea we have for being able to combine multiple nodes into a single node. We can also take existing nodes and dissolve them into multiple sub-nodes that do different things that we can directly extract from the math of the normal node. You'll see some of this in the current code, but we need to talk about this and figure out what direction we can take this and what our limitations are with relation to the channel mindset and relationships between the math and the nodes.
>
> I have annotated a lot here, and I don't want you to just go on and try to start implementing a bunch of things. What I want you to do is create a new folder in the documentation, and then inside that we can organize things by creating new folders and new files.
>
> I want you to sort out what I've said and annotate it into multiple files, where we have files that iterate on what we need to have a conversation more on at a later time. Sort what we can actually implement now and fix, because we have enough info to actually do so. There's gonna be a lot more on the former, because we have a lot more to talk about before we can converge on reliable plans and ideas for exactly what we want through both conversation and online research. That's why we're doing this organizing first.
>
> Go ahead and create that new folder structure and file structure. Keep the names human-readable. If you need to, you can also still look at Stack's code and do any online research you need. Let's just try to sort out what I've annotated and write down the questions that I've asked and the things that I've tried to communicate in those files. We can come back to them one by one and look over them independently so that we can give them our full attention one at a time.
