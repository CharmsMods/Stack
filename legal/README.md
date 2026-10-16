# Stack third-party notices and archived agreements

Current application packages include third-party attributions, dependency license
texts, the Adobe DNG notice, and the corresponding LibRaw source release. These
remain required inputs to the release scripts and are available in the installed
application under `Stack/App/Legal` and Settings > Legal.

## Active release inputs

- `THIRD_PARTY_COMPONENTS.json` — component, detection, license, and notice data.
- `third_party/` — exact dependency license texts and notices stored in this repo.
- `Adobe-DNG-Notice.txt` — documentation notice for the DNG technology.
- `LIBRAW_BUILD_INFO.txt` — build information included with LibRaw's source ZIP.

The root `THIRD_PARTY_NOTICES.md`, staged notices, and release SBOM are generated
from the component manifest and release notice generator. Do not hand-edit the
generated copies. Keep component entries, license files, and runtime staging in
sync when dependencies change.

## Stack's own agreement is archived

At the owner's request on 2026-10-16, the draft compiled-application EULA, privacy
notice, installer disclosure, approval manifest, and their acceptance plumbing
were archived in [archive/stack-agreement](archive/stack-agreement/README.md).
They are not active application terms or binary-package inputs. Installers and
application startup do not request or record acceptance. Signed packaging still
checks signatures; it no longer requires approval of the archived draft packet.

The repository's root [LICENSE](../LICENSE) remains the source-code license. It
is not bundled as an end-user agreement in application downloads. Archiving the
application agreement does not choose an open-source license or dedicate Stack
to the public domain. The product's SPDX licensing fields use `NOASSERTION`;
dependency licensing remains recorded separately.
