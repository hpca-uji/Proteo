#!/usr/bin/env python3
"""Expand a complex JSON config (colon variants) into concrete JSON files."""

import json
import sys
from pathlib import Path

from expand_multi_json import expand_from_file


def main() -> int:
    if len(sys.argv) < 3:
        print("Not enough arguments given.")
        print("Expected usage: python3 read_multiple_json.py complex.json output_prefix")
        return 1

    input_path = Path(sys.argv[1])
    output_prefix = sys.argv[2]

    if not input_path.is_file():
        print(f"Input file not found: {input_path}")
        return 1

    try:
        count = expand_from_file(input_path, output_prefix)
    except (ValueError, json.JSONDecodeError) as exc:
        print(f"Error: {exc}")
        return 1

    return 0 if count > 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
