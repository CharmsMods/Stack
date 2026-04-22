# 3D Features and Layout

## What Can Be Done
The 3D space is a fully-fledged viewer and rendering node that manages object models and global scene parameters, leading to both rasterized workflow edits and full path-traced final image exports.

**Features:**
- **Model Import/Setup:** Direct import of `.glb` or `.gltf` scenes. Drops them into an editable layout where users can assign built-in materials (Matte, Metal, Glass, Emissive) or map local textures properties.
- **Primitives and Text:** Features boolean cutting behavior into basic mathematical shapes (Cubes, Spheres, etc.) and allows fully extruded 3D text generation using uploaded fonts.
- **Lighting Controls:** Employs precise scene lights (Spot, Point, Directional) that can be targeted, alongside completely dynamic World Environments including solid drops, gradients, and HDRI mapping.
- **Dual Rasterization/Traced Modes:** Operates in three explicit modes via keyboard hotkeys:
  - `Edit` (Rasterized, unlit, fast)
  - `Path Trace` (Physically accurate bouncing, slow, gorgeous) 
  - `Mesh` (Wireframes)
- **Advanced Path Tracing Options:** Allows denoting final output samples, glossy filtering logic, and post-bake Denoise layers to produce high-end imagery as backgrounds render while people swap to different tabs.

## Layout Design
- **Workspace Paradigm:** Explicitly ignores the Neumorphic shell styling. Opts for an anti-gloss, completely flat, dark "Technical Workstation" design. Pure black surfaces with pure white text and warm-grey accents. Focuses on extremely large canvas presentation.
- **HUD and Overlays:** Instead of heavy bottom tools, information chips float in viewport corners. Heavy rendering export panels pop out entirely as distinct blocking Modals rather than sitting in side-panels permanently.
- **Outliner/Dock:** Has one heavily tabbed vertical structure on the left containing the scene graph hierarchy, selection parameters, and tool menus.
- **The "Library Link":** Contains a bottom drawer serving as an immediate portal to the Library's 3D folder, letting users physically drag saved .glb models back up onto the canvas plane.
