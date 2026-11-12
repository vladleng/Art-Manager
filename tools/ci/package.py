"""Package only the successful full-test build from this run, with provenance."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import zipfile
from policy import ROOT, inventory, version, validate_report


def package():
    cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    current = version(cmake)
    if os.environ["CI_VERSION"] != current or os.environ["CI_PACKAGE"] != "true":
        raise ValueError("Not a validated packaging run")
    expected = set(inventory(cmake))
    validate_report(ROOT / "build/test-results.xml", expected)
    executable = ROOT / "build/ArrangerManagerDesktop_artefacts/Release/Arranger Manager.exe"
    if not executable.is_file():
        raise ValueError("Desktop executable missing")
    directory = ROOT / "package" / f"Arranger Manager {current}"
    directory.mkdir(parents=True, exist_ok=False)
    shutil.copy2(executable, directory)
    for name in ("TEMPLATES-67.md", "PORTABILITY-68.md", "TODAY-38.md",
                 "LEGACY-RETIREMENT-7.md", "WORKSPACE-0.1e-DECISIONS.md"):
        shutil.copy2(ROOT / "docs" / name, directory)
    for preview in (ROOT / "build").glob("*.png"):
        shutil.copy2(preview, directory)
    manifest = {"component": "Arranger Manager", "version": current,
                "commit": os.environ["GITHUB_SHA"], "run_id": os.environ["GITHUB_RUN_ID"],
                "test_profile": "full", "tests": sorted(expected),
                "exe_sha256": hashlib.sha256(executable.read_bytes()).hexdigest()}
    (directory / "version.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    archive = dist / f"Arranger-Manager-{current}-Windows-App.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as out:
        for path in sorted(directory.iterdir()):
            out.write(path, f"{directory.name}/{path.name}")
    checksum = hashlib.sha256(archive.read_bytes()).hexdigest()
    archive.with_suffix(".sha256").write_text(f"{checksum}  {archive.name}\n", encoding="ascii")
    print(f"Packaged {current}, {len(expected)} passing tests, SHA {manifest['commit']}")


if __name__ == "__main__":
    package()
