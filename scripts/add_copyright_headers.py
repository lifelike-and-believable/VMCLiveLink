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
- Only the first line is ever decoded as text (to detect/build the header);
  everything after it is read and rewritten as untouched raw bytes, so a
  file that isn't valid UTF-8 past the header line can't get silently
  mangled by a lossy decode/re-encode round-trip.
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
BOM = b"\xef\xbb\xbf"

YEAR_RANGE_SPLIT_RE = re.compile(r"[–-]")  # hyphen or en dash
HEADER_LINE_RE = re.compile(r"//\s*Copyright\s*\(c\)\s*([0-9]{4}(?:[–-][0-9]{4})?)\s+.+", re.I)


def split_first_line(raw):
    """Split raw bytes into (first_line_bytes, eol_bytes, rest_bytes). eol_bytes
    is b"" if the file has no newline (single line, nothing to preserve after it)."""
    idx = raw.find(b"\n")
    if idx == -1:
        return raw, b"", b""
    if idx > 0 and raw[idx - 1:idx] == b"\r":
        return raw[:idx - 1], b"\r\n", raw[idx + 1:]
    return raw[:idx], b"\n", raw[idx + 1:]


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
    with open(path, "rb") as f:
        raw = f.read()

    if not raw:
        return None

    if raw.startswith(BOM):
        raw = raw[len(BOM):]

    first_bytes, eol, rest = split_first_line(raw)
    # Only this slice is ever decoded, and only to detect/parse an existing
    # header - the decoded string is never written back, so a lossy decode
    # here (errors="ignore") can't corrupt anything.
    first_line_no_eol = first_bytes.decode("utf-8", errors="ignore")

    m = HEADER_LINE_RE.match(first_line_no_eol)
    if m:
        years = normalize_years(m.group(1))
        desired = desired_header_line(holder, years)
        if first_line_no_eol == desired:
            return None
        if verify:
            return path
        new_raw = desired.encode("utf-8") + (eol or b"\n") + rest
    else:
        desired = desired_header_line(holder, str(CURRENT_YEAR))
        if verify:
            return path
        new_raw = desired.encode("utf-8") + b"\n" + raw

    with open(path, "wb") as f:
        f.write(new_raw)
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
