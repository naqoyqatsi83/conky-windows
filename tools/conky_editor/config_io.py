"""Targeted read/write of the alignment/gap_x/gap_y keys in a conkyrc.

Not a Lua parser -- like tools/backport_conkyrc.py, this uses regex
substitution scoped to `conky.config = { ... }` and only touches these three
keys, leaving everything else in the block untouched.

Accepted alignment strings match src/output/gui.cc's alignment table exactly.
"""

from __future__ import annotations

import re

ALIGNMENTS = (
    "top_left", "top_middle", "top_right",
    "middle_left", "middle_middle", "middle_right",
    "bottom_left", "bottom_middle", "bottom_right",
)

_CONFIG_BLOCK_RE = re.compile(r"conky\.config\s*=\s*\{.*?\n\}", re.DOTALL)
_ALIGNMENT_RE = re.compile(r"""alignment\s*=\s*['"](\w+)['"]""")
_GAP_X_RE = re.compile(r"\bgap_x\s*=\s*(-?\d+)")
_GAP_Y_RE = re.compile(r"\bgap_y\s*=\s*(-?\d+)")


def _config_block(text: str) -> str:
    match = _CONFIG_BLOCK_RE.search(text)
    return match.group(0) if match else text


def read_alignment(text: str) -> str | None:
    match = _ALIGNMENT_RE.search(_config_block(text))
    return match.group(1) if match else None


def read_gap_x(text: str) -> int | None:
    match = _GAP_X_RE.search(_config_block(text))
    return int(match.group(1)) if match else None


def read_gap_y(text: str) -> int | None:
    match = _GAP_Y_RE.search(_config_block(text))
    return int(match.group(1)) if match else None


def _replace_in_block(text: str, pattern: re.Pattern, replacement: str) -> str:
    block = _config_block(text)
    if not pattern.search(block):
        return text
    start, end = _CONFIG_BLOCK_RE.search(text).span()
    new_block = pattern.sub(replacement, block, count=1)
    return text[:start] + new_block + text[end:]


def write_alignment(text: str, alignment: str) -> str:
    if alignment not in ALIGNMENTS:
        raise ValueError(f"unknown alignment {alignment!r}")
    return _replace_in_block(
        text, _ALIGNMENT_RE, f"alignment = '{alignment}'"
    )


def write_gap_x(text: str, gap_x: int) -> str:
    return _replace_in_block(text, _GAP_X_RE, f"gap_x = {gap_x}")


def write_gap_y(text: str, gap_y: int) -> str:
    return _replace_in_block(text, _GAP_Y_RE, f"gap_y = {gap_y}")
