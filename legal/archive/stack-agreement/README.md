# Archived Stack application agreement

Archived on 2026-10-16 at the owner's request so work on Stack's own release
agreement can resume later. Nothing in this folder is consumed by current builds,
installers, startup, or binary release packages.

- `documents/` preserves the original EULA, privacy notice, installer disclosure,
  approval manifest, legal README, and a snapshot of the source license that used
  to be copied into application packages. Original document paths and hashes in
  the manifest describe that historical packet. The root source-code `LICENSE`
  remains in place in the repository.
- `code/` preserves the application agreement manager, generated-version inputs,
  and agreement-related caller fragments before removal.
- `installer/` preserves the installer and CMake setup before removal.
- `tooling/` preserves the affected release helpers before removal.

These are reference snapshots, not supported alternative build scripts. Do not
restore them wholesale over newer source. If the agreement work resumes, decide
the intended terms and review status first, then reconnect only the required
document staging, startup/installer UI, versioning, and consent storage. Verify
fresh and upgraded portable/installed copies and both product packages at that
time.

Third-party notices and dependency license obligations remain active outside this
archive. No third-party license text, attribution, source requirement, or release
signing capability was intentionally removed by this archival.
