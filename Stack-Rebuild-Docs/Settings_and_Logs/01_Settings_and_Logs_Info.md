# Settings and Logs Features

## What Can Be Done

### Logs
Logs serve as the central nervous system readout. Rather than being mere debug commands, they are first-class workflows for stability auditing.
- **Process Streams:** Creates living cards tracking active subroutines: "Workspace Boot", "Library Hydration", "File Exports", "Renderer Activity".
- **Download Behaviors:** Cards store in-memory histories and can be explicitly downloaded individually as pure text outputs.

### Settings
Settings dictate the system-level behavior of the app beyond individual document instances.
- **Global Themes and Aesthetics:** Drives exactly how bright or dark the "Neumorphic Shell" renders, determining drop-shadow darkness and corner radii across the entire app.
- **Tool Level Governance:** Allows setting limits. E.g., instructing the Composite tool to prefer background execution, or configuring universal editor limits like WebWorker bounds.
- **Performance Thresholds:** Surfaces probed internal GPU caps (like max texture dimension) visibly to the user to predict why huge saves might crash based on hardware.

## Layout Design

### Workspace Paradigms
- Both tools utilize the core App Shell neumorphic styling constraints. They feel like system option hubs.
- **Logs Layout:** Simple linear card display. Each process stream constructs an independent visual module that groups related readouts sequentially.
- **Settings Layout:** Structured heavily in explicit category tabs (`General`, `Personalization`, `Library`, `Editor`, `Composite`, `3D`, `Logs`). Designed with explicit save/apply architectures.
