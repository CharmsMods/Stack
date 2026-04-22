Anti-Goals & Constraints Specification

Project: Realism-First Spectral Path Tracer (C++/OpenGL 4.6)
Purpose: To define strictly forbidden patterns, UI behaviors, and architectural shortcuts to maintain performance and physical accuracy.
1. UI & UX Anti-Goals

    No Node-Based Material Editing: The material system must remain linear and parameter-based. Avoid the implementation of graph-based shader nodes to prevent UI complexity and overhead.

    No "Hidden" Mode Logic: Avoid deep nesting of menus. The "Inspector" should remain the primary source of truth for object properties.

    No Friction-Heavy Confirmations:

        Forbidden: Confirmation pop-ups for non-destructive or easily reversible actions (e.g., deleting an object with the Del key, changing a color, moving a gizmo).

        Required: Confirmations are strictly reserved for high-level destructive actions (e.g., "Clear Scene," "Discard Project," "Overwrite File").

    No Interaction Blocking: The UI must never freeze during heavy tasks.

        If a process (like scene compilation) is running, specific buttons should be greyed out (disabled) rather than locking the entire window.

2. Rendering & Technical Constraints

    No RGB Fallbacks: * Light transport math must strictly remain in the spectral domain.

        Constraint: RGB textures/colors must be converted to spectral data (upsampled) before the rendering loop begins. The integrator is forbidden from switching to 3-component RGB math for "speed."

    No "Fake" Game-Engine Shortcuts (Initial Phase): * Avoid legacy rasterization tricks (Shadow Maps, SSR) for the Path Trace view.

        Post-processing effects like Bloom or Lens Flares are strictly "add-on" features and must not interfere with the raw physical energy accumulation.

    No Mandatory Biased Clamping: While "Firefly" removal and noise reduction are permitted, they must be optional toggles. The renderer must allow for a "pure" unbiased mode where no energy is clamped or discarded.

3. Architecture & Code Style

    No Deep OOP Inheritance:

        Forbidden: Complex class hierarchies (e.g., Object -> Mesh -> GlassMesh -> Renderable).

        Required: Use a Flat Data / ECS approach. Store data in contiguous arrays (Components) and use Systems to process them. This ensures high CPU cache hits and aligns with the GPU's requirement for flattened instance data.

    No Thread-Blocking GL Calls: * The main UI thread must never wait for the GPU to finish a frame.

        All OpenGL execution should be handled via the RenderDelegator using fences/sync objects to prevent the "spinning wheel" cursor.