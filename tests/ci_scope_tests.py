#!/usr/bin/env python3
"""Exercise scope using real temporary Git history and rejected gate results."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("ci_scope", Path(__file__).resolve().parents[1] / "scripts/ci_scope.py")
scope = importlib.util.module_from_spec(spec)
spec.loader.exec_module(scope)


class ScopeTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ark-ci-scope-")
        self.addCleanup(self.temporary.cleanup)
        self.repository = Path(self.temporary.name)
        self.git("init", "--quiet")
        self.git("config", "user.name", "ARK isolated CI fixture")
        self.git("config", "user.email", "ci-fixture@example.invalid")
        self.write("README.md", "[Guide](docs/GUIDE.md)\n")
        self.write("docs/GUIDE.md", "# Synthetic guide\n")
        self.write("src/runtime.cpp", "// synthetic runtime input\n")
        self.base = self.commit()

    def git(self, *args):
        return subprocess.check_output(["git", "-C", str(self.repository), *args], stderr=subprocess.DEVNULL).decode().strip()

    def write(self, relative, text):
        path = self.repository / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def commit(self):
        self.git("add", "--all")
        self.git("commit", "--quiet", "-m", "isolated fixture")
        return self.git("rev-parse", "HEAD")

    def classify(self, **overrides):
        values = dict(event="pull_request", base=self.base, head=self.git("rev-parse", "HEAD"), ref="refs/pull/7/merge")
        values.update(overrides)
        return scope.classify(self.repository, **values)

    def test_documentation_and_issue_paths_can_skip_native(self):
        for path in ("README.md", "docs/GUIDE.md", ".github/ISSUE_TEMPLATE/first.yml", ".github/FUNDING.yml"):
            self.write(path, "# synthetic documentation change\n")
        self.commit()
        changed, _, paths = self.classify()
        self.assertFalse(changed)
        self.assertEqual(len(paths), 4)

    def test_unknown_and_runtime_inputs_fail_closed(self):
        paths = ("src/runtime.cpp", "include/runtime.hpp", "CMakeLists.txt", "cmake/Runtime.cmake",
                 "scripts/build.sh", "tests/runtime_test.cpp", ".github/workflows/build.yml",
                 "release.json", "assets/ui/icon.png", "LICENSE", "NOTICE", "AGENTS.md",
                 "unrecognized-file", "docs/schemas/contract.json", "docs/THIRD_PARTY_NOTICES.md",
                 "docs/BOM.md", "docs/UPDATES.md", "docs/AGENT_BENCHMARK.md")
        for path in paths:
            with self.subTest(path=path):
                previous = self.git("rev-parse", "HEAD")
                self.write(path, "synthetic changed input\n")
                head = self.commit()
                self.assertTrue(scope.classify(self.repository, "push", previous, head, "refs/heads/main")[0])

    def test_runtime_delete_renamed_into_docs_still_requires_native(self):
        self.git("mv", "src/runtime.cpp", "docs/runtime.md")
        self.commit()
        changed, _, paths = self.classify()
        self.assertTrue(changed)
        self.assertIn("src/runtime.cpp", paths)

    def test_docs_deletion_is_classified_but_validation_detects_broken_link(self):
        (self.repository / "docs/GUIDE.md").unlink()
        self.commit()
        self.assertFalse(self.classify()[0])
        self.assertTrue(scope.validate_docs(self.repository)[1])

    def test_unprovable_events_and_revisions_require_native(self):
        self.write("README.md", "documentation change\n")
        self.commit()
        cases = ({"event": "unknown"}, {"ref": "refs/tags/v0.9.17"},
                 {"base": "0" * 40}, {"base": "f" * 40}, {"base": ""},
                 {"head": self.base}, {"base": "$(echo should-not-execute)"})
        for case in cases:
            with self.subTest(case=case):
                self.assertTrue(self.classify(**case)[0])

    def test_empty_diff_requires_native(self):
        self.assertTrue(self.classify()[0])

    def test_control_character_paths_require_native(self):
        for path in ("docs/line\nbreak.md", "docs/tab\tname.md", "docs/delete\x7f.md"):
            with self.subTest(path=path):
                self.assertFalse(scope.docs_only_path(path))

    def test_filename_shell_text_is_never_executed(self):
        self.write("docs/$(echo literal).md", "# Literal filename\n")
        self.commit()
        self.assertFalse(self.classify()[0])
        self.assertTrue((self.repository / "docs/$(echo literal).md").is_file())

    def test_invalid_yaml_rejects_documentation_validation(self):
        self.write(".github/FUNDING.yml", "custom: [unfinished\n")
        self.commit()
        self.assertTrue(scope.validate_docs(self.repository)[1])

    def test_code_example_links_are_not_treated_as_live_links(self):
        self.write("README.md", "```md\n[Example](missing.md)\n```\n`[Example](missing.md)`\n[Guide](docs/GUIDE.md)\n")
        self.commit()
        self.assertEqual(scope.validate_docs(self.repository)[1], [])


class GateTests(unittest.TestCase):
    def test_full_ci_accepts_only_successful_required_jobs(self):
        self.assertEqual(scope.gate_errors("success", "true", ["success"] * 3), [])
        for result in ("skipped", "failure", "cancelled", "", "unknown"):
            with self.subTest(result=result):
                self.assertTrue(scope.gate_errors("success", "true", ["success", result, "success"]))

    def test_failed_scope_cannot_hide_behind_skipped_runtime_jobs(self):
        for result in ("failure", "skipped", "cancelled", ""):
            with self.subTest(result=result):
                self.assertTrue(scope.gate_errors(result, "false", ["skipped"] * 3))

    def test_docs_scope_still_rejects_failed_jobs_or_missing_classification(self):
        self.assertEqual(scope.gate_errors("success", "false", ["success", "skipped", "success"]), [])
        self.assertTrue(scope.gate_errors("success", "false", ["success", "failure", "success"]))
        for value in ("", "unknown", "TRUE"):
            with self.subTest(value=value):
                self.assertTrue(scope.gate_errors("success", value, ["success"] * 3))


if __name__ == "__main__":
    unittest.main(verbosity=2)
