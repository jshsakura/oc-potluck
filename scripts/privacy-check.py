#!/usr/bin/env python3
"""Block real addresses, personal data and secrets from entering the repo.

Checks text for public IPv4 addresses, email addresses, token-shaped
secrets, and any literal from a private denylist. The denylist holds the
real hosts and addresses that must never appear, so it never lives in the
repo: it comes from ~/.config/potluck/denylist (one entry per line) and
from the PRIVACY_DENYLIST environment variable (newline or comma separated).

Usage:
  privacy-check.py --staged        files staged for commit (pre-commit hook)
  privacy-check.py --all           every tracked file (CI)
  privacy-check.py --message FILE  a commit message (commit-msg hook)
  privacy-check.py --range A..B    commit messages in a range (CI)

A line containing "privacy:allow" is skipped.
"""

import argparse
import ipaddress
import os
import re
import subprocess
import sys

IPV4 = re.compile(r"(?<![\w.])(?:\d{1,3}\.){3}\d{1,3}(?![\w.])")
EMAIL = re.compile(r"[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}")
SECRETS = [
    ("GitHub token", re.compile(r"\b(?:ghp|gho|ghu|ghs|ghr)_[A-Za-z0-9]{30,}\b|\bgithub_pat_[A-Za-z0-9_]{40,}\b")),
    ("AWS key", re.compile(r"\bAKIA[0-9A-Z]{16}\b")),
    ("Slack token", re.compile(r"\bxox[abprs]-[A-Za-z0-9-]{10,}\b")),
    ("private key", re.compile(r"-----BEGIN (?:RSA |EC |OPENSSH |DSA )?PRIVATE KEY-----")),
]
# Well-known public resolvers and placeholder addresses used in code and tests.
ALLOWED_IPS = {"8.8.8.8", "8.8.4.4", "1.1.1.1", "1.0.0.1", "1.2.3.4", "7.8.9.10"}
ALLOWED_EMAIL = re.compile(r"(?:noreply|no-reply)@|@(?:[\w-]+\.)*(?:example\.(?:com|org|net)|localhost)$", re.I)
ALLOW_MARK = "privacy:allow"
TRAILER = re.compile(r"^(?:Signed-off-by|Co-Authored-By|Co-authored-by):", re.I)
MAX_BYTES = 2 * 1024 * 1024
DENYLIST_FILE = os.path.expanduser("~/.config/potluck/denylist")


def load_denylist():
    entries = []
    try:
        with open(DENYLIST_FILE, encoding="utf-8") as f:
            entries += f.read().splitlines()
    except FileNotFoundError:
        pass
    except OSError as e:
        print(f"privacy-check: cannot read {DENYLIST_FILE}: {e}", file=sys.stderr)
    entries += re.split(r"[\n,]", os.environ.get("PRIVACY_DENYLIST", ""))
    return sorted({e.strip().lower() for e in entries if e.strip() and not e.strip().startswith("#")})


def is_public_ip(text):
    if text in ALLOWED_IPS:
        return False
    try:
        ip = ipaddress.IPv4Address(text)
    except ValueError:
        return False  # octet over 255: a version string or a test value
    return ip.is_global and not ip.is_multicast


def scan_line(line, denylist, is_message):
    if ALLOW_MARK in line or (is_message and TRAILER.match(line)):
        return []
    found = []
    lowered = line.lower()
    found += [f"denylist entry #{i + 1}" for i, entry in enumerate(denylist) if entry in lowered]
    found += [f"public IP {m}" for m in IPV4.findall(line) if is_public_ip(m)]
    found += [f"email {m}" for m in EMAIL.findall(line) if not ALLOWED_EMAIL.search(m)]
    found += [name for name, pattern in SECRETS if pattern.search(line)]
    return found


def scan_text(label, text, denylist, is_message=False):
    problems = []
    for number, line in enumerate(text.splitlines(), 1):
        problems += [f"{label}:{number}: {what}" for what in scan_line(line, denylist, is_message)]
    return problems


def git(*args):
    return subprocess.run(["git", *args], check=True, capture_output=True).stdout


def read_blob(spec):
    data = git("show", spec)
    if len(data) > MAX_BYTES or b"\0" in data[:8192]:
        return None  # binary or generated: nothing readable to leak
    return data.decode("utf-8", errors="replace")


def file_specs(staged):
    if staged:
        names = git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").split(b"\0")
        return [(n.decode(), ":" + n.decode()) for n in names if n]
    names = git("ls-files", "-z", "--cached").split(b"\0")
    return [(n.decode(), "HEAD:" + n.decode()) for n in names if n]


def scan_files(staged, denylist):
    problems = []
    for name, spec in file_specs(staged):
        if name == "scripts/privacy-check.py":
            continue  # holds the patterns themselves
        try:
            text = read_blob(spec)
        except subprocess.CalledProcessError:
            continue  # submodule entry or a path git cannot show
        if text is not None:
            problems += scan_text(name, text, denylist)
    return problems


def scan_range(span, denylist):
    problems = []
    for sha in git("rev-list", span).decode().split():
        message = git("log", "-1", "--format=%B", sha).decode("utf-8", errors="replace")
        problems += scan_text(f"commit {sha[:8]}", message, denylist, is_message=True)
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--staged", action="store_true")
    mode.add_argument("--all", action="store_true")
    mode.add_argument("--message", metavar="FILE")
    mode.add_argument("--range", metavar="A..B")
    args = parser.parse_args()

    denylist = load_denylist()
    if args.message:
        with open(args.message, encoding="utf-8", errors="replace") as f:
            text = "\n".join(l for l in f.read().splitlines() if not l.startswith("#"))
        problems = scan_text("commit message", text, denylist, is_message=True)
    elif args.range:
        problems = scan_range(args.range, denylist)
    else:
        problems = scan_files(args.staged, denylist)

    if not problems:
        return 0
    print("privacy-check: blocked. Use relay.example.com or 203.0.113.x instead,", file=sys.stderr)
    print("or add 'privacy:allow' to a line that is safe on purpose.", file=sys.stderr)
    for problem in problems:
        print(f"  {problem}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
