#!/usr/bin/env python3
"""Validate a release request and emit GitHub Actions job outputs."""

import json
import os
import re
import subprocess
from pathlib import Path


SEMVER = re.compile(
    r"v?(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)"
    r"(?:-[0-9A-Za-z]+(?:[.-][0-9A-Za-z]+)*)?"
)
APP_VER = re.compile(
    r'^\s*#define\s+APP_VER\s+"([^"]+)"',
    re.MULTILINE,
)


def project_version(root: Path) -> str:
    text = (root / "src/appGlobals.h").read_text(encoding="utf-8")
    match = APP_VER.search(text)
    if not match:
        raise ValueError("Could not find APP_VER in src/appGlobals.h")
    return match.group(1)


def resolve(event_name: str, event: dict, root: Path) -> dict[str, str]:
    if event_name == "workflow_dispatch":
        inputs = event.get("inputs", {})
        tag = inputs.get("tag", "")
        commit = inputs.get("commit_firmware", True)
        if isinstance(commit, str):
            if commit not in ("true", "false"):
                raise ValueError("commit_firmware must be a boolean")
            commit = commit == "true"
        enabled = True
    elif event_name == "push":
        request = json.loads(
            (root / ".github/release-request.json").read_text(encoding="utf-8")
        )
        if set(request) != {"enabled", "tag", "commit_firmware"}:
            raise ValueError(
                "Request must contain enabled, tag, and commit_firmware only"
            )
        enabled, tag, commit = (
            request["enabled"],
            request["tag"],
            request["commit_firmware"],
        )
    else:
        raise ValueError(f"Unsupported event: {event_name}")

    if type(enabled) is not bool or type(commit) is not bool or not isinstance(tag, str):
        raise ValueError("Invalid release request field types")

    if not enabled:
        return {
            "publish": "false",
            "tag": "",
            "ver": "",
            "commit_firmware": "false",
        }

    if not SEMVER.fullmatch(tag):
        raise ValueError("Tag must be a version such as v10.9.5")

    tag = tag if tag.startswith("v") else "v" + tag
    version = tag[1:]
    baked_version = project_version(root)
    if version != baked_version:
        raise ValueError(
            f"Requested version {version} does not match APP_VER {baked_version}"
        )

    return {
        "publish": "true",
        "tag": tag,
        "ver": version,
        "commit_firmware": str(commit).lower(),
    }


def main() -> None:
    event = json.loads(Path(os.environ["GITHUB_EVENT_PATH"]).read_text(encoding="utf-8"))
    outputs = resolve(os.environ["GITHUB_EVENT_NAME"], event, Path("."))

    if outputs["publish"] == "true":
        result = subprocess.run(
            [
                "git",
                "show-ref",
                "--verify",
                "--quiet",
                "refs/tags/" + outputs["tag"],
            ],
            check=False,
        )
        if result.returncode == 0:
            raise ValueError("Tag already exists; choose a new version")
        if result.returncode != 1:
            raise RuntimeError("Could not check existing tags")

    with open(os.environ["GITHUB_OUTPUT"], "a", encoding="utf-8") as stream:
        for key, value in outputs.items():
            stream.write(f"{key}={value}\n")

    if outputs["publish"] == "true":
        print(
            f"Release request validated: {outputs['tag']} "
            f"(commit_firmware={outputs['commit_firmware']})"
        )
    else:
        print("Release publishing disabled; setup validation complete.")


if __name__ == "__main__":
    main()
