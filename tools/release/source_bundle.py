#!/usr/bin/env python3
"""Create a tracked-source snapshot and explicit, non-legal release evidence.

Python 3.9+ standard library and Git only. This does not build or execute repository
code, prove binary correspondence, or determine GPL/corresponding-source compliance.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile


class BundleError(Exception):
    pass


LIMITATIONS = [
    "Only tracked Git source is in source.zip; ignored and external build inputs are not automatically collected.",
    "Dependency artifacts are supplied by the caller; completeness, versions, licenses, patches, and required build/install materials need maintainer review.",
    "Staged executable/DLL hashes are an inventory, not proof that binaries were built from this commit.",
    "Review flags are maintainer assertions, not automatic corresponding-source or legal-compliance certification.",
]


def safe_path(value):
    """Reject archive names unsafe or ambiguous on common Windows extractors."""
    parts = value.split("/")
    reserved = {"CON", "PRN", "AUX", "NUL"} | {
        prefix + str(i) for prefix in ("COM", "LPT") for i in range(1, 10)
    }
    if (not value or "\\" in value or any(ord(c) < 32 or ord(c) == 127 for c in value)
            or any(p in ("", ".", "..") or ":" in p or p[-1:] in (".", " ")
                   or p.split(".")[0].upper() in reserved for p in parts)):
        raise BundleError("Unsafe archive path: " + repr(value))
    return value


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path, data):
    path.write_text(json.dumps(data, indent=2, sort_keys=True) + "\n", encoding="utf-8")


class Git:
    def __init__(self, root):
        self.root = root.resolve()
        self.env = os.environ.copy()
        for name in list(self.env):
            if name.startswith("GIT_"):
                del self.env[name]
        self.env.update(GIT_OPTIONAL_LOCKS="0", GIT_TERMINAL_PROMPT="0")
        self.options = ["-c", "core.fsmonitor=false", "-c", "core.hooksPath=" + os.devnull]
        # A status/diff may invoke a configured clean filter. Disable every
        # configured external filter before inspecting the working tree.
        result = self.run("config", "--null", "--name-only", "--get-regexp",
                          r"^filter\..*\.(clean|smudge|process|required)$", allowed=(0, 1))
        drivers = {key.decode("utf-8").rsplit(".", 1)[0]
                   for key in result.split(b"\0") if key}
        for driver in sorted(drivers):
            for field in ("clean", "smudge", "process"):
                self.options += ["-c", driver + "." + field + "="]
            self.options += ["-c", driver + ".required=false"]
        top = Path(self.run("rev-parse", "--show-toplevel").decode().strip()).resolve()
        if top != self.root:
            raise BundleError("--root must name the Git checkout root, not a subdirectory")

    def command(self, *args):
        return ["git", "--no-pager", "-C", str(self.root)] + self.options + list(args)

    def run(self, *args, allowed=(0,), **kwargs):
        result = subprocess.run(self.command(*args), env=self.env, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, check=False, **kwargs)
        if result.returncode not in allowed:
            raise BundleError("Git " + args[0] + " failed: " + result.stderr.decode(errors="replace").strip())
        return result.stdout

    def clean_commit(self, expected=None):
        commit = self.run("rev-parse", "--verify", "HEAD^{commit}").decode().strip()
        if expected is not None and expected != commit:
            raise BundleError("Expected revision does not match current HEAD: " + commit)
        # These flags can hide tracked changes, including sparse checkouts.
        for entry in self.run("ls-files", "-v", "-z").split(b"\0"):
            if entry and (entry[:1].islower() or entry[:1] == b"S"):
                raise BundleError("Unsupported assume-unchanged/skip-worktree index flag: "
                                  + entry[2:].decode(errors="replace"))
        # Reject before status can traverse submodules and their separate config.
        for entry in self.run("ls-files", "--stage", "-z").split(b"\0"):
            if entry.startswith(b"160000 "):
                raise BundleError("Gitlinks/submodules require explicit source handling; refusing: "
                                  + entry.split(b"\t", 1)[1].decode(errors="replace"))
        dirty = self.run("status", "--porcelain=v1", "-z", "--untracked-files=all",
                         "--ignore-submodules=none")
        if dirty:
            raise BundleError("Source checkout is not clean (tracked, staged, or nonignored untracked changes): "
                              + dirty.replace(b"\0", b"; ").decode(errors="replace")[:1000])
        return commit

    def snapshot(self, commit):
        tree = self.run("rev-parse", commit + "^{tree}").decode().strip()
        entries = []
        seen = {}
        for record in self.run("ls-tree", "-r", "-z", "--full-tree", commit).split(b"\0"):
            if not record:
                continue
            metadata, raw_path = record.split(b"\t", 1)
            mode, kind, oid = metadata.decode("ascii").split()
            try:
                path = safe_path(raw_path.decode("utf-8"))
            except UnicodeDecodeError as exc:
                raise BundleError("Non-UTF-8 Git paths are unsupported") from exc
            for parent in [path] + [str(p) for p in PurePosixPath(path).parents if str(p) != "."]:
                folded = parent.casefold()
                if folded in seen and seen[folded] != parent:
                    raise BundleError("Case-colliding archive paths: " + parent + " and " + seen[folded])
                seen[folded] = parent
            if mode == "160000" or kind == "commit":
                raise BundleError("Gitlinks/submodules require explicit source handling; refusing: " + path)
            if mode == "120000":
                raise BundleError("Symlinks are unsupported in release source bundles: " + path)
            if kind != "blob" or mode not in ("100644", "100755"):
                raise BundleError("Unsupported tracked entry: " + path)
            entries.append({"path": path, "mode": mode, "gitBlob": oid})
        # One streaming Git process, not one process or an all-content buffer per file.
        with tempfile.TemporaryFile() as errors:
            process = subprocess.Popen(self.command("cat-file", "--batch"), env=self.env,
                                       stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=errors)
            try:
                for entry in entries:
                    process.stdin.write((entry["gitBlob"] + "\n").encode("ascii"))
                    process.stdin.flush()
                    header = process.stdout.readline().decode("ascii").split()
                    if len(header) != 3 or header[1] != "blob":
                        raise BundleError("Cannot read Git blob: " + entry["path"])
                    size = int(header[2])
                    remaining, digest, prefix = size, hashlib.sha256(), b""
                    while remaining:
                        chunk = process.stdout.read(min(1024 * 1024, remaining))
                        if not chunk:
                            raise BundleError("Truncated Git blob: " + entry["path"])
                        prefix += chunk[:max(0, 128 - len(prefix))]
                        digest.update(chunk)
                        remaining -= len(chunk)
                    if process.stdout.read(1) != b"\n":
                        raise BundleError("Invalid Git blob framing")
                    if prefix.splitlines()[:1] == [b"version https://git-lfs.github.com/spec/v1"]:
                        raise BundleError("Git LFS pointer would omit actual content: " + entry["path"])
                    entry.update(bytes=size, sha256=digest.hexdigest())
                process.stdin.close()
                if process.wait() != 0:
                    raise BundleError("Git blob inspection failed")
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()
                process.stdout.close()
                if not process.stdin.closed:
                    process.stdin.close()
        return {"schemaVersion": 1, "commit": commit, "tree": tree,
                "archivePrefix": "source/", "files": entries}


def validate_review(args, commit):
    for name in ("revision", "reviewed_build_commit"):
        value = getattr(args, name)
        if value is not None and not re.fullmatch(r"(?:[0-9a-f]{40}|[0-9a-f]{64})", value):
            raise BundleError("--" + name.replace("_", "-") + " requires a full lowercase Git commit SHA")
    if args.reviewed_build_commit is not None and args.reviewed_build_commit != commit:
        raise BundleError("Reviewed build commit does not match current HEAD")
    if args.public and (not args.reviewed_build_commit or not args.corresponding_source_reviewed):
        raise BundleError("--public requires --reviewed-build-commit matching HEAD and --corresponding-source-reviewed")


def validate_archive(path, manifest):
    expected = {"source/" + item["path"]: item for item in manifest["files"]}
    with zipfile.ZipFile(path) as archive:
        actual = [entry for entry in archive.infolist() if not entry.is_dir()]
        if len(actual) != len(expected) or {entry.filename for entry in actual} != set(expected):
            raise BundleError("git archive omitted or added tracked files (for example export-ignore attributes)")
        for entry in actual:
            digest = hashlib.sha256()
            with archive.open(entry) as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            if digest.hexdigest() != expected[entry.filename]["sha256"]:
                raise BundleError("git archive changed tracked bytes (for example export-subst): " + entry.filename)


def inventory_files(root):
    """List only ordinary files; never follow symlinks or special files."""
    if root.is_symlink():
        raise BundleError("Symlink inputs are unsupported: " + str(root))
    if not root.is_dir():
        raise BundleError("Expected an existing directory: " + str(root))
    result = []
    seen = set()
    def unreadable(error):
        raise BundleError("Cannot inventory artifact directory: " + str(error))
    for directory, dirs, files in os.walk(root, followlinks=False, onerror=unreadable):
        for name in sorted(dirs + files):
            path = Path(directory) / name
            relative = safe_path(path.relative_to(root).as_posix())
            if relative.casefold() in seen:
                raise BundleError("Case-colliding artifact path: " + relative)
            seen.add(relative.casefold())
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode) or not (stat.S_ISREG(mode) or stat.S_ISDIR(mode)):
                raise BundleError("Symlink/special artifact input is unsupported: " + str(path))
            if stat.S_ISREG(mode):
                result.append((relative, path))
    return sorted(result)


def copy_and_hash(source, destination):
    before = source.stat()
    digest = hashlib.sha256()
    with source.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
            if destination is not None:
                destination.write(chunk)
    after = source.stat()
    if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
        raise BundleError("Input changed while being read: " + str(source))
    return {"bytes": after.st_size, "sha256": digest.hexdigest()}


def add_dependency(source, index, bundle):
    source = Path(source).absolute()
    safe_path(source.name)
    if source.is_symlink():
        raise BundleError("Symlink dependency input is unsupported: " + str(source))
    folder = bundle / "dependencies"
    folder.mkdir(exist_ok=True)
    basename = str(index) + "-" + source.name
    members = []
    if source.is_dir():
        artifact = folder / (basename + ".zip")
        files = inventory_files(source)
        with zipfile.ZipFile(artifact, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for relative, path in files:
                info = zipfile.ZipInfo(relative, date_time=(1980, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | (0o755 if path.stat().st_mode & 0o111 else 0o644)) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                with archive.open(info, "w", force_zip64=True) as stream:
                    evidence = copy_and_hash(path, stream)
                members.append({"path": relative, **evidence})
        if [name for name, _ in inventory_files(source)] != [name for name, _ in files]:
            raise BundleError("Dependency directory changed during packaging: " + str(source))
        kind = "directory-zip"
    elif source.is_file():
        artifact = folder / basename
        with artifact.open("wb") as stream:
            copy_and_hash(source, stream)
        kind = "file"
    else:
        raise BundleError("Dependency artifact is not a regular file or directory: " + str(source))
    return {"inputName": source.name, "kind": kind, "packagePath": artifact.relative_to(bundle).as_posix(),
            "bytes": artifact.stat().st_size, "sha256": sha256_file(artifact), "members": members}


def check_output(git, output, manifest):
    output = output.resolve()
    for metadata in ("--absolute-git-dir", "--git-common-dir"):
        location = Path(git.run("rev-parse", metadata).decode().strip())
        if not location.is_absolute():
            location = git.root / location
        if output.is_relative_to(location.resolve()):
            raise BundleError("Output cannot be inside Git metadata")
    if output.is_relative_to(git.root):
        relative = output.relative_to(git.root).as_posix()
        if relative == "." or any(item["path"] == relative or item["path"].startswith(relative + "/")
                                  for item in manifest["files"]):
            raise BundleError("Output overlaps tracked source")
        result = subprocess.run(git.command("check-ignore", "--quiet", "--no-index", "--", relative + "/"),
                                env=git.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if result.returncode != 0:
            raise BundleError("Output inside the checkout must be ignored by Git")
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        raise BundleError("Output must be absent or an empty directory: " + str(output))
    return output


def run(args):
    git = Git(Path(args.root))
    commit = git.clean_commit(args.revision)
    validate_review(args, commit)
    manifest = git.snapshot(commit)
    if args.check_only:
        with tempfile.TemporaryDirectory(prefix="stack-source-check-") as temporary:
            archive = Path(temporary) / "source.zip"
            git.run("archive", "--format=zip", "--prefix=source/", "--output=" + str(archive), commit)
            validate_archive(archive, manifest)
        git.clean_commit(commit)
        return {"commit": commit, "tree": manifest["tree"], "clean": True,
                "binaryCorrespondence": "maintainer-reviewed" if args.reviewed_build_commit else "unverified",
                "correspondingSourceReviewed": bool(args.corresponding_source_reviewed),
                "limitations": LIMITATIONS}
    if not args.output:
        raise BundleError("--output is required unless --check-only is used")
    if args.public and not args.stage:
        raise BundleError("Public packaging requires --stage containing executable/DLL files")
    output = check_output(git, Path(args.output), manifest)
    for value in ([args.stage] if args.stage else []) + args.dependency_artifact:
        source = Path(value).resolve()
        if output.is_relative_to(source) or source.is_relative_to(output):
            raise BundleError("Output must not overlap a dependency input or staged payload")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Stage outside the repository, so incomplete outputs never pollute a clean check.
    with tempfile.TemporaryDirectory(prefix="stack-source-") as temporary:
        bundle = Path(temporary) / "bundle"
        bundle.mkdir()
        archive = bundle / "source.zip"
        git.run("archive", "--format=zip", "--prefix=source/", "--output=" + str(archive), commit)
        validate_archive(archive, manifest)
        write_json(bundle / "source-manifest.json", manifest)
        dependencies = [add_dependency(item, index, bundle)
                        for index, item in enumerate(args.dependency_artifact, 1)]
        binaries = []
        if args.stage:
            for relative, path in inventory_files(Path(args.stage).absolute()):
                if path.suffix.lower() in (".exe", ".dll"):
                    binaries.append({"path": relative, **copy_and_hash(path, None)})
        if args.public and not binaries:
            raise BundleError("Public packaging requires a nonempty executable/DLL inventory")
        provenance = {
            "schemaVersion": 1, "commit": commit, "tree": manifest["tree"], "cleanCheckout": True,
            "sourceArchive": {"path": "source.zip", "sha256": sha256_file(archive), "bytes": archive.stat().st_size},
            "sourceManifest": {"path": "source-manifest.json", "sha256": sha256_file(bundle / "source-manifest.json")},
            "binaryCorrespondence": "maintainer-reviewed" if args.reviewed_build_commit else "unverified",
            "reviewedBuildCommit": args.reviewed_build_commit,
            "correspondingSourceReview": {"maintainerAsserted": bool(args.corresponding_source_reviewed),
                                           "automaticComplianceProof": False},
            "publicMode": bool(args.public), "dependencyArtifacts": dependencies,
            "stagedBinaries": binaries, "limitations": LIMITATIONS,
        }
        write_json(bundle / "release-provenance.json", provenance)
        sums = [sha256_file(path) + "  " + relative for relative, path in inventory_files(bundle)]
        (bundle / "SHA256SUMS").write_text("\n".join(sums) + "\n", encoding="utf-8")
        git.clean_commit(commit)
        # Copy to a sibling first, allowing an atomic rename even across volumes.
        with tempfile.TemporaryDirectory(prefix=".stack-source-publish-", dir=output.parent) as publishing:
            ready = Path(publishing) / "ready"
            shutil.copytree(bundle, ready)
            # The sibling can be nonignored even when the destination itself is
            # ignored. Do not let our own temporary directory masquerade as dirt.
            if git.run("rev-parse", "--verify", "HEAD^{commit}").decode().strip() != commit:
                raise BundleError("HEAD changed during publication")
            if output.exists():
                output.rmdir()  # Empty only; never silently delete an earlier bundle.
            ready.rename(output)
        git.clean_commit(commit)
    return {"commit": commit, "tree": manifest["tree"], "output": str(output),
            "binaryCorrespondence": provenance["binaryCorrespondence"],
            "sourceArchiveSha256": provenance["sourceArchive"]["sha256"]}


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--root", required=True, help="Exact Git checkout root")
    result.add_argument("--output", help="Absent or empty bundle directory, outside the checkout or Git-ignored")
    result.add_argument("--revision", help="Require this full SHA to remain current HEAD")
    result.add_argument("--check-only", action="store_true", help="Validate source/review gates and print JSON; do not write a bundle")
    result.add_argument("--stage", help="Inventory .exe/.dll files recursively in this staged payload")
    result.add_argument("--dependency-artifact", action="append", default=[], help="Copy a file or deterministically ZIP a directory; repeatable")
    result.add_argument("--reviewed-build-commit", help="Maintainer's full SHA assertion; must equal current HEAD")
    result.add_argument("--corresponding-source-reviewed", action="store_true", help="Record maintainer review assertion, not automatic proof")
    result.add_argument("--public", action="store_true", help="Require both maintainer assertions")
    return result


def main(argv=None):
    try:
        print(json.dumps(run(parser().parse_args(argv)), sort_keys=True))
        return 0
    except (BundleError, OSError, ValueError, zipfile.BadZipFile) as exc:
        print("source-bundle: " + str(exc), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
