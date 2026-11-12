"""Fail-closed CI planning. CMake owns the version and registered test inventory."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
UI_FILES = {"src/TaskTree.cpp", "src/TaskTree.h", "src/TaskTreeRows.h",
            "src/TaskQuickProperties.h", "src/BlockEditor.cpp", "src/BlockEditor.h",
            "src/DayCalendarPanel.cpp", "src/DayCalendarPanel.h"}
UI_TESTS = ("WorkspaceNavigationTests", "BlockEditorTests", "GeneralTaskTests", "TaskTreeUiTests")


def version(text):
    base = re.search(r'project\(ArrangerManager VERSION (\d+\.\d+\.\d+) LANGUAGES C CXX\)', text)
    suffix = re.search(r'set\(ARRANGER_MANAGER_VERSION_SUFFIX "([A-Za-z0-9.-]*)"\)', text)
    if not base or not suffix:
        raise ValueError("Cannot determine application version from CMake")
    return base[1] + suffix[1]


def inventory(text):
    tests = re.findall(r'add_test\(NAME (\w+) COMMAND (\w+)\)', text)
    # Unknown registrations must not silently disappear from the release gate.
    if not tests or len(tests) != len(re.findall(r'\badd_test\s*\(', text)):
        raise ValueError("Unsupported test registration; update CI inventory parser")
    targets = set(re.findall(r'(?:add_executable|juce_add_console_app)\((\w+)', text))
    names = [name for name, target in tests]
    if len(set(names)) != len(names) or any(name != target or target not in targets for name, target in tests):
        raise ValueError("Each registered test must have a unique matching executable target")
    return names


def profile(paths):
    if not paths:
        return "full"
    code = [p for p in paths if not (p.endswith(".md") and
            ("/" not in p or p.startswith("docs/")))]
    if not code:
        return "docs"
    return "ui" if all(p in UI_FILES for p in code) else "full"


def plan(paths, event, mode, ref):
    if mode not in ("check", "candidate", "release"):
        raise ValueError("Unknown CI mode")
    if mode == "release" and (event != "workflow_dispatch" or ref != "refs/heads/main"):
        raise ValueError("Release is only allowed by explicit dispatch on main")
    # A manual check has no PR base: HEAD^ misses earlier feature commits.
    full = event in ("push", "workflow_dispatch") or mode in ("candidate", "release")
    return {"profile": "full" if full else profile(paths),
            "package": event == "workflow_dispatch" and mode in ("candidate", "release")}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--base")
    parser.add_argument("--head")
    parser.add_argument("--verify-ctest")
    args = parser.parse_args()
    if args.verify_ctest:
        result = subprocess.run(["ctest", "--test-dir", args.verify_ctest, "-C", "Release",
                                 "--show-only=json-v1"], cwd=ROOT, check=True, capture_output=True, text=True)
        actual = [t["name"] for t in json.loads(result.stdout)["tests"]]
        expected = inventory((ROOT / "CMakeLists.txt").read_text(encoding="utf-8"))
        if sorted(actual) != sorted(expected):
            raise ValueError(f"CTest inventory mismatch: {actual} != {expected}")
        print(f"Verified all {len(actual)} CTest registrations")
        return
    if not args.base or not args.head:
        parser.error("--base and --head are required for planning")
    result = subprocess.run(["git", "diff", "--name-only", "-z", args.base, args.head],
                            cwd=ROOT, check=True, capture_output=True)
    paths = result.stdout.decode("utf-8").strip("\0").split("\0") if result.stdout else []
    selected = plan(paths, os.environ["GITHUB_EVENT_NAME"], os.environ.get("CI_MODE", "check"),
                    os.environ["GITHUB_REF"])
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    tests = inventory(text)
    if any(t not in tests for t in UI_TESTS):
        raise ValueError("Scoped UI test inventory is out of date")
    selected["version"] = version(text)
    if os.environ.get("CI_MODE", "check") in ("candidate", "release"):
        if os.environ.get("EXPECTED_VERSION") != selected["version"]:
            raise ValueError("Candidate/release requires the exact CMake version input")
    application = (ROOT / "src/HubApplication.cpp").read_text(encoding="utf-8")
    if "return ARRANGER_MANAGER_VERSION;" not in application:
        raise ValueError("Application must use the CMake version definition")
    selected["tests"] = " ".join(UI_TESTS if selected["profile"] == "ui" else tests)
    selected["regex"] = "^(" + "|".join(UI_TESTS) + ")$" if selected["profile"] == "ui" else ".*"
    print(json.dumps({**selected, "paths": paths}, indent=2))
    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as out:
        for key, value in selected.items():
            out.write(f"{key}={str(value).lower() if isinstance(value, bool) else value}\n")


if __name__ == "__main__":
    main()
