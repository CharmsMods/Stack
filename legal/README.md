# Stack licensing and third-party notices

## Stack's original code

Stack's original program source, build scripts, documentation and program assets are licensed under
[GNU GPL version 3 only](../LICENSE), SPDX identifier `GPL-3.0-only`.
[STACK-LICENSE-NOTICE.txt](STACK-LICENSE-NOTICE.txt) records the scope,
copyright, third-party distinction and source-distribution reminder. This grant
covers original program assets, including original icons and generated assets,
to the extent of the copyright holder's rights; it does not assert exclusive
copyright in generated material.
Selling GPL-covered copies is allowed; recipients retain their GPL rights.
This does not promise a right to relicense outside contributions or third-party
code for a future proprietary product.

Third-party libraries, adapted code, fonts, icons and other third-party materials
retain their applicable licenses. Stack's license does not erase those terms
or claim ownership of those materials. Names and logos do not imply endorsement.

Binary packages keep licensing documents under `Stack/App/Legal`, available
through Settings > Legal. Dependency documents are in its `ThirdParty/`
subdirectory. The adaptation records there include
`ThirdParty/LibRaw-Demosaic-NOTICE.txt` and `ThirdParty/LiquidGlass-NOTICE.txt`.

[Original replacement icon provenance and regeneration](ORIGINAL_ICONS.md)
records the six newly authored UI assets and their editable SVG sources.

## Active release inputs

- `THIRD_PARTY_COMPONENTS.json`: component, detection, license and notice data.
- `third_party/`: dependency license texts and notices stored in this repo.
- `Adobe-DNG-Notice.txt`: DNG technology notice.
- `LIBRAW_BUILD_INFO.txt`: exact LibRaw DLL source/build record.
- `STACK-LICENSE-NOTICE.txt`: Stack license scope and source-distribution notice.

The root third-party notices, staged notices and SBOM derive from the component
manifest and release notice generator. Preserve the component records, license
files and runtime staging together when dependencies change. Package notices
are selected using the actual staged product; a source-tree notice file alone
does not prove binary-package completeness.

The LibRaw DLL remains dynamically linked under the recorded LGPL 2.1 option.
The separately adapted PPG, VNG and AHD source has a documented GPL-3.0-only
election through LibRaw's LGPL option and LGPL 2.1 section 3; see
[the adaptation record](third_party/LibRaw-Demosaic-NOTICE.txt).

## Before distributing a new binary release

Include the GPL text, Stack notice and every applicable dependency notice.
Provide matching complete corresponding source by a GPL-compliant method,
including required build/install scripts and applicable dependency material.
Verify the actual package against its source revision, not merely the latest
branch. The existing LibRaw source ZIP does not by itself supply corresponding
source for the entire Stack application.

Static license bookkeeping is not a Windows build, runtime test, complete
asset-rights audit or legal assurance. Verify package contents and any optional
model/runtime licensing before distribution. Do not distribute optional model
artifacts merely because their setup tools exist in the repository.

## Historical agreement archive

[`archive/stack-agreement/`](archive/stack-agreement/README.md) preserves the former draft compiled-application EULA,
privacy notice and acceptance plumbing. Those archived files are historical,
not active package terms, and are not reinstated by the GPL transition.
Earlier published versions retain the terms applicable to those versions.

[Matching-source release checklist and helper](SOURCE_RELEASE.md) explains the
source bundle, immutable revision checks and explicit maintainer review required
before public packaging. These checks do not replace a Windows build or prove
complete corresponding source automatically.
