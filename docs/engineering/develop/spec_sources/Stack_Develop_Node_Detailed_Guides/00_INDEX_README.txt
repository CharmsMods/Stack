STACK DEVELOP NODE DETAILED PLANNING GUIDE SET
==============================================

Date: 2026-06-10
Status: Authoritative planning guide set, not a claim of current implementation
Prepared for: Stack image editor / Develop node planning

Purpose
=======

This folder splits the larger Develop Node planning document into detailed, concept-first guides.

The documents are intentionally not organized as implementation passes. They are organized around image-editing ideas, data behavior, and user intent:

- what the image data is,
- what Stack is allowed to do to it,
- why a stage exists,
- what alternative choices exist,
- what tradeoffs those choices create,
- how graph-style controls can guide the solver,
- how Auto and Manual should cooperate,
- how Stack can avoid pretending that ordinary preset behavior is a true RAW-solving system.

The guide set is based on:

1. The current DEVELOP_NODE_CONTEXT.txt document, which describes the actual current Develop node behavior and limitations.
2. User direction from the current planning conversation.
3. External RAW/color/tone references used to avoid relying only on common online habits or guessed assumptions.

Core direction
==============

Develop Auto should be a solver, not a preset system and not a pile of ordinary editing sliders.

Natural Finished should be the default Auto mode because the first result should look genuinely usable.

Flat Editing Base should not be an ugly log-style dump. It should bring useful information into a visible range, especially mids, and make Manual editing easier.

Auto controls should guide the processing. They should be allowed to change multiple settings across RAW exposure, local exposure, denoise, tone, color, subject importance, and dynamic range at once.

Graph-style controls should be first-class controls because many editing decisions are tradeoffs, not one-dimensional values.

Manual mode should reveal and refine the exact Auto-authored state. It should not switch to a separate pipeline.

Returning from Manual to Auto should preserve manual edits as mathematical biases unless the user resets or locks/recalibrates differently.

Candidate solving should be supported. Auto may generate multiple render possibilities, remove weak/duplicate ones, and show the useful remaining candidates for selection or merging.

Stack should be honest about what is happening. Recovery, reconstruction, tone rolloff, denoise, brightness, EV multiplication, and display rendering should not be treated as the same thing.

Documents
=========

00_INDEX_README.txt
-------------------
This file. Overview of the guide set and how the documents relate.

01_Develop_Philosophy_Auto_Modes_and_User_Intent.txt
----------------------------------------------------
Defines what Develop is supposed to be, what Auto means, what "realistic" means, why Natural Finished is default, and why modes exist. This is the philosophical anchor for the rest of the set.

02_RAW_Data_Exposure_EV_Brightness_and_Scene_Linear_Control.txt
---------------------------------------------------------------
Explains RAW values, scene-linear control, EV multiplication, black/white levels, exposure versus visible brightness, and why Auto needs control over both data exposure and rendered brightness.

03_Iterative_Auto_Solve_Candidate_Rendering_Convergence_and_Learning.txt
------------------------------------------------------------------------
Defines the multi-pass Auto solver, candidate generation, elimination, convergence, anti-oscillation behavior, candidate selection/merging, and learning controls for current/future images.

04_Dynamic_Range_Highlights_Shadows_and_Local_Exposure_Strategy.txt
-------------------------------------------------------------------
Defines how Stack should handle extreme dynamic range, highlight protection, clipped-data honesty, shadow lifting, noise-aware tone decisions, and local exposure without fake HDR.

05_Subject_Importance_User_Guided_Bias_Brushes_and_Scene_Understanding.txt
--------------------------------------------------------------------------
Defines automatic and user-guided subject/importance bias, including the transparent soft brush overlay idea and edge-aware interpretation behind the scenes.

06_Color_White_Balance_Mood_Preservation_Skin_and_Memory_Colors.txt
-------------------------------------------------------------------
Defines white balance philosophy, neutral versus mood-preserved rendering, camera profile trust, skin protection, memory colors, color richness, and color graph controls.

07_Denoise_Demosaic_Texture_Detail_and_Cleanup.txt
--------------------------------------------------
Defines pre-demosaic denoise, post-demosaic denoise, demosaic quality, hot pixels, false color, texture preservation, detail, sharpening, and clean-versus-natural tradeoffs.

08_Tone_Contrast_Finish_Feel_and_Realistic_Final_Rendering.txt
--------------------------------------------------------------
Defines tone, contrast, highlight shoulder, shadow toe, midtone placement, final image feel, mode-specific tone behavior, and finish-tone graph controls.

09_Manual_Mode_Auto_Handoff_Bias_Preservation_and_Expert_Controls.txt
---------------------------------------------------------------------
Defines Manual mode, how Auto-authored values become editable, how Manual edits become biases when returning to Auto, how locks/safety should work, and how expert controls should be grouped.

10_Graph_Controls_Solver_Controls_Diagnostics_and_User_Education.txt
--------------------------------------------------------------------
Defines the graph-control system in detail, candidate gallery behavior, solver-control philosophy, diagnostics levels, hover education, learning toggles, and requested-versus-achieved result explanations.

Recommended reading order
=========================

Read 01 first.
Read 02 and 03 next.
Then read 04 through 08 for the core image-processing concepts.
Read 09 when planning Manual behavior.
Read 10 when planning UI/UX, graph controls, candidate gallery, diagnostics, and educational explanations.

Current-state caution
=====================

These guides are planning documents. They intentionally describe a target direction for future Stack Develop behavior.

They do not claim that the current Develop node already implements all of this.

According to the current context document, the existing node supports RAW development, scene prep, integrated finish tone, debug views, Auto/Manual UI modes, and several current controls. But it also has known limitations: demosaic is effectively bilinear-only in the current public path, camera transform behavior is approximate, scene prep does not recover missing clipped detail, integrated scene prep/tone are forced on, linear RGB inputs skip mosaic-specific controls, and Auto solve is not yet the richer multi-candidate convergence system described in these guides.

External source anchors used
============================

The documents were written with awareness of the following external references:

- LibRaw documentation and API pages for RAW unpacking, metadata, black/white level fields, and the distinction between RAW data extraction and final production-quality processing.
- Adobe DNG resources for DNG as a public archival RAW format and DNG color/metadata context.
- ACES documentation for scene-referred image states, input transforms, output/display transforms, and color-management separation.
- OpenColorIO documentation for scene-referred and display-referred reference spaces and view transforms.
- RawPedia documentation for demosaicing, RAW processing, and local adjustment concepts.
- darktable documentation for scene-referred workflow, highlight reconstruction, denoise, local contrast, and module-based editing concepts.
- MIT Local Laplacian Filters research for local tone/detail manipulation and natural halo-resistant local edits.
- DaVinci Resolve and Adobe color-grading documentation as examples of multidimensional color/tone controls and color wheels/graphs.

Use of external references
==========================

These guides do not recommend copying another application's exact design.

The point of the external research is to ground concepts and vocabulary while preserving Stack's own direction:

- quality first,
- truthful RAW/data behavior,
- iterative Auto solving,
- graph-based intent controls,
- candidate rendering,
- manual handoff,
- no preset-thinking as the core model.

End of index.
