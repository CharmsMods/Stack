# Stack legal sources

This directory is the editable source of truth for Stack's compiled-application
terms and release-compliance metadata. The root [`LICENSE`](../LICENSE) remains
the editable source of truth for the public repository and source code.

## Edit these files

- `EULA.txt` — terms for official compiled installers and portable releases.
- `PRIVACY.md` — current application and website privacy behavior.
- `INFO_BEFORE_INSTALL.txt` — the non-acceptance disclosure shown by the installer.
- `THIRD_PARTY_COMPONENTS.json` — component, detection, license, and notice data.
- `RELEASE_LEGAL_STATUS.json` — legal-document versions, hashes, and approval state.
- `third_party/` — exact license texts for dependencies stored directly in this repo.

The root `THIRD_PARTY_NOTICES.md`, staged notices, and release SBOM are generated
from `THIRD_PARTY_COMPONENTS.json`. Do not hand-edit generated copies.

## Approval workflow

`EULA.txt` and `PRIVACY.md` are attorney-ready drafts, not approved legal advice.
Public packaging is allowed only when `RELEASE_LEGAL_STATUS.json` says
`"approvalState": "approved"` and every recorded SHA-256 hash matches the
current file. Any text edit changes a hash and therefore invalidates approval.

After legal review, record the approval date and a non-secret external approval
reference identifier in the manifest. Do not commit private correspondence, personal
addresses, certificate passwords, or signing credentials.

Local test installers may use draft documents, but their name, AppId, install
directory, registry data, user-data directories, and artifact names are
deliberately separate from public Stack releases.
