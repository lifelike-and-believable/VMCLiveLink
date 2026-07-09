#!/usr/bin/env python3
"""
Scan an Unreal plugin's Source/ directory and ensure every .h/.cpp file starts
with a copyright header.

Modes:
- Default (fix): insert a header where one is missing, and normalize existing
  headers (holder text, year range extended to include the current year).
- --verify: check only, write nothing. Prints every file whose header is
  missing or doesn't match the expected text and exits non-zero if any are
  found. Intended for CI.

Notes:
- Strips any UTF-8 BOM (U+FEFF) that may exist at the start of the file.
- Reads with utf-8-sig and writes with utf-8 (no BOM) to prevent BOM from
  appearing before includes/pragma lines.
- Preserves existing line endings (LF or CRLF) when inserting/updating the
  header.
- Handles year ranges with hyphen or en dash.
"""

import argparse
import datetime
import os
import re
import sys

CURRENT_YEAR = datetime.date.today().year
DEFAULT_HOLDER = "Lifelike & Believable Animation Design, Inc. | Athomas Goldberg"

YEAR_RANGE_SPLIT_RE = re.compile(r"[–-]")  # hyphen or en dash
HEADER_LINE_RE = re.compile(r"//\s*Copyright\s*\(c\)\s*([0-9]{4}(?:[–-][0-9]{4})?)\s+.+", re.I)


def detect_eol(lines):
    # Try to preserve existing newline style
    for l in lines:
        if l.endswith("\r\n"):
            return "\r\n"
        if l.endswith("\n"):
            return "\n"
    # Default to '\n' if we can't detect
    return "\n"


def normalize_years(years_str):
    years_str = years_str.strip()
    if YEAR_RANGE_SPLIT_RE.search(years_str):
        parts = YEAR_RANGE_SPLIT_RE.split(years_str, maxsplit=1)
        try:
            start_year = int(parts[0].strip())
        except ValueError:
            start_year = CURRENT_YEAR
        return f"{start_year}-{CURRENT_YEAR}"
    try:
        y = int(years_str)
    except ValueError:
        y = CURRENT_YEAR
    return f"{y}-{CURRENT_YEAR}" if y != CURRENT_YEAR else f"{CURRENT_YEAR}"


def desired_header_line(holder, years):
    return f"// Copyright (c) {years} {holder}. All Rights Reserved."


def process_file(path, holder, verify):
    """Returns the path if it has a missing/outdated header, else None.
    In fix mode (verify=False), also rewrites the file and returns None."""
    # Read using utf-8-sig to automatically remove BOM if present
    with open(path, "r", encoding="utf-8-sig", errors="ignore", newline="") as f:
        lines = f.readlines()

    if not lines:
        return None

    # Defensive: strip any lingering BOM at the very start of the file
    if lines[0].startswith("﻿"):
        lines[0] = lines[0].lstrip("﻿")

    eol = detect_eol(lines)

    # Work with the first line without its trailing newline
    first_line_no_eol = lines[0].rstrip("\r\n")

    m = HEADER_LINE_RE.match(first_line_no_eol)
    if m:
        years = normalize_years(m.group(1))
        desired = desired_header_line(holder, years)
        if first_line_no_eol == desired:
            return None
        if verify:
            return path
        lines[0] = desired + eol
    else:
        desired = desired_header_line(holder, str(CURRENT_YEAR))
        if verify:
            return path
        lines.insert(0, desired + eol)

    with open(path, "w", encoding="utf-8", newline="") as f:
        f.writelines(lines)
    print(f"Updated: {path}")
    return None


def iter_source_files(source_root):
    for dirpath, _, filenames in os.walk(source_root):
        for fn in filenames:
            if fn.endswith((".h", ".hpp", ".cpp", ".cxx")):
                yield os.path.join(dirpath, fn)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("plugin_root", help="Path to the plugin root folder (contains Source/).")
    parser.add_argument("--holder", default=DEFAULT_HOLDER,
                         help="Copyright holder text to enforce (default: %(default)r).")
    parser.add_argument("--verify", action="store_true",
                         help="Check only; do not modify files. Exit non-zero if any header is missing or outdated.")
    args = parser.parse_args()

    source_root = os.path.join(args.plugin_root, "Source")
    if not os.path.isdir(source_root):
        print(f"No Source folder under {args.plugin_root}")
        sys.exit(1)

    violations = [
        path
        for path in iter_source_files(source_root)
        if process_file(path, args.holder, args.verify)
    ]

    if args.verify:
        if violations:
            print("Missing or outdated copyright header:")
            for v in violations:
                print(f"  {v}")
            sys.exit(1)
        print("All copyright headers OK.")


if __name__ == "__main__":
    main()
