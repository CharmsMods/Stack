"""Bounded, offline tests with disposable Git repositories (no real release build)."""

import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import source_bundle as bundle


class SourceBundleTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.base = Path(self.temporary.name)
        self.root = self.base / "repo"
        self.root.mkdir()
        self.env = os.environ.copy()
        for key in list(self.env):
            if key.startswith("GIT_"):
                del self.env[key]
        self.env.update(GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
        self.git("init", "--template=")
        self.git("config", "user.name", "Bundle Test")
        self.git("config", "user.email", "bundle-test@example.invalid")
        for name, contents in {
            "README.md": b"Fixture source\n", ".gitignore": b"ignored/\n",
            "tools/build.ps1": b"# Build instructions\n", "docs/LICENSE.txt": b"Fixture license\n",
            "assets/icon.bin": bytes(range(256)), "src/main.cpp": b"int main() {return 0;}\n",
        }.items():
            self.write(name, contents)
        self.commit()
        self.head = self.git("rev-parse", "HEAD").strip()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args], env=self.env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout.decode()

    def write(self, relative, data):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def commit(self):
        self.git("add", "--all")
        self.git("commit", "-m", "Fixture")

    def run_bundle(self, *extra, output=None):
        arguments = ["--root", str(self.root)]
        if "--check-only" not in extra:
            arguments += ["--output", str(output or self.base / "bundle")]
        return bundle.run(bundle.parser().parse_args(arguments + list(extra)))

    def assert_rejected(self, phrase, *args, **kwargs):
        with self.assertRaisesRegex(bundle.BundleError, phrase):
            self.run_bundle(*args, **kwargs)

    def stage(self):
        stage = self.base / "stage"
        stage.mkdir()
        (stage / "Stack.exe").write_bytes(b"fixture executable")
        (stage / "helper.DLL").write_bytes(b"fixture DLL")
        (stage / "ignore.txt").write_text("not inventoried")
        return stage

    def test_clean_full_source_and_manifest_hashes(self):
        result = self.run_bundle()
        output = Path(result["output"])
        manifest = json.loads((output / "source-manifest.json").read_text())
        provenance = json.loads((output / "release-provenance.json").read_text())
        self.assertEqual(result["commit"], self.head)
        self.assertEqual(result["tree"], self.git("rev-parse", "HEAD^{tree}").strip())
        self.assertEqual(provenance["binaryCorrespondence"], "unverified")
        self.assertFalse(provenance["correspondingSourceReview"]["automaticComplianceProof"])
        self.assertIn("ignored", " ".join(provenance["limitations"]))
        with zipfile.ZipFile(output / "source.zip") as archive:
            for item in manifest["files"]:
                actual = archive.read("source/" + item["path"])
                self.assertEqual(actual, (self.root / item["path"]).read_bytes())
                self.assertEqual(item["sha256"], hashlib.sha256(actual).hexdigest())
        self.assertTrue({"tools/build.ps1", "docs/LICENSE.txt", "assets/icon.bin"}
                        <= {item["path"] for item in manifest["files"]})
        for line in (output / "SHA256SUMS").read_text().splitlines():
            expected, relative = line.split("  ", 1)
            self.assertEqual(expected, bundle.sha256_file(output / relative))
        self.assertEqual(provenance["sourceManifest"]["sha256"], bundle.sha256_file(output / "source-manifest.json"))

    def test_dirty_tracked_rejected(self):
        self.write("README.md", b"changed\n")
        self.assert_rejected("not clean")

    def test_staged_rejected(self):
        self.write("README.md", b"changed\n")
        self.git("add", "README.md")
        self.assert_rejected("not clean")

    def test_untracked_rejected(self):
        self.write("new-source.cpp", b"not committed\n")
        self.assert_rejected("not clean")

    def test_ignored_inputs_not_claimed(self):
        self.write("ignored/build-secret", b"not source\n")
        self.run_bundle(output=self.root / "ignored" / "bundle")
        self.assertEqual(self.git("status", "--porcelain"), "")

    def test_nonignored_output_rejected_before_creation(self):
        output = self.root / "bundle"
        self.assert_rejected("must be ignored", output=output)
        self.assertFalse(output.exists())

    def test_existing_bundle_not_overwritten(self):
        self.run_bundle()
        self.assert_rejected("absent or an empty")

    def test_moved_revision_rejected(self):
        self.write("README.md", b"new commit\n")
        self.commit()
        self.assert_rejected("Expected revision", "--revision", self.head, "--check-only")

    def test_wrong_review_sha_rejected_even_nonpublic(self):
        self.assert_rejected("Reviewed build commit", "--reviewed-build-commit", "0" * 40)

    def test_public_attestations_and_stage_required(self):
        self.assert_rejected("requires --reviewed-build-commit", "--public", "--check-only")
        self.assert_rejected("requires --reviewed-build-commit", "--public", "--check-only",
                             "--reviewed-build-commit", self.head)
        flags = ("--public", "--reviewed-build-commit", self.head, "--corresponding-source-reviewed")
        checked = self.run_bundle(*flags, "--check-only")
        self.assertEqual(checked["commit"], self.head)
        self.assert_rejected("requires --stage", *flags)
        empty = self.base / "empty-stage"
        empty.mkdir()
        self.assert_rejected("nonempty", *flags, "--stage", str(empty))
        result = self.run_bundle(*flags, "--stage", str(self.stage()))
        provenance = json.loads((Path(result["output"]) / "release-provenance.json").read_text())
        self.assertEqual(provenance["binaryCorrespondence"], "maintainer-reviewed")
        self.assertEqual({item["path"] for item in provenance["stagedBinaries"]}, {"Stack.exe", "helper.DLL"})
        self.assertEqual(provenance["stagedBinaries"][0]["sha256"], hashlib.sha256(b"fixture executable").hexdigest())

    def test_lfs_pointer_rejected(self):
        self.write("assets/large.dat", b"version https://git-lfs.github.com/spec/v1\noid sha256:" + b"0" * 64 + b"\nsize 100\n")
        self.commit()
        self.assert_rejected("Git LFS pointer", "--check-only")

    def test_gitlink_rejected(self):
        self.git("update-index", "--add", "--cacheinfo", "160000," + self.head + ",vendor/dependency")
        self.git("commit", "-m", "Add gitlink")
        (self.root / "vendor" / "dependency").mkdir(parents=True)
        self.assert_rejected("Gitlinks/submodules", "--check-only")

    @unittest.skipUnless(hasattr(os, "symlink"), "Requires symlink support")
    def test_symlink_rejected(self):
        try:
            (self.root / "link").symlink_to("README.md")
        except OSError:
            self.skipTest("OS denied fixture symlink creation")
        self.commit()
        self.assert_rejected("Symlinks are unsupported", "--check-only")

    def test_export_ignore_rejected(self):
        self.write(".gitattributes", b"docs/ export-ignore\n")
        self.commit()
        self.assert_rejected("omitted or added", "--check-only")

    def test_export_subst_rejected(self):
        self.write(".gitattributes", b"README.md export-subst\n")
        self.write("README.md", b"$Format:%H$\n")
        self.commit()
        self.assert_rejected("changed tracked bytes", "--check-only")

    def test_archive_deterministic_for_same_commit(self):
        a = self.run_bundle(output=self.base / "a")
        b = self.run_bundle(output=self.base / "b")
        self.assertEqual(a["sourceArchiveSha256"], b["sourceArchiveSha256"])
        self.assertEqual((self.base / "a" / "source.zip").read_bytes(), (self.base / "b" / "source.zip").read_bytes())

    def test_dependency_file_and_directory_packaged_with_hashes(self):
        source_dir = self.base / "libavoid-source"
        source_dir.mkdir()
        (source_dir / "source.cpp").write_bytes(b"dependency source\n")
        artifact = self.base / "dependency.tar.gz"
        artifact.write_bytes(b"fixture source archive")
        flags = ("--dependency-artifact", str(artifact), "--dependency-artifact", str(source_dir))
        a = self.run_bundle(*flags, output=self.base / "a")
        b = self.run_bundle(*flags, output=self.base / "b")
        evidence = json.loads((Path(a["output"]) / "release-provenance.json").read_text())["dependencyArtifacts"]
        self.assertEqual(len(evidence), 2)
        for entry in evidence:
            packaged = Path(a["output"]) / entry["packagePath"]
            self.assertTrue(packaged.is_file())
            self.assertEqual(bundle.sha256_file(packaged), entry["sha256"])
            self.assertEqual(packaged.read_bytes(), (Path(b["output"]) / entry["packagePath"]).read_bytes())
        with zipfile.ZipFile(Path(a["output"]) / evidence[1]["packagePath"]) as archive:
            self.assertEqual(archive.read("source.cpp"), b"dependency source\n")
        self.assertEqual(evidence[1]["members"][0]["sha256"], hashlib.sha256(b"dependency source\n").hexdigest())

    def test_output_input_overlap_rejected(self):
        stage = self.stage()
        self.assert_rejected("must not overlap", "--stage", str(stage), output=stage / "source-bundle")
        self.assert_rejected("must not overlap", "--dependency-artifact", str(stage), output=stage / "source-bundle")

    def test_assume_unchanged_rejected(self):
        self.git("update-index", "--assume-unchanged", "README.md")
        self.write("README.md", b"hidden change\n")
        self.assert_rejected("assume-unchanged", "--check-only")

    def test_external_clean_filter_not_executed(self):
        marker = self.base / "filter-executed"
        self.git("config", "filter.fixture.clean", "touch '" + str(marker) + "'; cat")
        # Attribute is configured after the commit, so setup itself never runs it.
        (self.root / ".git" / "info").mkdir(exist_ok=True)
        (self.root / ".git" / "info" / "attributes").write_text("README.md filter=fixture\n")
        self.write("README.md", b"changed\n")
        self.assert_rejected("not clean", "--check-only")
        self.assertFalse(marker.exists())

    def test_check_only_cli_json_stdout(self):
        result = subprocess.run([sys.executable, str(Path(bundle.__file__)), "--root", str(self.root),
                                 "--check-only"], env=self.env, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["commit"], self.head)
        self.assertEqual(result.stderr, "")

    def test_unsafe_paths(self):
        for value in ("../escape", "/absolute", "C:/drive", "dir\\file", "dir/CON.txt", "a/../b", "a\nfile", "a."):
            with self.subTest(value=value), self.assertRaises(bundle.BundleError):
                bundle.safe_path(value)


if __name__ == "__main__":
    unittest.main()
