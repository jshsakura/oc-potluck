#!/usr/bin/env python3
"""Install git hooks for potluck.

Copies the pre-commit and commit-msg hooks from scripts/hooks/ to .git/hooks/
and makes them executable.
"""

import os
import shutil
import stat
import subprocess
import sys


HOOKS = ("pre-commit", "commit-msg")


def install(repo_root, git_hooks, name):
    source_hook = os.path.join(repo_root, "scripts", "hooks", name)
    if not os.path.exists(source_hook):
        print(f"ERROR: Hook source not found: {source_hook}", file=sys.stderr)
        sys.exit(1)
    dest = os.path.join(git_hooks, name)
    if os.path.exists(dest):
        # Remove old hook to avoid conflicts
        os.remove(dest)
    shutil.copy2(source_hook, dest)
    os.chmod(dest, os.stat(dest).st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    print(f"Installed {name} hook: {dest}")


def main():
    repo_root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    common_dir = subprocess.run(["git", "rev-parse", "--git-common-dir"], cwd=repo_root,
                                check=True, capture_output=True, text=True).stdout.strip()
    git_hooks = os.path.join(repo_root, common_dir, "hooks")
    os.makedirs(git_hooks, exist_ok=True)
    for name in HOOKS:
        install(repo_root, git_hooks, name)
    print("Both hooks block real addresses, personal data and secrets (scripts/privacy-check.py).")
    print("Put real hosts and IPs that must never appear in ~/.config/potluck/denylist.")
    print("Set POTLUCK_RUN_TESTS=1 to also run unit tests with >= 80% coverage before each commit")
    print("(MIN_COVERAGE changes the threshold).")


if __name__ == "__main__":
    main()