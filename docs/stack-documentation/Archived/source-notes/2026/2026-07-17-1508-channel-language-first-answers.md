# Source Note: Channel Language First Answers

- Session ID: idea-20260717-1508-channel-language-first-answers
- Received: 2026-07-17 15:08
- Source Kind: pasted-text

## Original Text

### Value, Boolean, vectors, and coordinates

Selected context:

> Value: one number, Boolean, vector, coordinate, etc.

User annotation:

> I do like this, but also the word "boolean" is great, along with "value". However, I would like to learn more about specifically where vectors or coordinates are used in order to understand whether I would like to use that kind of terminology for the edits we can do with nodes.

### Accepted primary names

Selected context:

> Channel: one value at every pixel. Image: a related bundle of channels. Data: curves, LUTs, histograms, statistics, and metadata. Specialized: RAW, spectra, frame collections, and external-model data.

User annotation:

> These are great names, and I like these.

### Scalar as internal terminology

Selected context:

> “Scalar” is a legitimate professional mathematical term—it means one number rather than a vector—but Stack doesn’t have to use it as its primary user-facing word. We could retain terms such as uniform scalar internally while the interface simply says Value.

User annotation:

> Yeah, I like this idea a lot. It just makes it more beginner-friendly.

### Mask as a channel

Selected context:

> Should Mask be a separate category or a kind of Channel?

User annotation:

> Here's the thing: isn't Mask already a kind of channel mathematically, since it's just a single channel instead of three or four?

### Three channels defining an image and interactive display roles

Selected context:

> What makes several channels an Image?

User annotation:

> This is a very interesting concept to explore and hopefully reliably define soon. My thoughts on it so far are that I think three channels should define an image. I actually don't want to constrain the image classification to having to have pre-defined R, G, and B channels, where metadata determines which channel is R and which channel is G. I want to be able to just plug three channels in and have them be an image.
>
> This does present an issue of needing to know which channel to display on which subpixels on the screen. What we're going to need to do is come up with a user-interactive UI way to choose which connection point, basically add the metadata ourselves, and choose if a channel is neutral or if it's an RGB channel. Display-wise, if we're just trying to view a single-channel output, we would want to view that channel three different times equally on all of our different subpixels on the screen. If we tag it as red, green, or blue, then we would only view it as the subpixel color corresponding to that channel designation.

### Need for UI exploration

Selected context:

> My recommendation: Image: related channels with declared roles, such as R/G/B/A. Channel Set or Data Image: an arbitrary bundle of channels.

User annotation:

> I'm not really sure here, mostly because in this design we can't just update how the code views things. We have to make sure that the UI reflects it well, so we need to put a little more time into thinking about how we can actually show this. And what ways would be best?

### Broadcasting principle

Selected context:

> Suppose Multiply accepts an Image and you connect the Value 0.5. Stack can repeat that value at every pixel. The professional term for this is broadcasting.

User annotation:

> This sounds mathematically correct, so unless I'm missing context here, this sounds correct.

### Broadcasting UI needs explanation

Selected context:

> We have two broad choices: Require a visible Broadcast or Fill node every time. Allow it only on inputs explicitly designed to accept either a Value or a Channel.

User annotation:

> This doesn't make a whole lot of sense to me, just because I don't understand it. If you could explain this better, what the context of each of these is, and what the different outcomes would be, that would be great.

### Imported image channel appearance

Selected context:

> How should imported image channels appear? The main options are: Always create visible Split and Combine nodes. Use an Image node with expandable R/G/B/A pins. Use a compact compound that can be opened or dissolved. Keep one Image connection, but create extraction nodes when a channel is requested. Offer simple and channel-ready import templates.

User annotation:

> I think that when we import an image, it should import like it does normally right now, but let's add the option to select it and then right-click and then dissolve it into all of its channels, including the alpha channel also

