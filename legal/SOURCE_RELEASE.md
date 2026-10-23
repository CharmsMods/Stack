# Matching source for a binary release

The release helper creates an exact tracked-source snapshot and records evidence.
It does **not** prove that an EXE was built from that source, that every ignored
input was unnecessary, or that every distribution obligation has been met.

## Before a distributable package

1. Commit the intended source, assets, version and build/install scripts. Check
   `git status --short` and `git rev-parse HEAD`. Preserve unrelated work; do not
   delete files just to satisfy the check. Source packaging rejects tracked
   changes and non-ignored untracked files. Local experiments can use
   `-LocalTestPackage`, which remains explicitly non-distributable.
2. Rebuild from that exact revision with the documented Windows toolchain.
   Check the actual CMake cache, dependency revisions/patches, generated assets,
   optional providers and any ignored local inputs. The normal incremental build
   is not independent evidence that every existing binary matches this revision.
   A fresh build tree is preferable; keep the build log and configuration.
3. Verify that the packaged EXEs/DLLs are the outputs of that build, not an old
   deployment folder or a different product. `-SkipBuild` does not establish this.
   Review the package, UI and Settings > Legal on Windows.
4. Review complete corresponding source for the actual binaries, including
   applicable dependencies, modifications, scripts and installation information.
   The helper copies the existing LibRaw source artifact and records/packages
   staged libavoid source when present. It does not automatically collect every
   dependency fetched into the ignored build tree. Review static dependencies,
   optional providers, generated inputs and redistributables separately; supply
   any additional required material. Pinned URLs alone are not a blanket
   substitute for the source distribution required by the applicable license.
5. After those checks, public packaging requires both
   `-ReviewedBuildCommit <full commit SHA>` and `-CorrespondingSourceReviewed`.
   These are explicit maintainer attestations, not an automated legal guarantee.
   Existing signing and payload requirements still apply. Do not pass the flags
   merely to bypass a failure.

## Commands

Run the portable helper tests without building Stack:

```text
python -m unittest discover -s tools/release/tests -p test_source_bundle.py
```

From a clean Windows checkout, a standard unsigned validation package can use:

```powershell
.\tools\release\create_release.ps1 -Product Stack -SkipInstaller
```

It creates source evidence but does not label an unreviewed build as verified.
Use `-PythonExecutable` if Python 3.9 or newer is not named `python` on PATH. Python and Git
must be available. Public packaging retains the existing signing requirements.
No command here publishes a GitHub release, creates a tag or uploads files.

## What to distribute and verify

The product output contains a `<product>-source` directory with `source.zip`,
`source-manifest.json`, `release-provenance.json`, dependency evidence and
`SHA256SUMS`. Preserve the whole directory as a source bundle alongside the
binary release, plus any additional corresponding source identified by review.
The packager also creates a `<product>-<tag>-source-bundle.zip` containing
that directory's contents and includes it in the root release checksum list. Upload that
source-bundle ZIP beside the binaries. The source bundle also has its own
internal checksum list. Choose a release tag pointing to the same recorded
commit. Do not link only to a moving branch or an older release's source ZIP.

The source manifest identifies the immutable Git commit/tree and tracked files.
The provenance record hashes the staged executable files after signing and
labels binary correspondence as unverified or maintainer-reviewed. The helper
checks the revision and clean tree before and after packaging. Ignored local
inputs and third-party completeness remain explicit review requirements.

A downloaded source ZIP without `.git` cannot establish release provenance with
this helper. Build from a Git checkout when preparing a release. Submodules,
LFS pointers or unsupported archive entries must be resolved deliberately, not
silently omitted from a claimed source bundle.
