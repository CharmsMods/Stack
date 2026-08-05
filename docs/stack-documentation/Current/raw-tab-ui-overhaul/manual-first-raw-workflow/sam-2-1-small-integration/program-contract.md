# SAM 2.1 Small Program Contract

## Goal

Provide an optional, local, promptable semantic-selection capability that can
help Stack isolate a person or object when color, brightness, and connected
edges alone are insufficient.

SAM 2.1 Small proposes or refines a mask. Stack remains responsible for:

- the interaction and preview;
- mask composition and persistence;
- luminance/color qualification;
- Local Exposure math;
- undo, dirty state, save, load, and export;
- package installation and removal;
- disclosure of third-party components.

## Initial User Capability

The first product slice should support:

- one positive point or rough box;
- additional positive points;
- negative points to exclude regions;
- an asynchronous proposed outline;
- Add, Subtract, and Intersect with Stack's deterministic mask;
- Accept and Cancel;
- continued Local Exposure editing using the accepted mask without rerunning
  inference.

`Select Main Subject`, people-part selection, sky classification, text
prompting, video propagation, and automatic batch selection are later
possibilities, not initial requirements.

## Invariants

- Stack remains a closed proprietary application.
- SAM and its runtime remain clearly attributed third-party components.
- The base application launches and edits normally without the optional pack.
- The model and runtime are not embedded in `Stack.exe`.
- Inference is local and offline for the initial product.
- Model input is deterministic and versioned.
- Hovering alone does not invoke expensive neural inference.
- Inference never changes a recipe until the user accepts or begins an
  explicitly authorized edit.
- An accepted mask remains usable when the pack is absent or upgraded.
- Exports consume stored mask data, not live inference.
- Model failure cannot corrupt the current preview, recipe, or saved project.
- Existing Local Exposure and RAW processing math remain authoritative.

## Non-Goals

- Reintroducing automatic RAW editing.
- Training or fine-tuning SAM inside Stack.
- Shipping a Python interpreter or package environment.
- General third-party model/plugin loading.
- Cloud inference or transmission of user photographs.
- Bundling every SAM checkpoint.
- Replacing Stack's deterministic qualifier and connected-area selection.
- Making UI redesign dependent on model integration.
- Claiming that an AI-generated mask is mathematically certain.

## Ownership and Attribution

Stack may describe the feature as Stack AI-assisted selection powered by an
approved SAM 2.1 Small model, provided the product identifies Meta's
third-party model and its license accurately.

Stack's integration, UI, mask algebra, persistence, package manager, and
selection workflow are Stack work. The model architecture and checkpoint are
not.

## Success Definition

The program succeeds when:

- difficult low-contrast subjects require materially less manual correction
  than Stack's qualifier alone;
- repeated prompts feel interactive after image embedding;
- accepted masks are stable, editable, and non-destructive;
- the optional package can be installed, verified, updated, rolled back, and
  removed without affecting core editing;
- every shipped artifact has a complete license, hash, provenance, and
  reproducibility record;
- no unacceptable source-disclosure or redistribution obligation is imposed
  on proprietary Stack.
