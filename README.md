# Stack

Stack is a native desktop image compositing application for Windows, written in C++ with OpenGL and Dear ImGui. A node-based processing backend supports both graph editing and visual editing views with controls designed for each tool.

The project is under active development. Features, interfaces and output behavior can change; source availability alone does not establish release readiness or performance.

## Downloads and examples

See [GitHub Releases](https://github.com/CharmsMods/Stack/releases) for published installers, portable packages, version-specific notes and image examples. The current source tree can be newer than the latest published binary. Use the release notes for the version you download rather than assuming every feature in the source is included.

The published packages target Windows. Installer and portable-package behavior, including Explorer integration, is documented in [Building and packaging](Guidance/BUILDING.md).

## Workflows

The codebase includes RAW development, layers and masks, color and tone controls, and graph-based image processing. Stack Burst provides capture grouping and multi-frame processing through a separate worker, with a standalone interface as well as integration into Stack.

Optional learned-model features are separate from the normal application payload. Experimental AI-assisted alignment is off by default and requires a separately prepared SEA-RAFT package. Developer model setup is not a self-contained public installer. See the [optional AI alignment instructions](Guidance/BUILDING.md#optional-ai-alignment-package) for prerequisites and limitations.

Original camera RAW files are read-only inputs. Keep backups of important images and projects while evaluating development software.

## Source and build documentation

**Read the [current source license](LICENSE) before using the source.** The repository is currently source-available under a proprietary license, not GPL or another open-source license. Public visibility does not grant permission to build, modify or redistribute Stack beyond that license.

For the maintainer and others with the necessary permission, [Building and packaging](Guidance/BUILDING.md) describes the Windows build workflow, tool requirements, output layout and focused checks. The normal full-product entry point is `build.cmd`; it closes running Stack app and worker instances before building. No build or runtime verification is implied by this README.

- [Repository layout](Guidance/REPOSITORY.md)
- [Implementation guidance](Guidance/Implementation/README.md)
- [Build and packaging instructions](Guidance/BUILDING.md)

## Licensing and third-party components

Stack's own source terms are in [LICENSE](LICENSE). Third-party code, libraries, fonts and other materials retain their respective licenses; they are not relicensed by Stack's source license.

See [third-party component metadata](legal/THIRD_PARTY_COMPONENTS.json), [third-party notices](THIRD_PARTY_NOTICES.md) and the [legal documentation](legal/README.md). Some dependencies and notices are selected when a package is staged, so consult the legal materials included with the particular binary package as well. The archived custom application agreement is historical material, not an active package input.

## Feedback and contributions

Read [CONTRIBUTING.md](CONTRIBUTING.md) before submitting feedback or creative contributions. The current contribution policy does not accept unsolicited code or artwork. Use [SECURITY.md](SECURITY.md) for security-reporting guidance rather than posting sensitive details in a public issue.
