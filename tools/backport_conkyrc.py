#!/usr/bin/env python3
"""backport_conkyrc.py - lint a Linux conky theme for this Windows port.

Not a full transpiler. It parses a Lua-style conkyrc (`conky.config = {...}`,
`conky.text = [[...]]`), walks every `${...}` object reference inside
conky.text, and classifies each one:

  - OK          - works unchanged on Windows
  - REVIEW      - works, but has a Windows-specific gotcha (GPU index
                  numbering, network interface naming, single-battery
                  support, etc.) that needs a human decision
  - UNSUPPORTED - no Windows equivalent (Linux-only subsystem)
  - EXEC        - a ${exec}/${execi}/${execp}/${execpi}/${texeci} shell
                  body; matched against a small POSIX -> PowerShell idiom
                  table where possible

It never rewrites conky.text in place except for EXEC bodies matched with
high confidence (a verbatim command substitution) - everything else is left
untouched and reported, because conky.text is a Lua long-bracket string with
no comment syntax, so injecting inline comments would corrupt the rendered
output.

See conky-windows issue #3 for scope/rationale. Extend UNSUPPORTED_PREFIXES,
UNSUPPORTED_EXACT, REVIEW_OBJECTS and EXEC_IDIOMS as real themes surface new
cases - this is a starting point, not an exhaustive mapping.

Usage:
    python backport_conkyrc.py <linux.conkyrc> [-o output.conkyrc] [-r report.md]
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# Knowledge tables - see conky-windows src/core.cc for the object registry.
# ---------------------------------------------------------------------------

# Object name prefixes with zero Windows equivalent (feature compiled out  - 
# see cmake/ConkyBuildOptions.cmake / .github/workflows/build-windows.yml).
UNSUPPORTED_PREFIXES = (
    "mpd_", "moc_", "cmus_", "xmms2_", "audacious_",  # music players
    "apcupsd_",                                        # UPS monitoring
    "pa_",                                              # PulseAudio
    "wireless_",                                        # Linux wireless-tools
    "irc_", "ical_", "ibm_", "smapi_",
)
UNSUPPORTED_EXACT = {
    "i2c", "platform", "hwmon",              # Linux sensors subsystem
    "key_num_lock", "key_caps_lock", "key_scroll_lock",
    "keyboard_layout", "mouse_speed",         # X11-only
    "mysql",
    "top", "top_mem", "top_time", "top_io",  # process list works differently; see REVIEW below instead if present
}

# Objects that work on Windows but have a platform-specific gotcha worth
# flagging for manual review rather than silently passing.
REVIEW_OBJECTS = {
    "nvidia": "Raw NVML path (src/data/hardware/nvidia_nvml.cc). Index counts "
              "ONLY NVIDIA GPUs (usually 0 for a single-GPU system). Fine for "
              "NVIDIA-only setups; for cross-vendor/multi-GPU stats use the "
              "${gputemp N} family instead (different index space - see below).",
    "nvidiabar": "See ${nvidia} note above.",
    "nvidiagraph": "See ${nvidia} note above.",
    "nvidiagauge": "See ${nvidia} note above.",
    "gputemp": "LHM cross-vendor path (src/data/os/windows/gpu.cc). Index N "
               "counts ALL GPUs (integrated + discrete) in LibreHardwareMonitor's "
               "enumeration order - NOT the same index space as ${nvidia*}. "
               "Verify the index against C:\\ProgramData\\Conky\\gpu.dat "
               "(or lhm_debug.txt) rather than assuming 0.",
    "gpuutil": "See ${gputemp} note above.",
    "gpuname": "See ${gputemp} note above.",
    "gpufan": "See ${gputemp} note above.",
    "gpumemused": "See ${gputemp} note above.",
    "gpumemtotal": "See ${gputemp} note above.",
    "gpugraph": "See ${gputemp} note above.",
    "acpitemp": "Works via a WMI/LibreHardwareMonitor fallback chain that "
                "depends on the 'ConkyTempHelper' scheduled task + lhm-temp.exe "
                "helper actually running. If this shows -1, check that the "
                "helper is running before assuming the config is wrong.",
    "battery": "Windows only reports the system's single battery; a Linux "
               "battery name argument (e.g. BAT0) is accepted but ignored.",
    "battery_bar": "See ${battery} note above.",
    "battery_percent": "See ${battery} note above.",
    "battery_time": "See ${battery} note above.",
}

# Objects whose first argument is commonly a Linux network interface name on
# Linux themes (eth0, wlan0, enp3s0, ...) - Windows uses friendly adapter
# names instead (Ethernet, Wi-Fi, ...).
NETWORK_OBJECTS = {
    "downspeed", "upspeed", "downspeedf", "upspeedf", "downspeedgraph",
    "upspeedgraph", "totaldown", "totalup", "addr", "addrs",
}
LINUX_IFACE_RE = re.compile(r"^(eth\d+|wlan\d+|en[ospx]\w*|wl[ospx]\w*|lo)$")

# ---------------------------------------------------------------------------
# ${exec ...} shell-body idiom table.
# Each entry: (name, regex, category, message, replacement_or_None)
#   category "auto"   -> replacement is applied verbatim in the output file
#   category "manual" -> flagged only, never auto-applied
# ---------------------------------------------------------------------------

EXEC_IDIOMS = [
    (
        "already-powershell",
        re.compile(r"^\s*(powershell|pwsh)\b", re.IGNORECASE),
        "auto",
        "Already PowerShell - no rewrite needed (verify the cmdlets used are "
        "correct for your case, but this isn't a POSIX->PowerShell porting item).",
        None,
    ),
    (
        "external-ip-curl",
        re.compile(r"^\s*(curl|wget)\s+.*ifconfig\.me", re.IGNORECASE),
        "auto",
        "curl/wget against ifconfig.me for external IP.",
        "powershell -Command \"(Invoke-WebRequest -Uri 'http://ifconfig.me' "
        "-UseBasicParsing).Content.Trim()\"",
    ),
    (
        "hostname",
        re.compile(r"^\s*hostname\s*$"),
        "auto",
        "hostname.exe exists unchanged on Windows - no rewrite needed "
        "(or just use the built-in ${nodename} object).",
        None,  # no change
    ),
    (
        "whoami",
        re.compile(r"^\s*whoami\s*$"),
        "auto",
        "whoami.exe exists unchanged on Windows - no rewrite needed.",
        None,
    ),
    (
        "uname",
        re.compile(r"^\s*uname\b"),
        "manual",
        "uname has no direct Windows equivalent. Try: "
        "powershell -Command \"(Get-CimInstance Win32_OperatingSystem).Caption\"",
        None,
    ),
    (
        "nproc",
        re.compile(r"^\s*nproc\b|grep\s+-c\s+processor\s+/proc/cpuinfo"),
        "manual",
        "CPU core count. Try: powershell -Command \"$env:NUMBER_OF_PROCESSORS\"",
        None,
    ),
    (
        "date",
        re.compile(r"^\s*date\b"),
        "manual",
        "date command - PowerShell equivalent is Get-Date, but format "
        "specifiers differ (e.g. %Y-%m-%d -> yyyy-MM-dd). Rewrite manually: "
        "powershell -Command \"Get-Date -Format 'yyyy-MM-dd'\"",
        None,
    ),
    (
        "proc-fs",
        re.compile(r"/proc/"),
        "manual",
        "Reads Linux /proc - no direct Windows equivalent. Needs a custom "
        "PowerShell/WMI query, or check if a built-in conky object already "
        "covers this (cpu/mem/disk/net objects usually do).",
        None,
    ),
    (
        "sensors",
        re.compile(r"\bsensors\b"),
        "manual",
        "lm-sensors output - use ${acpitemp} / ${gputemp N} instead.",
        None,
    ),
    (
        "awk-sed",
        re.compile(r"\b(awk|sed)\b"),
        "manual",
        "Uses awk/sed text processing - rewrite using PowerShell string/regex "
        "operators (-replace, Select-String, etc.).",
        None,
    ),
    (
        "shebang",
        re.compile(r"^\s*#!\s*/bin/"),
        "manual",
        "POSIX shell script body - needs a full PowerShell rewrite.",
        None,
    ),
]

EXEC_OBJECT_NAMES = {"exec", "execi", "execp", "execpi", "execbar", "execibar",
                      "execgraph", "execigraph", "execgauge", "execigauge",
                      "texeci", "lua", "lua_parse"}


@dataclass
class Finding:
    line: int
    token: str
    category: str  # OK / REVIEW / UNSUPPORTED / EXEC
    message: str
    applied: bool = False


@dataclass
class Report:
    findings: list[Finding] = field(default_factory=list)
    ok_count: int = 0


def extract_conky_text(source: str) -> tuple[str, int] | None:
    """Return (text_block, start_line_offset) for the conky.text = [[ ]] block."""
    m = re.search(r"conky\.text\s*=\s*\[\[(.*)\]\]", source, re.DOTALL)
    if not m:
        return None
    start_line = source[: m.start(1)].count("\n") + 1
    return m.group(1), start_line


def scan_objects(text: str):
    """Yield (start, end, name, args) for each ${...} in text, brace-depth aware.

    Brace-depth tracking correctly skips over nested ${...}-looking sequences
    inside exec bodies (e.g. bash `${HOME}` inside a `${exec ...}` block) by
    treating them as part of the outer token's argument text rather than
    trying to parse them as separate conky objects.
    """
    i = 0
    n = len(text)
    while i < n:
        if text[i] == "$" and i + 1 < n and text[i + 1] == "{":
            start = i
            depth = 1
            j = i + 2
            while j < n and depth > 0:
                if text[j] == "{":
                    depth += 1
                elif text[j] == "}":
                    depth -= 1
                j += 1
            inner = text[i + 2 : j - 1]
            m = re.match(r"^(\w+)\s*(.*)$", inner, re.DOTALL)
            if m:
                yield start, j, m.group(1), m.group(2)
            i = j
        else:
            i += 1


def classify(name: str, args: str) -> tuple[str, str] | None:
    """Return (category, message) for a non-exec object, or None if plain OK."""
    if name in UNSUPPORTED_EXACT or any(name.startswith(p) for p in UNSUPPORTED_PREFIXES):
        return "UNSUPPORTED", f"${{{name}}} has no Windows equivalent (Linux-only subsystem)."
    if name in REVIEW_OBJECTS:
        return "REVIEW", REVIEW_OBJECTS[name]
    if name in NETWORK_OBJECTS:
        first_arg = args.strip().split()[0] if args.strip() else ""
        if LINUX_IFACE_RE.match(first_arg):
            return (
                "REVIEW",
                f"'{first_arg}' looks like a Linux interface name. Windows uses "
                f"friendly adapter names instead (check `Get-NetAdapter`, e.g. "
                f"'Ethernet' or 'Wi-Fi').",
            )
    return None


def classify_exec(args: str) -> tuple[str, str, str | None]:
    body = args.strip()
    for _name, pattern, category, message, replacement in EXEC_IDIOMS:
        if pattern.search(body):
            return category, message, replacement
    return (
        "manual",
        "Shell command not matched by any known idiom - needs manual review "
        "and likely a PowerShell rewrite (wrap as "
        '${exec powershell -Command "..."}).',
        None,
    )


def lint(source: str) -> tuple[Report, str]:
    """Returns (report, rewritten_source)."""
    extracted = extract_conky_text(source)
    report = Report()
    if extracted is None:
        report.findings.append(
            Finding(0, "", "ERROR",
                     "No `conky.text = [[ ... ]]` block found. This tool only "
                     "handles Lua-format conkyrc files (conky 1.10+). If this "
                     "is an old-style config, convert it to Lua format first.")
        )
        return report, source

    text, line_offset = extracted
    out_chunks = []
    last_end = 0

    for start, end, name, args in scan_objects(text):
        line_no = line_offset + text[:start].count("\n")
        out_chunks.append(text[last_end:start])
        raw_token = text[start:end]

        if name in EXEC_OBJECT_NAMES:
            category, message, replacement = classify_exec(args)
            applied = False
            replacement_token = raw_token
            if category == "auto":
                report.ok_count += 1 if replacement is None else 0
                if replacement is not None:
                    # Preserve any leading interval-number argument for
                    # execi/execpi/texeci (e.g. "${execi 30 <cmd>}").
                    m = re.match(r"^(\s*\d+\s+)?.*$", args, re.DOTALL)
                    prefix = m.group(1) or ""
                    replacement_token = f"${{{name} {prefix}{replacement}}}"
                    applied = True
                report.findings.append(
                    Finding(line_no, raw_token, "EXEC (auto)", message, applied)
                )
            else:
                report.findings.append(Finding(line_no, raw_token, "EXEC (manual)", message))
            out_chunks.append(replacement_token)
            last_end = end
            continue

        result = classify(name, args)
        if result is None:
            report.ok_count += 1
        else:
            category, message = result
            report.findings.append(Finding(line_no, raw_token, category, message))
        out_chunks.append(raw_token)
        last_end = end

    out_chunks.append(text[last_end:])
    rewritten_text = "".join(out_chunks)
    rewritten_source = source.replace(text, rewritten_text, 1) if extracted else source
    return report, rewritten_source


def format_report(report: Report, config_path: str) -> str:
    lines = [f"# Backport report: {config_path}", ""]
    if any(f.category == "ERROR" for f in report.findings):
        for f in report.findings:
            lines.append(f"**ERROR**: {f.message}")
        return "\n".join(lines)

    unsupported = [f for f in report.findings if f.category == "UNSUPPORTED"]
    review = [f for f in report.findings if f.category == "REVIEW"]
    exec_auto = [f for f in report.findings if f.category == "EXEC (auto)"]
    exec_manual = [f for f in report.findings if f.category == "EXEC (manual)"]

    lines.append(
        f"{report.ok_count} object(s) OK as-is, "
        f"{len(unsupported)} unsupported, {len(review)} need review, "
        f"{len(exec_auto)} exec block(s) auto-handled, "
        f"{len(exec_manual)} exec block(s) need manual rewrite."
    )
    lines.append("")

    def section(title: str, items: list[Finding]):
        if not items:
            return
        lines.append(f"## {title} ({len(items)})")
        lines.append("")
        for f in items:
            applied = " - applied to output file" if f.applied else ""
            lines.append(f"- line {f.line}: `{f.token.strip()}`{applied}")
            lines.append(f"  {f.message}")
        lines.append("")

    section("Unsupported objects - no Windows equivalent, needs manual removal/replacement", unsupported)
    section("Objects needing review - work, but have a Windows-specific gotcha", review)
    section("Exec blocks - auto-handled", exec_auto)
    section("Exec blocks - need manual PowerShell rewrite", exec_manual)

    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("config", help="Path to a Linux conkyrc (Lua format)")
    parser.add_argument("-o", "--output", help="Write the (lightly) rewritten conkyrc here")
    parser.add_argument("-r", "--report", help="Write the report here (default: stdout)")
    args = parser.parse_args()

    src_path = Path(args.config)
    source = src_path.read_text(encoding="utf-8")
    report, rewritten = lint(source)
    report_text = format_report(report, str(src_path))

    if args.report:
        Path(args.report).write_text(report_text, encoding="utf-8")
    else:
        print(report_text)

    if args.output:
        Path(args.output).write_text(rewritten, encoding="utf-8")
        print(f"\nWrote rewritten config to {args.output}", file=sys.stderr)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
