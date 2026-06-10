#!/usr/bin/env python3
"""Validate SimplestSampler Notes vault structure and frontmatter."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
VAULT = REPO_ROOT / "SimplestSampler Notes"

VALID_TYPES = frozenset(
    {
        "daily",
        "task",
        "decision",
        "incident",
        "release",
        "runbook",
        "known-good",
        "index",
        "template",
    }
)
VALID_STATUSES = frozenset(
    {"draft", "active", "blocked", "verified", "stale", "superseded"}
)
REQUIRED_FRONTMATTER = ("schema_version", "title", "type", "status", "date", "tags")

DAILY_BULLET = re.compile(r"^-\s+\d{1,2}:\d{2}:", re.MULTILINE)
FRONTMATTER_RE = re.compile(r"^---\s*\n(.*?)\n---\s*\n", re.DOTALL)
DAILY_FILENAME = re.compile(r"^\d{4}-\d{2}-\d{2}\.md$")
TASK_FILENAME = re.compile(r"^\d{4}-\d{2}-\d{2}-.+\.md$")

SKIP_NAMES = frozenset(
    {
        "Home.md",
        "Notes System.md",
        "_README.md",
    }
)


def parse_simple_yaml(block: str) -> dict[str, str]:
    data: dict[str, str] = {}
    for line in block.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if ":" not in stripped:
            continue
        key, value = stripped.split(":", 1)
        data[key.strip()] = value.strip()
    return data


def read_frontmatter(path: Path) -> tuple[dict[str, str] | None, str]:
    text = path.read_text(encoding="utf-8")
    match = FRONTMATTER_RE.match(text)
    if not match:
        return None, text
    return parse_simple_yaml(match.group(1)), text


def validate_frontmatter_fields(meta: dict[str, str], path: Path, errors: list[str], warnings: list[str]) -> None:
    for field in REQUIRED_FRONTMATTER:
        if field not in meta:
            errors.append(f"{path.relative_to(REPO_ROOT)}: missing frontmatter field '{field}'")
        elif field == "tags" and meta[field] == "":
            errors.append(f"{path.relative_to(REPO_ROOT)}: missing frontmatter field 'tags'")
        elif field != "tags" and not meta[field]:
            errors.append(f"{path.relative_to(REPO_ROOT)}: missing frontmatter field '{field}'")

    note_type = meta.get("type", "")
    if note_type and note_type not in VALID_TYPES:
        errors.append(f"{path.relative_to(REPO_ROOT)}: invalid type '{note_type}'")

    status = meta.get("status", "")
    if status and status not in VALID_STATUSES:
        errors.append(f"{path.relative_to(REPO_ROOT)}: invalid status '{status}'")

    if meta.get("schema_version") and meta["schema_version"] != "1":
        warnings.append(
            f"{path.relative_to(REPO_ROOT)}: unexpected schema_version '{meta['schema_version']}'"
        )


def validate_daily_note(path: Path, text: str, errors: list[str], warnings: list[str]) -> None:
    if not DAILY_BULLET.search(text):
        warnings.append(
            f"{path.relative_to(REPO_ROOT)}: no timestamped bullets matching '- HH:MM:'"
        )


def should_require_frontmatter(path: Path, strict_all: bool) -> bool:
    rel = path.relative_to(VAULT)
    parts = rel.parts

    if path.name in SKIP_NAMES:
        return False
    if "Templates" in parts:
        return True
    if parts[0] in {"Decisions", "Incidents", "Releases"}:
        return True
    if strict_all and TASK_FILENAME.match(path.name):
        return True
    if strict_all and path.name.endswith(".md") and parts[0] not in {"Areas", "Phases", "Runbooks", "Known-Good", "Dashboards"}:
        if DAILY_FILENAME.match(path.name):
            return False
        if path.name in SKIP_NAMES:
            return False
    return False


def collect_markdown_files() -> list[Path]:
    if not VAULT.is_dir():
        return []
    return sorted(VAULT.rglob("*.md"))


def validate(strict_all: bool) -> int:
    errors: list[str] = []
    warnings: list[str] = []

    if not VAULT.is_dir():
        print(f"error: vault not found at {VAULT}", file=sys.stderr)
        return 1

    for path in collect_markdown_files():
        meta, text = read_frontmatter(path)

        if DAILY_FILENAME.match(path.name):
            validate_daily_note(path, text, errors, warnings)

        if should_require_frontmatter(path, strict_all):
            if meta is None:
                level = errors if strict_all else warnings
                level.append(
                    f"{path.relative_to(REPO_ROOT)}: expected YAML frontmatter"
                )
            else:
                validate_frontmatter_fields(meta, path, errors, warnings)

    for message in warnings:
        print(f"warning: {message}")

    for message in errors:
        print(f"error: {message}", file=sys.stderr)

    if errors:
        print(f"\n{len(errors)} error(s), {len(warnings)} warning(s)", file=sys.stderr)
        return 1

    print(f"OK — {len(warnings)} warning(s)")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate SimplestSampler Notes vault.")
    parser.add_argument(
        "--strict-all",
        action="store_true",
        help="Require frontmatter on task-style root notes (audit mode).",
    )
    args = parser.parse_args()
    return validate(strict_all=args.strict_all)


if __name__ == "__main__":
    sys.exit(main())
