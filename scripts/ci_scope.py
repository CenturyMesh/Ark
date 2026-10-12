#!/usr/bin/env python3
"""Conservative CI scope and local documentation validation; no network access."""
import argparse
import json
import os
from pathlib import Path, PurePosixPath
import re
import subprocess
from urllib.parse import unquote, urlsplit

ROOT_DOCS = {"README.md", "CONTRIBUTING.md", "CODE_OF_CONDUCT.md", "SECURITY.md"}
# These Markdown files are package inputs, so changes still exercise packaging.
PACKAGE_DOCS = {
    "docs/AGENT_BENCHMARK.md", "docs/UPDATES.md", "docs/BOM.md",
    "docs/AGENT_ACTIVITY.md", "docs/MEASUREMENT.md", "docs/THIRD_PARTY_NOTICES.md",
}
SHA = re.compile(r"[0-9a-fA-F]{40}\Z")
INLINE_LINK = re.compile(r"\[[^\]\n]*\]\(\s*(<[^>\n]+>|[^\s)]+)(?:\s+[^)]*)?\)")
REFERENCE_LINK = re.compile(r"^\s{0,3}\[[^\]\n]+\]:\s*(<[^>\n]+>|\S+)", re.MULTILINE)


def docs_only_path(path):
    parts = PurePosixPath(path).parts
    if not path or path.startswith("/") or ".." in parts or "\\" in path or any(ord(char) < 32 or ord(char) == 127 for char in path):
        return False
    if path in ROOT_DOCS or path == ".github/FUNDING.yml":
        return True
    if path.startswith(".github/ISSUE_TEMPLATE/"):
        return PurePosixPath(path).suffix in {".md", ".yml", ".yaml"}
    return path.startswith("docs/") and path.endswith(".md") and path not in PACKAGE_DOCS


def git(repository, *arguments):
    return subprocess.run(["git", "-C", str(repository), *arguments],
                          stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout


def classify(repository, event, base, head, ref):
    if event not in {"push", "pull_request"} or ref.startswith("refs/tags/"):
        return True, "event requires full CI", []
    if not SHA.fullmatch(base) or not SHA.fullmatch(head) or set(base) == {"0"}:
        return True, "missing or invalid comparison commit", []
    try:
        actual_head = git(repository, "rev-parse", "HEAD").decode().strip()
        if actual_head.lower() != head.lower():
            return True, "checkout does not match event head", []
        git(repository, "rev-parse", "--verify", base + "^{commit}")
        git(repository, "merge-base", "--is-ancestor", base, head)
        raw = git(repository, "diff", "--name-only", "--no-renames", "-z", base, head, "--")
        paths = [path.decode("utf-8") for path in raw.split(b"\0") if path]
    except (subprocess.CalledProcessError, UnicodeDecodeError):
        return True, "comparison could not be proven", []
    if not paths:
        return True, "empty comparison requires full CI", []
    if all(docs_only_path(path) for path in paths):
        return False, "only allowlisted documentation paths changed", paths
    return True, "runtime, build, test, package or unknown path changed", paths


def markdown_targets(text):
    # Ignore examples inside fenced and inline code, retaining actual links.
    lines = []
    fence = None
    for line in text.splitlines():
        marker = re.match(r"^\s{0,3}(`{3,}|~{3,})", line)
        if marker:
            char = marker.group(1)[0]
            if fence is None:
                fence = char
            elif fence == char:
                fence = None
            continue
        if fence is None:
            lines.append(re.sub(r"`+[^`\n]*`+", "", line))
    prose = "\n".join(lines)
    return [match.group(1).strip("<>") for pattern in (INLINE_LINK, REFERENCE_LINK)
            for match in pattern.finditer(prose)]


def validate_docs(repository):
    import yaml
    tracked = [path.decode("utf-8") for path in git(repository, "ls-files", "-z").split(b"\0") if path]
    errors = []
    checked = 0
    for relative in tracked:
        if not (relative.endswith(".md") and (relative in ROOT_DOCS or relative.startswith("docs/"))
                or relative.startswith(".github/ISSUE_TEMPLATE/") or relative == ".github/FUNDING.yml"):
            continue
        path = repository / relative
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeError) as error:
            errors.append(f"{relative}: cannot read UTF-8 text ({error})")
            continue
        checked += 1
        if "\0" in text:
            errors.append(f"{relative}: NUL in documentation text")
        if not relative.endswith(".md"):
            if path.suffix in {".yml", ".yaml"}:
                try:
                    document = yaml.safe_load(text)
                    if not isinstance(document, dict):
                        errors.append(f"{relative}: YAML must contain a mapping")
                except yaml.YAMLError as error:
                    errors.append(f"{relative}: invalid YAML ({error})")
            continue
        for target in markdown_targets(text):
            parsed = urlsplit(target)
            if parsed.scheme or parsed.netloc or not parsed.path:
                continue
            link_path = unquote(parsed.path)
            resolved = (repository / link_path.lstrip("/") if link_path.startswith("/")
                        else path.parent / link_path).resolve()
            try:
                resolved.relative_to(repository)
                inside_repository = True
            except ValueError:
                inside_repository = False
            # Frozen history retains references to earlier sibling workspaces.
            # Current docs must resolve inside this repository; archive text
            # and links that do stay inside the repository are still checked.
            if not inside_repository and relative.startswith("docs/history/"):
                continue
            if not inside_repository or not resolved.exists():
                errors.append(f"{relative}: missing local link {target}")
    return checked, errors


def gate_errors(scope_result, runtime_changed, results):
    errors = []
    if scope_result != "success":
        errors.append("CI scope, documentation and source metadata validation must succeed")
    if runtime_changed not in {"true", "false"}:
        errors.append("Missing or invalid runtime classification")
    accepted = {"success"} if runtime_changed == "true" else {"success", "skipped"}
    if any(result not in accepted for result in results):
        errors.append("Required CI jobs did not complete with the required result")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("classify", "check-docs", "gate"))
    parser.add_argument("--repository", type=Path, default=Path.cwd())
    args = parser.parse_args()
    repository = args.repository.resolve()
    if args.command == "gate":
        errors = gate_errors(os.environ.get("CI_SCOPE_RESULT", ""),
                             os.environ.get("CI_RUNTIME_CHANGED", ""),
                             [os.environ.get(name, "") for name in
                              ("CI_CORE_RESULT", "CI_WINDOWS_RESULT", "CI_LINUX_RESULT")])
        for error in errors:
            print("CI gate failure: " + error)
        if not errors:
            scope = "full runtime CI" if os.environ["CI_RUNTIME_CHANGED"] == "true" else "documentation only; native builds/tests skipped"
            print("CI gate passed: " + scope)
        return bool(errors)
    if args.command == "check-docs":
        checked, errors = validate_docs(repository)
        for error in errors:
            print("Documentation error: " + json.dumps(error, ensure_ascii=True))
        print(f"Validated {checked} documentation files; {len(errors)} local-link/UTF-8 errors.")
        return bool(errors)
    changed, reason, paths = classify(repository, os.environ.get("CI_EVENT_NAME", ""),
                                      os.environ.get("CI_BASE_SHA", ""), os.environ.get("CI_HEAD_SHA", ""),
                                      os.environ.get("CI_REF", ""))
    value = "true" if changed else "false"
    print(f"runtime_changed={value}; {reason}; {len(paths)} changed paths.")
    output = os.environ.get("GITHUB_OUTPUT")
    if output:
        with open(output, "a", encoding="utf-8") as stream:
            stream.write(f"runtime_changed={value}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
