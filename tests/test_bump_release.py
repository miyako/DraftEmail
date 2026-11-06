"""Release changes use isolated destination copies normalized to a 0.1.0 baseline."""
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import unittest
from unittest.mock import patch
import uuid


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import bump_release
import sign_release


class BumpReleaseTests(unittest.TestCase):
    source_root = ROOT

    def setUp(self):
        self.root = ROOT / (".bump-release-test-" + uuid.uuid4().hex)
        self.root.mkdir()
        self.addCleanup(shutil.rmtree, self.root)
        for name in ("component", "cli", "tests/host"):
            shutil.copytree(self.source_root / name, self.root / name,
                            ignore=shutil.ignore_patterns(
                                "DerivedData", "userPreferences.*", "CompilerIntermediateFiles",
                                "Libraries", "bin", "__pycache__"))
        for name in ("template.json", "package.json", "README.md", "release-policy.json"):
            shutil.copy2(self.source_root / name, self.root / name)
        (self.root / "docs").mkdir()
        shutil.copy2(self.source_root / "docs/PROTOCOL.md", self.root / "docs/PROTOCOL.md")
        shutil.copy2(self.source_root / "tests/test_cli.py", self.root / "tests/test_cli.py")
        shutil.copy2(self.source_root / "tests/cli-contract.json", self.root / "tests/cli-contract.json")
        self.git("init", "-q")
        self.git("config", "user.name", "Release fixture")
        self.git("config", "user.email", "release-fixture@example.invalid")
        self.git("remote", "add", "origin", "https://github.com/miyako/DraftEmail.git")
        self.git("commit", "-q", "--allow-empty", "-m", "fixture")
        self.environment = patch.dict(os.environ, {"GITHUB_REPOSITORY": "miyako/DraftEmail"})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        # Validate the copied live surfaces before resetting only their registered versions.
        bump_release.apply(self.root, "0.1.0")

    def git(self, *args):
        return subprocess.run(["git", *args], cwd=self.root, check=True,
                              capture_output=True, text=True, encoding="utf-8").stdout.strip()

    def snapshot(self):
        return {str(p.relative_to(self.root)): p.read_bytes()
                for p in self.root.rglob("*") if p.is_file() and ".git" not in p.parts}

    def replace_fixture_text(self, relative, old, new):
        path = self.root / relative
        text = path.read_text()
        self.assertIn(old, text, relative)
        changed = text.replace(old, new)
        self.assertNotEqual(text, changed, relative)
        path.write_text(changed)

    def test_destination_cpp_metadata_synchronization_and_idempotency(self):
        config = json.loads((self.root / "template.json").read_text())
        self.assertTrue(config["initialized"])
        self.assertEqual((config["repository"], config["namespace"], config["feature_class"],
                          config["cli"], config["language"]),
                         ("DraftEmail", "DraftEmail", "Composer", "draft-email-cli", "cpp"))
        self.assertFalse((self.root / "cli-starters").exists())
        before = self.snapshot()
        result = bump_release.apply(self.root, "1.2.3")
        changed = sorted(name for name, value in self.snapshot().items() if before[name] != value)
        self.assertEqual(result["changed_paths"], changed)
        self.assertEqual(result["tag"], "1.2.3")
        required = {
            "template.json", "package.json", "cli/main.cpp", "README.md", "docs/PROTOCOL.md",
            "component/DraftEmail/Documentation/Classes/Composer.md",
            "tests/host/Project/Sources/Methods/Smoke.4dm",
            "tests/host/contract.json", "tests/test_cli.py",
        }
        self.assertTrue(required.issubset(changed), changed)
        for name in ("template.json", "package.json"):
            self.assertEqual(json.loads((self.root / name).read_text())["version"], "1.2.3")
        self.assertRegex((self.root / "cli/main.cpp").read_text(), r'kVersion\s*=\s*"1\.2\.3"')
        self.assertIn('"version":"1.2.3"', (self.root / "tests/test_cli.py").read_text())
        self.assertIn('"version":"1.2.3"', (self.root / "docs/PROTOCOL.md").read_text())
        host_contract = json.loads((self.root / "tests/host/contract.json").read_text())
        assertions = {item["path"]: item for item in host_contract["feature"]["assertions"]}
        self.assertEqual(json.loads(assertions["/version/stdout"]["equals"]),
                         {"protocol": 1, "version": "1.2.3"})
        self.assertEqual(assertions["/version/value"]["equals"],
                         {"protocol": 1, "version": "1.2.3"})
        contract = json.loads((self.root / "tests/cli-contract.json").read_text())
        version_case = next(case for case in contract["cases"] if case["args"] == ["--version"])
        self.assertEqual(version_case["stdout"], {"protocol": 1, "version": "$VERSION"})
        _, plan = bump_release.plan(self.root, "1.2.3")
        self.assertEqual(plan, {})
        snapshot = self.snapshot()
        self.assertEqual(bump_release.apply(self.root, "1.2.3")["changed_paths"], [])
        self.assertEqual(snapshot, self.snapshot())

    def test_next_highest_stable_numeric_tag_and_modes(self):
        for tag in ("1.9.9", "1.10.2", "1.10.1", "20.0.0-rc.1", "9.0.0+build",
                    "01.0.0", "v01.0.0", "v20.0.0-rc.1", "v9.0.0+build", "20R3.2", "unrelated"):
            self.git("tag", tag)
        before = self.snapshot()
        for mode, version in (("patch", "1.10.3"), ("minor", "1.11.0"), ("major", "2.0.0")):
            self.assertEqual(bump_release.next_version(self.root, mode),
                             {"version": version, "tag": version})
        self.assertEqual(before, self.snapshot())

    def test_legacy_and_mixed_tag_history_uses_highest_baseline_without_rewriting(self):
        for tag in ("v1.10.2", "1.9.9"):
            self.git("tag", tag)
        self.assertEqual(bump_release.next_version(self.root, "patch"),
                         {"version": "1.10.3", "tag": "1.10.3"})
        self.git("tag", "1.11.0")
        before = self.git("show-ref", "--tags")
        self.assertEqual(bump_release.next_version(self.root, "patch"),
                         {"version": "1.11.1", "tag": "1.11.1"})
        self.assertEqual(self.git("show-ref", "--tags"), before)
        self.assertEqual(bump_release.next_version(self.root, "minor", tags=["v2.3.4"]),
                         {"version": "2.4.0", "tag": "2.4.0"})

    def test_published_draftemail_legacy_tag_plans_only_a_new_bare_version(self):
        bump_release.apply(self.root, "0.1.3")
        self.git("tag", "v0.1.3")
        before = self.snapshot()
        tags = self.git("show-ref", "--tags")
        policy = (self.root / "release-policy.json").read_bytes()
        self.assertEqual(bump_release.next_version(self.root, "patch"),
                         {"version": "0.1.4", "tag": "0.1.4"})
        self.assertEqual(self.git("show-ref", "--tags"), tags)
        self.assertEqual(before, self.snapshot())
        self.assertEqual((self.root / "release-policy.json").read_bytes(), policy)

    def test_no_stable_tag_bumps_configured_base(self):
        bump_release.apply(self.root, "3.4.5")
        for tag in ("99.0.0-preview", "v99.0.0-preview", "20R3.2"):
            self.git("tag", tag)
        for mode, version in (("patch", "3.4.6"), ("minor", "3.5.0"), ("major", "4.0.0")):
            self.assertEqual(bump_release.next_version(self.root, mode)["version"], version)

    def test_retry_equal_version_validates_all_surfaces_and_does_not_bump_again(self):
        bump_release.apply(self.root, "1.2.4")
        for tags in (["1.2.3"], ["v1.2.3"], ["1.2.3", "v1.2.3", "2.0.0-rc.1"]):
            self.assertEqual(bump_release.next_version(self.root, "patch", tags=tags),
                             {"version": "1.2.4", "tag": "1.2.4"})
        self.replace_fixture_text("tests/host/contract.json",
                                  '\\"version\\":\\"1.2.4\\"', '\\"version\\":\\"1.2.3\\"')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            bump_release.next_version(self.root, "patch", tags=["v1.2.3"])
        self.assertEqual(before, self.snapshot())

    def test_next_refuses_configured_version_ahead_of_computed_release(self):
        bump_release.apply(self.root, "3.4.5")
        before = self.snapshot()
        for mode in ("patch", "minor", "major"):
            with self.subTest(mode=mode), self.assertRaisesRegex(ValueError, "ahead"):
                bump_release.next_version(self.root, mode, tags=["v1.2.3"])
        self.assertEqual(before, self.snapshot())

    def test_empty_optional_feature_version_is_rejected_on_equal_retry(self):
        feature = self.root / "component/DraftEmail/Project/Sources/Classes/Composer.4dm"
        feature.write_text(feature.read_text() + '\nFunction fixtureVersion()\n\tThis.version:=""\n')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            bump_release.apply(self.root, "0.1.0", stage=True)
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            bump_release.next_version(self.root, "patch", tags=["v0.0.9"])
        self.assertEqual(before, self.snapshot())
        self.assertEqual(self.git("diff", "--cached", "--name-only"), "")

    def test_stage_adds_only_changed_paths_and_empty_retry_is_noop(self):
        self.git("add", "--", ".")
        self.git("commit", "-q", "-m", "initialized fixture")
        unrelated = self.root / "unrelated.txt"
        unrelated.write_text("must remain untracked\n")
        result = bump_release.apply(self.root, "0.2.0", stage=True)
        staged = self.git("diff", "--cached", "--name-only").splitlines()
        self.assertEqual(staged, result["changed_paths"])
        self.assertNotIn("unrelated.txt", staged)
        self.git("commit", "-q", "-m", "bump fixture")
        self.assertEqual(bump_release.apply(self.root, "0.2.0", stage=True)["changed_paths"], [])
        self.assertEqual(self.git("diff", "--cached", "--name-only"), "")
        self.assertEqual(unrelated.read_text(), "must remain untracked\n")

    def test_stage_validation_failure_neither_writes_nor_stages(self):
        self.git("add", "--", ".")
        self.git("commit", "-q", "-m", "initialized fixture")
        self.replace_fixture_text("tests/host/contract.json",
                                  '\\"version\\":\\"0.1.0\\"', '\\"version\\":\\"0.0.1\\"')
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "inconsistent"):
            bump_release.apply(self.root, "0.2.0", stage=True)
        self.assertEqual(before, self.snapshot())
        self.assertEqual(self.git("diff", "--cached", "--name-only"), "")

    def test_uninitialized_original_and_exact_destination_guards(self):
        metadata = self.root / "template.json"
        configured = metadata.read_text()
        self.replace_fixture_text("template.json", '"initialized": true', '"initialized": false')
        before = self.snapshot()
        with self.assertRaises(ValueError):
            bump_release.apply(self.root, "1.0.0")
        self.assertEqual(before, self.snapshot())
        metadata.write_text(configured)
        before = self.snapshot()
        for repository in ("miyako/draftemail", "other/DraftEmail", "miyako/Wrong"):
            with self.subTest(repository=repository), self.assertRaises(ValueError):
                bump_release.apply(self.root, "1.0.0", repository)
            self.assertEqual(before, self.snapshot())
        self.git("remote", "set-url", "origin", "https://github.com/miyako/4d-component-template.git")
        with patch.dict(os.environ, {"GITHUB_REPOSITORY": "miyako/4d-component-template"}):
            with self.assertRaisesRegex(ValueError, "original template"):
                bump_release.apply(self.root, "1.0.0")

    def test_invalid_new_versions_make_no_changes(self):
        before = self.snapshot()
        for version in ("v1.0.0", "01.2.3", "1.2", "1.2.3\n", "1.2.3-beta", "1.2.3+build", None):
            with self.subTest(version=version), self.assertRaises(ValueError):
                bump_release.apply(self.root, version)
            self.assertEqual(before, self.snapshot())

    def test_inconsistent_or_malformed_surfaces_fail_before_writes(self):
        targets = {
            "template.json": ('"version": "0.1.0"', '"version": "not-semver"'),
            "package.json": ('"version": "0.1.0"', '"version": "0.2.0"'),
            "cli/main.cpp": ('"0.1.0"', '"bad"'),
            "tests/host/Project/Sources/Methods/Smoke.4dm": ('"0.1.0"', '"0.3.0"'),
            "tests/host/contract.json": ('\\"version\\":\\"0.1.0\\"', '\\"version\\":\\"0.8.0\\"'),
            "README.md": ('"version":"0.1.0"', '"version":"0.9.0"'),
            "docs/PROTOCOL.md": ('"version":"0.1.0"', '"version":"0.9.0"'),
            "component/DraftEmail/Documentation/Classes/Composer.md":
                ('"0.1.0"', '"0.9.0"'),
            "tests/test_cli.py": ('"version":"0.1.0"', '"version":"bad"'),
        }
        for relative, (old, new) in targets.items():
            path = self.root / relative
            text = path.read_text()
            self.replace_fixture_text(relative, old, new)
            before = self.snapshot()
            with self.subTest(relative=relative), self.assertRaises(ValueError):
                bump_release.apply(self.root, "1.2.3")
            self.assertEqual(before, self.snapshot())
            path.write_text(text)

    def test_preserves_history_policy_dependencies_and_unrelated_versions(self):
        readme = self.root / "README.md"
        original = readme.read_text()
        history = ('\nHistorical release v0.1.0; dependency 0.1.0\n'
                   'Historical response {"protocol":1,"version":"0.1.0"}\n')
        readme.write_text(original + history)
        protocol_doc = self.root / "docs/PROTOCOL.md"
        original_protocol = protocol_doc.read_text()
        protocol_doc.write_text(original_protocol + history)
        cmake = self.root / "cli/CMakeLists.txt"
        dependencies = '\nset(THIRD_PARTY_VERSION "0.1.0")\n'
        cmake.write_text(cmake.read_text() + dependencies)
        source = self.root / "cli/main.cpp"
        source.write_text(source.read_text() + '\n// historical version 0.1.0\n'
                          'constexpr auto dependencyVersion = "0.1.0";\n')
        policy = self.root / "release-policy.json"
        policy.write_text('{"schema":1,"apple_notarized_versions":["0.1.0"]}\n')
        bump_release.apply(self.root, "0.2.0")
        after = readme.read_text()
        # Only the live CLI response example changes, not release commands,
        # upstream links, dependency-manager examples or historical observations.
        expected = "".join(
            line.replace('"version":"0.1.0"', '"version":"0.2.0"')
            if line.startswith("|") and "`draft-email-cli --version`" in line else line
            for line in original.splitlines(keepends=True))
        self.assertEqual(after, expected + history)
        self.assertEqual(protocol_doc.read_text(),
                         original_protocol.replace('"version":"0.1.0"', '"version":"0.2.0"') + history)
        self.assertIn(dependencies, cmake.read_text())
        self.assertIn('// historical version 0.1.0', source.read_text())
        self.assertIn('dependencyVersion = "0.1.0"', source.read_text())
        self.assertIn('["0.1.0"]', policy.read_text())
        self.assertEqual(json.loads((self.root / "package.json").read_text())["tool"]["version"], "20.7")

    def test_legacy_destination_without_optional_protocol_document(self):
        (self.root / "docs/PROTOCOL.md").unlink()
        (self.root / "docs").rmdir()
        result = bump_release.apply(self.root, "0.2.0")
        self.assertNotIn("docs/PROTOCOL.md", result["changed_paths"])
        self.assertFalse((self.root / "docs").exists())
        self.assertEqual(bump_release.apply(self.root, "0.2.0")["changed_paths"], [])

    def test_protocol_document_supports_only_current_version_command_rows(self):
        protocol_doc = self.root / "docs/PROTOCOL.md"
        original = protocol_doc.read_text()
        extra = ('\n| `draft-email-cli --version` | stdout '
                 '`{"protocol":1,"version":"0.1.0"}\\n` |\n')
        unrelated = ('| `other-cli --version` | '
                     '`{"protocol":1,"version":"8.9.0"}\\n` |\n'
                     'Dependency version 0.1.0, historical release v0.1.0.\n')
        protocol_doc.write_text(original + extra + unrelated)
        bump_release.apply(self.root, "0.2.0")
        self.assertEqual(protocol_doc.read_text(),
                         (original + extra).replace('"version":"0.1.0"', '"version":"0.2.0"')
                         + unrelated)

    def test_present_protocol_document_requires_registered_version_row(self):
        self.replace_fixture_text("docs/PROTOCOL.md", "`--version`", "`obsolete-version`")
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "missing supported component version surface"):
            bump_release.apply(self.root, "0.2.0", stage=True)
        self.assertEqual(before, self.snapshot())
        self.assertEqual(self.git("diff", "--cached", "--name-only"), "")

    def test_symlink_protocol_document_fails_without_writes_or_staging(self):
        protocol_doc = self.root / "docs/PROTOCOL.md"
        protocol_doc.unlink()
        protocol_doc.symlink_to(self.root / "README.md")
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "regular non-symlink file"):
            bump_release.apply(self.root, "0.2.0", stage=True)
        self.assertEqual(before, self.snapshot())
        self.assertEqual(self.git("diff", "--cached", "--name-only"), "")

    def test_no_stage_preserves_existing_index_and_repository_tags(self):
        self.git("add", "--", ".")
        self.git("commit", "-q", "-m", "initialized fixture")
        unrelated = self.root / "unrelated.txt"
        unrelated.write_text("already staged\n")
        self.git("add", "--", "unrelated.txt")
        index = self.git("diff", "--cached", "--binary")
        tags = self.git("tag", "--list")
        result = bump_release.apply(self.root, "2.3.4")
        self.assertTrue(result["changed_paths"])
        self.assertEqual(self.git("diff", "--cached", "--binary"), index)
        self.assertEqual(self.git("tag", "--list"), tags)

    def test_missing_cpp_version_constant_fails_without_writes(self):
        self.replace_fixture_text("cli/main.cpp", "constexpr auto kVersion",
                                  "constexpr auto unregisteredVersion")
        before = self.snapshot()
        with self.assertRaisesRegex(ValueError, "missing supported CLI version"):
            bump_release.apply(self.root, "2.3.4")
        self.assertEqual(before, self.snapshot())

    def test_cpp_constant_and_cmake_project_version(self):
        source = self.root / "cli/main.cpp"
        cmake = self.root / "cli/CMakeLists.txt"
        self.replace_fixture_text("cli/CMakeLists.txt", "project(draft-email-cli LANGUAGES CXX)",
                                  "project(draft-email-cli VERSION 0.1.0 LANGUAGES CXX)")
        cmake.write_text(cmake.read_text() + '\nset(CLI_VERSION "0.1.0")\n')
        bump_release.apply(self.root, "2.3.4")
        self.assertRegex(source.read_text(), r'kVersion\s*=\s*"2\.3\.4"')
        self.assertIn("project(draft-email-cli VERSION 2.3.4 LANGUAGES CXX)", cmake.read_text())
        self.assertIn('set(CLI_VERSION "2.3.4")', cmake.read_text())
        self.assertIn("VERSION 3.16", cmake.read_text())

    def test_malformed_contract_and_duplicate_metadata_fail_without_writes(self):
        for relative, edit in (
                ("template.json", lambda text: text.replace('"version": "0.1.0"',
                                                           '"version": "0.1.0", "version": "0.1.0"')),
                ("tests/host/contract.json", lambda text: text.replace(
                    '\\"protocol\\":1,\\"version\\":\\"0.1.0\\"', 'not json')),
                ("tests/cli-contract.json", lambda text: text.replace(
                    '"version": "$VERSION"', '"version": "bad"'))):
            path = self.root / relative
            text = path.read_text()
            changed = edit(text)
            self.assertNotEqual(text, changed, relative)
            path.write_text(changed)
            before = self.snapshot()
            with self.subTest(relative=relative), self.assertRaises(ValueError):
                bump_release.apply(self.root, "2.3.4")
            self.assertEqual(before, self.snapshot())
            path.write_text(text)

    def test_cli_outputs_json_changed_paths_and_github_outputs(self):
        output = self.root / "github-output"
        env = dict(os.environ, GITHUB_OUTPUT=str(output))
        command = [sys.executable, str(ROOT / "scripts/bump_release.py"), "--root", str(self.root)]
        next_result = subprocess.run(command + ["next", "--mode", "minor"], env=env,
                                     capture_output=True, text=True, check=True)
        self.assertEqual(json.loads(next_result.stdout)["version"], "0.2.0")
        applied = subprocess.run(command + ["apply", "--version", "0.2.0", "--stage"], env=env,
                                 capture_output=True, text=True, check=True)
        result = json.loads(applied.stdout)
        self.assertTrue(result["changed_paths"])
        lines = output.read_text().splitlines()
        self.assertIn("version=0.2.0", lines)
        self.assertIn("tag=0.2.0", lines)
        self.assertFalse(any(line.startswith("tag=v") for line in lines))
        self.assertEqual(json.loads(next(line.split("=", 1)[1] for line in lines
                                       if line.startswith("changed_paths="))), result["changed_paths"])
        self.assertEqual(self.git("diff", "--cached", "--name-only").splitlines(), result["changed_paths"])

    def test_missing_and_symlink_surfaces_fail_without_writes(self):
        source = self.root / "cli/main.cpp"
        content = source.read_bytes()
        source.unlink()
        before = self.snapshot()
        with self.assertRaises(ValueError):
            bump_release.apply(self.root, "1.0.0")
        self.assertEqual(before, self.snapshot())
        source.symlink_to(self.root / "README.md")
        before = self.snapshot()
        with self.assertRaises(ValueError):
            bump_release.apply(self.root, "1.0.0")
        self.assertEqual(before, self.snapshot())
        source.unlink()
        source.write_bytes(content)


class BumpedSourceFixtureTests(unittest.TestCase):
    def fixture(self):
        fixture = BumpReleaseTests()
        self.addCleanup(fixture.doCleanups)
        fixture.setUp()
        return fixture

    def test_release_suite_after_source_version_bumps(self):
        fixture = self.fixture()
        for version in ("0.1.1", "2.3.4"):
            with self.subTest(version=version):
                bump_release.apply(fixture.root, version)
                before = fixture.snapshot()
                with patch.object(BumpReleaseTests, "source_root", fixture.root):
                    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BumpReleaseTests)
                    result = unittest.TestResult()
                    suite.run(result)
                self.assertEqual(result.testsRun, suite.countTestCases())
                self.assertTrue(result.wasSuccessful(), result.failures + result.errors)
                self.assertEqual(before, fixture.snapshot())

    def test_patch_after_unpublished_0_1_1_requires_apple_signing(self):
        fixture = self.fixture()
        bump_release.apply(fixture.root, "0.1.1")
        self.assertEqual(fixture.git("tag", "--list"), "")
        candidate = bump_release.next_version(fixture.root, "patch")
        self.assertEqual(candidate, {"version": "0.1.2", "tag": "0.1.2"})
        policy = (fixture.root / "release-policy.json").read_bytes()
        bump_release.apply(fixture.root, candidate["version"])
        self.assertEqual((fixture.root / "release-policy.json").read_bytes(), policy)
        output = fixture.root / "github-output"
        with contextlib.redirect_stdout(io.StringIO()) as text:
            self.assertTrue(sign_release.policy_command(
                fixture.root, {"GITHUB_OUTPUT": str(output)}))
        self.assertEqual(text.getvalue(), "apple-notarized\n")
        self.assertEqual(output.read_text(), "apple_signing=true\n")


if __name__ == "__main__":
    unittest.main()
