# Library Features and Layout

## What Can Be Done
The Library manages the persistent storage and browsing for every project type and raw asset in the application.

**Features:**
- **Mixed Project Storage:** Holds saved instances from Editor, Composite, and 3D in one unified database structure.
- **Asset Catalog:** Manages a separate repository for standalone images and 3D models (.glb).
- **Import/Export Pipeline:** Users can perform bulk tag operations, secure encrypted bundle transfers, and handle folder-level asset import/export.
- **Preview Generation:** Maintains thumbnails and visual snapshots of varying projects to display heavily optimized grids. Capable of retroactively rendering previews for older Editor projects if they are missing.
- **Tagging & Filtering:** Multi-select operations and tag-based catalog filtering for mass organization.

## Layout Design
- **Workspace Paradigm:** Shares the structured Shell UI pattern (Neumorphic Style).
- **Page Tabs:** Distinct switching tabs at the top for `Library` (Projects) and `Assets` (Raw Models/Images).
- **Grid View:** A heavy visual grid of project preview cards.
- **Side Drawer & Modals:** Provides a detail inspection view when clicking an object, and uses heavy modal overlays for drag-safe secure imports, warning prompts, and explicit save wizards.
- **3D Integration:** Can spawn a lightweight 3D viewer directly inside the asset-detail overlay to preview .glb models without opening the full 3D editor.
- **Loading Overlays:** Utilizes non-blocking progress feedback loops so the UI doesn't lock while database hydration completes.
