#!/usr/bin/env python3

from pathlib import Path
import subprocess

SUBMODULES_MAP = {
    "./": [None],
    "deps/rocket-chip": [
        "dependencies/cde",
        "dependencies/diplomacy",
        "dependencies/hardfloat",
    ],
    "deps/LaomaSubsystem": [
        "xs-utils",
    ],
    "deps/rvt": [
        "csrc/DRAMsim3",
    ],
}

PROJECT_ROOT = Path(__file__).resolve().parent.parent


def run_git(module_path: Path, args: list[str]) -> None:
    subprocess.run(
        ["git", "-C", str(module_path), *args],
        check=True,
    )


def update_submodules() -> None:
    for module_name, submodules in SUBMODULES_MAP.items():
        module_path = PROJECT_ROOT / module_name

        run_git(module_path, ["submodule", "sync"])

        for submodule in submodules:
            cmd = ["submodule", "update", "--init", "--progress"]
            if submodule is not None:
                cmd.append(submodule)
            run_git(module_path, cmd)


if __name__ == "__main__":
    update_submodules()
