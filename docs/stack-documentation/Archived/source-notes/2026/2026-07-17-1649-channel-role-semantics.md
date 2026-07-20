# Source Note: Channel Role Semantics

- Session ID: idea-20260717-1649-channel-role-semantics
- Received: 2026-07-17 16:49
- Source Kind: pasted-text

## Original Text

### Mask label

Selected context:

> Should the normal pin simply say Mask, while detailed information says Channel · Mask? This is probably the cleanest balance between friendly and precise language.

User annotation:

> Yes, I like this.

### Role preservation

Selected context:

> When should an operation preserve a role? For example, blurring a Mask probably produces another Mask. But what about adding two masks, multiplying a Mask by an EV Channel, or performing arbitrary math that makes its meaning uncertain?

User annotation:

> I think preservation should be the default, but it shouldn't be so strong that it becomes annoying.

### Manual role assignment

Selected context:

> Can users assign roles manually? We need to define whether actions such as Treat As Mask, Treat As Alpha, or Treat As EV merely control presentation and warnings, or also make semantic promises used by later nodes.

User annotation:

> I definitely think that changing the roles for channels should affect what downstream nodes receive and show off on the UI visually.
