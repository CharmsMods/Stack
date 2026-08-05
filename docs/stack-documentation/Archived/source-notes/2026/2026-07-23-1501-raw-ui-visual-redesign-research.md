# Source Note: RAW UI visual redesign research

- Session ID: idea-20260723-1501-raw-ui-visual-redesign-research
- Received: 2026-07-23 15:01
- Source Kind: pasted-text

## Original Text

next I want to work on the process of really redefining how the Raw tab visually looks: its layout, where controls are exposed, and what controls even look like. I know we have the basic general concept of sliders, graphs, and drop-downs but I don't really like the idea of the drop-downs. I think I like the idea better of using icons and icon rows to dictate tabs and organization rather than having an extremely long vertical list of sliders with drop-downs, checkboxes, and all this different stuff just all in one.

We already consolidated the top row actions into icons and I also don't like the dockable windows that we have going on here. I do want a mostly fixed layout where you can resize things, but I don't want to be able to move windows around. I also don't want the windows to have seams between them. We were able to get around this with the editor tab and graph, with the transition between the graph window and the viewport window, so you can go look at that in the code if you think it would help.

We don't have the docking window buttons or the window names, so I'd like you to do some research on how major photo/video editing programs lay out a lot of their main raw editing pipeline controls, UI-wise and design-wise. Specifically, think Adobe and DaVinci Resolve. Let's not do dark table, because dark table, even though it's good, kind of falls into this drop-down menu style that I kind of want to avoid. It's okay to have drop-down menus, but I don't want to have drop-down sections that expand, at least as much as we can avoid them.

I also want to refine our graphs, and I want the entire UI surface to feel as floating and seamless as possible, especially borderless, like how I had you remove the containers around the icons. I don't want all these lines, the border around the outsides of the graph, buttons, and text, and just dividers between so many things on the UI.

Before doing any implementation, just do some deep research on these programs' layout and where they store things. You can definitely include things that Stack doesn't have in existence so far. The goal isn't to exactly replicate another program, but if we find a lot of design inspiration from other programs and their functionalities, even if that specific functionality isn't something we either want or already have in Stack, we can still take inspiration from that design and use it for a different control set if we want.

In your response, after you are able to iron out some of the common things that are done, also provide some ideas for more minimalistic and designed layouts that follow more of the minimalistic design that I described just now. If you could also provide some clickable links to some of the sources that you found helpful, that might provide me with a visual understanding of some of these other programs. Any articles or research papers or anything like that that would help me with my ideas and help me to become clear on what I want this UI interface to actually look like.

Before doing your web search, you'll need to look at the code and take a look at what the UI currently has on it for the Raw tab. The basic overview is that we have:
- our local exposure
- global exposure
- tone curve
- our display transform
Those are the main controls that we have right now. There is some other stuff, but I'm not really using that other stuff right now. I'm going to completely revamp the other stuff as well, so we don't need to put too much focus on it right now.
Once you've looked at all that and seen what we have, you can start the process of doing deep online research for the rest of the prompt I gave you. Also, I want you to create a new folder and very frequently update a set of files in it that, in plain language, describe your findings. Your memory is limited, so as you do more code reading and especially as you do more web research, you'll need to write down what you have in there so it doesn't become polluted by context compaction (which you can read about for Codex from OpenAI).
