"""Regression checks for CI selection and release safety (no third-party packages)."""
import importlib.util
from pathlib import Path
import unittest
import os
import tempfile
import sys
from unittest.mock import patch

sys.path.insert(0, str(ROOT_DIR := Path(__file__).resolve().parent))
import package as packager

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("policy", ROOT / "tools/ci/policy.py")
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)


class PolicyTests(unittest.TestCase):
    def test_docs_skip_windows_but_unknown_files_do_not(self):
        self.assertEqual(policy.profile(["README.md", "docs/TODAY-38.md"]), "docs")
        for files in ([], ["new-tool.py"], ["docs/helper.py"], ["assets/icon.svg"],
                      [".github/workflows/windows-build.yml"], ["CMakeLists.txt"],
                      ["README.md", "src/WorkspaceModel.h"], ["tests/NewTests.cpp"]):
            self.assertEqual(policy.profile(files), "full", files)

    def test_ui_selection_is_a_narrow_allowlist(self):
        self.assertEqual(policy.profile(["src/TaskTree.cpp", "docs/UX.md"]), "ui")
        self.assertEqual(policy.profile(["src/TaskTree.cpp", "src/WorkspaceCommands.h"]), "full")
        self.assertEqual(policy.profile(["src/HubApplication.cpp"]), "full")

    def test_candidate_and_release_always_require_full_tests(self):
        for mode in ("candidate", "release"):
            for paths in (["README.md"], ["src/TaskTree.cpp"]):
                plan = policy.plan(paths, "workflow_dispatch", mode, "refs/heads/main")
                self.assertEqual(plan["profile"], "full")
                self.assertTrue(plan["package"])
        self.assertEqual(policy.plan(["README.md"], "push", "check", "refs/heads/main")["profile"], "full")
        self.assertFalse(policy.plan(["src/TaskTree.cpp"], "pull_request", "check", "refs/pull/1/merge")["package"])

    def test_manual_check_cannot_skip_earlier_branch_commits(self):
        self.assertEqual(policy.plan(["README.md"], "workflow_dispatch", "check",
                                     "refs/heads/feature")["profile"], "full")

    def test_package_requires_every_test_passed(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        names = policy.inventory(cmake)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            (root / "CMakeLists.txt").write_text(cmake, encoding="utf-8")
            build = root / "build"
            build.mkdir()
            executable = build / "ArrangerManagerDesktop_artefacts/Release/Arranger Manager.exe"
            executable.parent.mkdir(parents=True)
            executable.write_bytes(b"fixture-not-a-real-executable")
            (root / "docs").mkdir()
            for filename in ("TEMPLATES-67.md", "PORTABILITY-68.md", "TODAY-38.md",
                             "LEGACY-RETIREMENT-7.md", "WORKSPACE-0.1e-DECISIONS.md"):
                (root / "docs" / filename).write_text("fixture", encoding="utf-8")
            report = build / "test-results.xml"
            good = ''.join(f'<testcase name="{n}" status="run" />' for n in names)
            with patch.object(packager, "ROOT", root), patch.dict(os.environ, {
                    "CI_VERSION": policy.version(cmake), "CI_PACKAGE": "true",
                    "GITHUB_SHA": "a" * 40, "GITHUB_RUN_ID": "123"}):
                for cases in (good.replace('status="run"', 'status="fail"', 1),
                              good.replace('status="run"', 'status="notrun"', 1),
                              good.replace('status="run"', 'status="disabled"', 1),
                              good.replace('/>', '><failure /></testcase>', 1),
                              good.replace('/>', '><skipped /></testcase>', 1),
                              good.replace(f'<testcase name="{names[0]}" status="run" />', ''),
                              good + f'<testcase name="{names[0]}" status="run" />'):
                    report.write_text(f'<testsuite>{cases}</testsuite>', encoding="utf-8")
                    with self.assertRaises(ValueError):
                        packager.package()
                    self.assertFalse((root / "dist").exists())
                report.write_text(f'<testsuite>{good}</testsuite>', encoding="utf-8")
                packager.package()
                self.assertEqual(len(list((root / "dist").glob("*.zip"))), 1)
                self.assertEqual(len(list((root / "dist").glob("*.sha256"))), 1)

    def test_release_rejects_non_main_and_invalid_modes(self):
        with self.assertRaises(ValueError):
            policy.plan([], "workflow_dispatch", "release", "refs/heads/feature")
        with self.assertRaises(ValueError):
            policy.plan([], "pull_request", "release", "refs/heads/main")
        with self.assertRaises(ValueError):
            policy.plan([], "workflow_dispatch", "typo", "refs/heads/main")

    def test_inventory_and_version_come_from_cmake(self):
        text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertRegex(policy.version(text), r"^\d+\.\d+\.\d+[A-Za-z0-9.-]*$")
        self.assertEqual(policy.version('project(ArrangerManager VERSION 1.2.3 LANGUAGES C CXX)\n'
                                        'set(ARRANGER_MANAGER_VERSION_SUFFIX "-fix2")'), "1.2.3-fix2")
        self.assertTrue(policy.inventory(text))
        for item in policy.UI_TESTS:
            self.assertIn(item, policy.inventory(text))
        with self.assertRaises(ValueError):
            policy.inventory(text + '\nadd_test(NAME Hidden COMMAND not_a_target)\n')
        with self.assertRaises(ValueError):
            policy.version("project(Other)")

    def test_no_duplicate_test_run_or_branch_bypass(self):
        workflow = (ROOT / ".github/workflows/windows-build.yml").read_text(encoding="utf-8")
        self.assertNotIn("ARRANGER_UI_PREVIEW", workflow)
        self.assertNotIn("github.head_ref !=", workflow)
        self.assertIn("--no-tests=error", workflow)
        self.assertIn("needs.windows.result == 'success'", workflow)
        self.assertIn("--draft", workflow)
        self.assertNotIn("pull_request_target", workflow)
        self.assertIn("contents: read", workflow)


if __name__ == "__main__":
    unittest.main()
