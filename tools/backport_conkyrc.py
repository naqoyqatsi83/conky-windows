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
  - UNKNOWN     - not a real conky object at all in this fork (typo, or a
                  long-deprecated/removed upstream object -- see issue #8).
                  Checked against the actual object registry in
                  src/core.cc, not a hand-maintained guess.
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
    "moc_", "cmus_", "xmms2_", "audacious_",  # music players -- mpd_ works (issue #19)
    "apcupsd_",                                        # UPS monitoring
    "pa_",                                              # PulseAudio
    "wireless_",                                        # Linux wireless-tools
    "irc_", "ical_", "ibm_", "smapi_",
)
UNSUPPORTED_EXACT = {
    "i2c", "platform", "hwmon",              # Linux sensors subsystem
    "mysql",
    "top_time", "top_io",  # top/top_mem work (see issue #17); time/io sorting not re-verified,
                            # and io needs BUILD_IOSTATS, off for Windows
    "running_threads",  # threads is supported (see REVIEW below), but "running" (ready-state)
                        # per-thread status isn't exposed by the Toolhelp32 API this port uses
    "image",  # BUILD_IMLIB2 is off; would need a from-scratch GDI+ backend, not a port (issue #24)
    "rss",  # needs libxml2 in addition to curl; curl itself is vendored and on by
            # default now (issue #18), but rss's own dependency isn't vendored yet
    "mixer", "mixerbar", "mixerl", "mixerr", "if_mixer_mute",  # needs Core Audio (issue #21)
    "desktop", "desktop_number", "desktop_name",  # no stable pre-Win11 API (issue #22)
    "user_names", "user_times", "user_number", "user_terms",  # low value, not implemented (issue #23)
}

# Per-object overrides for UNSUPPORTED_EXACT's generic "Linux-only
# subsystem" message, for the handful of cases where that phrasing isn't
# accurate (e.g. rss: not Linux-only, just missing a second vendored
# dependency on top of curl).
UNSUPPORTED_MESSAGES = {
    "rss": "${rss} needs libxml2 in addition to curl (which is now vendored "
           "and on by default, issue #18) -- libxml2 itself isn't vendored "
           "for this toolchain yet. Not a Linux-only subsystem, just an "
           "unfinished dependency.",
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
    "stock": "Works (curl is vendored, issue #18), but the hardcoded endpoint "
             "(download.finance.yahoo.com's old CSV quotes API) has been dead "
             "since ~2017 -- confirmed via a direct request (connection "
             "times out, no response). Not a Windows-specific issue; a "
             "current Linux conky build hits the exact same dead endpoint.",
}

# ---------------------------------------------------------------------------
# Master registry of real conky object names, for telling "not implemented
# on Windows" apart from "not a conky object at all" (issue #8). Objects
# handled via a manual strncmp() dispatch in core.cc rather than the OBJ()
# macro family (only "top"/"top_mem"/"top_time"/"top_io", as of this
# writing -- see the parse_top_args() call in core.cc) are added on top,
# since a regex scan of the macro invocations alone can't see them.
# ---------------------------------------------------------------------------

EXTRA_KNOWN_OBJECTS = {"top", "top_mem", "top_time", "top_io"}

# Snapshot of src/core.cc's OBJ/OBJ_ARG/OBJ_IF/OBJ_IF_ARG registrations,
# used only if core.cc can't be found on disk (e.g. this script copied out
# of the repo) -- _load_known_objects() below re-derives this from source
# whenever possible, so it stays correct as core.cc changes.
_KNOWN_OBJECTS_SNAPSHOT = frozenset({
    "acpiacadapter", "acpifan", "acpitemp", "addr", "addrs", "alignc",
    "alignr", "apcupsd", "apcupsd_cable", "apcupsd_charge", "apcupsd_lastxfer", "apcupsd_linev",
    "apcupsd_load", "apcupsd_loadbar", "apcupsd_loadgauge", "apcupsd_loadgraph", "apcupsd_model", "apcupsd_name",
    "apcupsd_status", "apcupsd_temp", "apcupsd_timeleft", "apcupsd_upsmode", "apm_adapter", "apm_battery_life",
    "apm_battery_time", "audacious_bar", "audacious_bitrate", "audacious_channels", "audacious_filename", "audacious_frequency",
    "audacious_length", "audacious_length_seconds", "audacious_main_volume", "audacious_playlist_length", "audacious_playlist_position", "audacious_position",
    "audacious_position_seconds", "audacious_status", "audacious_title", "battery", "battery_bar", "battery_percent",
    "battery_power_draw", "battery_short", "battery_status", "battery_time", "blink", "buffers",
    "cached", "cat", "catp", "cmdline_to_pid", "cmus_aaa", "cmus_album",
    "cmus_artist", "cmus_curtime", "cmus_date", "cmus_file", "cmus_genre", "cmus_percent",
    "cmus_progress", "cmus_random", "cmus_repeat", "cmus_state", "cmus_timeleft", "cmus_title",
    "cmus_totaltime", "cmus_track", "color", "color0", "color1", "color2",
    "color3", "color4", "color5", "color6", "color7", "color8",
    "color9", "combine", "conky_build_arch", "conky_version", "cpu", "cpubar",
    "cpugauge", "cpugovernor", "cpugraph", "curl", "desktop", "desktop_name",
    "desktop_number", "disk_protect", "diskio", "diskio_read", "diskio_write", "diskiograph",
    "diskiograph_read", "diskiograph_write", "distribution", "downspeed", "downspeedf", "downspeedgraph",
    "draft_mails", "else", "endif", "entropy_avail", "entropy_bar", "entropy_perc",
    "entropy_poolsize", "eval", "exec", "execbar", "execgauge", "execgraph",
    "execi", "execibar", "execigauge", "execigraph", "execp", "execpi",
    "flagged_mails", "font", "font0", "font1", "font2", "font3",
    "font4", "font5", "font6", "font7", "font8", "font9",
    "format_time", "forwarded_mails", "free_bufcache", "free_cached", "freq", "freq2",
    "freq_g", "fs_bar", "fs_bar_free", "fs_free", "fs_free_perc", "fs_size",
    "fs_type", "fs_used", "fs_used_perc", "gid_name", "github_notifications", "goto",
    "gpufan", "gpugraph", "gpumemtotal", "gpumemused", "gpuname", "gputemp",
    "gpuutil", "gw_iface", "gw_ip", "hddtemp", "head", "hr",
    "hwmon", "hwmonbar", "i2c", "i2cbar", "i8k_ac_status", "i8k_bios",
    "i8k_buttons_status", "i8k_cpu_temp", "i8k_left_fan_rpm", "i8k_left_fan_status", "i8k_right_fan_rpm", "i8k_right_fan_status",
    "i8k_serial", "i8k_version", "ibm_brightness", "ibm_fan", "ibm_temps", "ibm_thinklight",
    "ibm_volume", "ical", "iconv_start", "iconv_stop", "if_empty", "if_existing",
    "if_gw", "if_match", "if_mixer_mute", "if_mounted", "if_mpd_playing", "if_pa_sink_muted",
    "if_pa_source_muted", "if_pa_source_running", "if_running", "if_smapi_bat_installed", "if_up", "if_updatenr",
    "if_xmms2_connected", "iface", "image", "imap_messages", "imap_unseen", "intel_backlight",
    "ioscheduler", "irc", "journal", "kernel", "key_caps_lock", "key_num_lock",
    "key_scroll_lock", "keyboard_layout", "laptop_mode", "legacymem", "lines", "loadavg",
    "loadgraph", "lowercase", "lua", "lua_bar", "lua_gauge", "lua_graph",
    "lua_parse", "machine", "mails", "mboxscan", "mem", "memactive",
    "memavail", "membar", "memdirty", "memeasyfree", "memfree", "memgauge",
    "memgraph", "meminactive", "memlaundry", "memmax", "memperc", "memwired",
    "memwithbuffers", "memwithbuffersbar", "memwithbuffersgraph", "mixer", "mixerbar", "mixerl",
    "mixerlbar", "mixerr", "mixerrbar", "moc_album", "moc_artist", "moc_avgbitrate",
    "moc_bar", "moc_bitrate", "moc_cursec", "moc_curtime", "moc_file", "moc_percent",
    "moc_rate", "moc_song", "moc_state", "moc_timeleft", "moc_title", "moc_totalsec",
    "moc_totaltime", "monitor", "monitor_number", "mouse_speed", "mpd_album", "mpd_albumartist",
    "mpd_artist", "mpd_bar", "mpd_bitrate", "mpd_comment", "mpd_date", "mpd_elapsed",
    "mpd_file", "mpd_length", "mpd_name", "mpd_percent", "mpd_random", "mpd_repeat",
    "mpd_smart", "mpd_status", "mpd_title", "mpd_track", "mpd_vol", "mysql",
    "nameserver", "new_mails", "no_update", "nodename", "nodename_short", "nvidia",
    "nvidiabar", "nvidiagauge", "nvidiagraph", "obsd_product", "obsd_sensors_fan", "obsd_sensors_temp",
    "obsd_sensors_volt", "obsd_vendor", "offset", "outlinecolor", "pa_card_active_profile", "pa_card_name",
    "pa_sink_active_port_description", "pa_sink_active_port_name", "pa_sink_description", "pa_sink_volume", "pa_sink_volumebar", "password",
    "pb_battery", "pid_chroot", "pid_cmdline", "pid_cwd", "pid_egid", "pid_environ",
    "pid_environ_list", "pid_euid", "pid_exe", "pid_fsgid", "pid_fsuid", "pid_gid",
    "pid_nice", "pid_openfiles", "pid_parent", "pid_priority", "pid_read", "pid_sgid",
    "pid_state", "pid_state_short", "pid_stderr", "pid_stdin", "pid_stdout", "pid_suid",
    "pid_thread_list", "pid_threads", "pid_time", "pid_time_kernelmode", "pid_time_usermode", "pid_uid",
    "pid_vmdata", "pid_vmexe", "pid_vmhwm", "pid_vmlck", "pid_vmlib", "pid_vmpeak",
    "pid_vmpte", "pid_vmrss", "pid_vmsize", "pid_vmstk", "pid_write", "platform",
    "platformbar", "pop3_unseen", "pop3_used", "processes", "read_tcp", "read_udp",
    "replied_mails", "rss", "rstrip", "running_processes", "running_threads", "save_coordinates",
    "scroll", "seen_mails", "shadecolor", "shmem", "sip_status", "smapi",
    "smapi_bat_bar", "smapi_bat_perc", "smapi_bat_power", "smapi_bat_temp", "sony_fanspeed", "start_case",
    "startcase", "stippled_hr", "stock", "swap", "swapbar", "swapfree",
    "swapmax", "swapperc", "sysctlbyname", "sysname", "tab", "tail",
    "tcp_ping", "tcp_portmon", "texeci", "texecpi", "threads", "time",
    "to_bytes", "totaldown", "totalup", "trashed_mails", "tztime", "uid_name",
    "unflagged_mails", "unforwarded_mails", "unreplied_mails", "unseen_mails", "updates", "uppercase",
    "upspeed", "upspeedf", "upspeedgraph", "uptime", "uptime_short", "user_names",
    "user_number", "user_terms", "user_time", "user_times", "utime", "v6addrs",
    "version", "voffset", "voltage_mv", "voltage_v", "wireless_ap", "wireless_bitrate",
    "wireless_channel", "wireless_essid", "wireless_freq", "wireless_link_bar", "wireless_link_qual", "wireless_link_qual_max",
    "wireless_link_qual_perc", "wireless_mode", "words", "xmms2_album", "xmms2_artist", "xmms2_bar",
    "xmms2_bitrate", "xmms2_comment", "xmms2_date", "xmms2_duration", "xmms2_elapsed", "xmms2_genre",
    "xmms2_id", "xmms2_percent", "xmms2_playlist", "xmms2_size", "xmms2_smart", "xmms2_status",
    "xmms2_timesplayed", "xmms2_title", "xmms2_tracknr", "xmms2_url",
}) | EXTRA_KNOWN_OBJECTS


def _load_known_objects() -> frozenset[str]:
    """Return the master set of real conky object names.

    Parsed fresh from src/core.cc's OBJ/OBJ_ARG/OBJ_IF/OBJ_IF_ARG
    registration macros when the repo is available, so this list stays
    correct as core.cc changes without needing to be hand-updated. Falls
    back to a baked-in snapshot if core.cc can't be found (e.g. this
    script copied out of the repo tree).
    """
    core_cc = Path(__file__).resolve().parent.parent / "src" / "core.cc"
    try:
        source = core_cc.read_text(encoding="utf-8")
    except OSError:
        return _KNOWN_OBJECTS_SNAPSHOT

    # The OBJ/OBJ_ARG/OBJ_IF/OBJ_IF_ARG #define block itself (right after
    # gen_text_object_internal's setup) uses "a" as a placeholder macro
    # parameter, not a real object name -- only scan past it.
    define_end = source.find("#define END")
    if define_end == -1:
        return _KNOWN_OBJECTS_SNAPSHOT
    body = source[define_end:]
    # OBJ_ARG(...) calls are sometimes clang-format-wrapped across lines
    # (e.g. "OBJ_ARG(\n      nvidiagraph, 0,\n      ...")," so allow
    # whitespace (including newlines) between "(" and the object name.
    names = set(re.findall(r"OBJ(?:_IF)?(?:_ARG)?\(\s*([a-zA-Z_]\w*)", body))
    if not names:
        return _KNOWN_OBJECTS_SNAPSHOT
    return frozenset(names) | EXTRA_KNOWN_OBJECTS


KNOWN_OBJECTS = _load_known_objects()

# Objects whose first argument is commonly a Linux network interface name on
# Linux themes (eth0, wlan0, enp3s0, ...) - Windows uses friendly adapter
# names instead (Ethernet, Wi-Fi, ...).
NETWORK_OBJECTS = {
    "downspeed", "upspeed", "downspeedf", "upspeedf", "downspeedgraph",
    "upspeedgraph", "totaldown", "totalup", "addr", "addrs",
}
LINUX_IFACE_RE = re.compile(r"^(eth\d+|wlan\d+|en[ospx]\w*|wl[ospx]\w*|lo)$")

# Objects whose argument is a Linux block device path (no Windows
# equivalent by device path at all -- needs a manual rework using a drive
# letter or physical disk index instead).
DISKIO_OBJECTS = {"diskio", "diskio_read", "diskio_write", "diskiograph",
                   "diskio_read_graph", "diskio_write_graph"}

# Objects whose argument is a filesystem/mount path on Linux, but a drive
# letter (e.g. "C:") on Windows -- a mechanical rename once you know which
# drive it maps to.
FS_OBJECTS = {"fs_used", "fs_free", "fs_free_perc", "fs_used_perc",
              "fs_size", "fs_bar", "fs_bar_free", "fs_type"}

UNIX_PATH_RE = re.compile(r"^/")
WINDOWS_DRIVE_RE = re.compile(r"^[A-Za-z]:\\?$")

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

# ---------------------------------------------------------------------------
# conky.config = { ... } settings with a Windows-specific gotcha. This port
# always renders as a single WS_POPUP layered window with per-pixel alpha
# (see src/output/display-windows.cc) regardless of most window-placement
# settings, so X11/WM-specific config is easy to port "successfully" (it
# parses fine, nothing errors) while silently doing nothing.
# ---------------------------------------------------------------------------

CONFIG_SETTINGS = {
    "own_window_type": lambda v: (
        None if v.strip("'\"") in ("", "normal") else
        f"own_window_type = {v} has no effect on Windows -- this port always "
        f"renders as a single layered popup window regardless of this setting."
    ),
    "own_window_hints": lambda v: (
        None if not v.strip("'\" ") else
        f"own_window_hints = {v}: X11 window-manager hints (skip_taskbar, "
        f"below, sticky, etc.) have no Windows equivalent in this port and "
        f"are silently ignored."
    ),
    "xinerama_head": lambda v: (
        f"xinerama_head = {v}: Xinerama is an X11-only multi-monitor "
        f"extension with no Windows equivalent in this port -- silently "
        f"ignored. This port always positions on the primary monitor's "
        f"work area (see AGENTS.md); there's currently no Windows setting "
        f"to pick a different monitor by index."
    ),
    "lua_draw_hook_pre": lambda v: (
        None if not v.strip("'\" ") else
        f"lua_draw_hook_pre = {v}: Cairo Lua draw hooks are supported on "
        f"this port. A script calling cairo_xlib_surface_create() directly "
        f"(the common X11 pattern) gets its X11 display/drawable/visual "
        f"arguments ignored and is transparently redirected to the "
        f"portable conky_surface() -- no theme edits needed. Content draws "
        f"correctly same-frame. New scripts should prefer conky_surface() "
        f"directly (see upstream issue #10)."
    ),
    "lua_draw_hook_post": lambda v: (
        None if not v.strip("'\" ") else
        f"lua_draw_hook_post = {v}: supported like lua_draw_hook_pre, with "
        f"one caveat -- content drawn here is composited starting on the "
        f"*next* frame (a brief lag) rather than the same frame, and layers "
        f"underneath conky's own text/graphics rather than on top like "
        f"upstream 'post' semantics. Usually imperceptible at typical "
        f"update_interval values (see upstream issue #10)."
    ),
}


def extract_conky_config(source: str) -> str | None:
    """Return the raw text inside conky.config = { ... }, or None."""
    m = re.search(r"conky\.config\s*=\s*\{(.*?)\n\}", source, re.DOTALL)
    return m.group(1) if m else None


def lint_config_section(source: str) -> list[Finding]:
    findings = []

    if is_old_style(source):
        # Old format: one `key value` setting per line, value is the rest
        # of the line (no quotes, no comma terminator), up to the bare
        # `TEXT` marker.
        text_start = re.search(r"^TEXT\s*$", source, re.MULTILINE)
        config_text = source[: text_start.start()] if text_start else source
        config_start = 0
        pattern = re.compile(r"^\s*(\w+)\s+(.+?)\s*$", re.MULTILINE)
    else:
        config_text = extract_conky_config(source)
        if config_text is None:
            return []
        config_start = source.index(config_text)
        # Value is either a quoted string (which may itself contain commas,
        # e.g. own_window_hints = 'undecorated,skip_taskbar,...') or a bare
        # comma-terminated token (numbers, true/false, identifiers).
        value_pattern = r"'(?:[^'\\]|\\.)*'|\"(?:[^\"\\]|\\.)*\"|[^,\n]+"
        pattern = re.compile(rf"^\s*(\w+)\s*=\s*({value_pattern}),?\s*$", re.MULTILINE)

    for m in pattern.finditer(config_text):
        key, value = m.group(1), m.group(2).strip()
        checker = CONFIG_SETTINGS.get(key)
        if checker is None:
            continue
        message = checker(value)
        if message is None:
            continue
        line_no = source[: config_start + m.start()].count("\n") + 1
        findings.append(Finding(line_no, f"{key} {value}", "CONFIG", message))
    return findings


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
    """Return (text_block, start_line_offset) for the template text.

    Handles both the modern Lua format (`conky.text = [[ ... ]]`) and the
    old, pre-1.10 format (a bare `TEXT` line, then everything to EOF is the
    template -- no closing marker). BUILD_OLD_CONFIG is on by default in
    this port (cmake/ConkyBuildOptions.cmake) and this port's conky.exe
    loads old-format files directly, so they're a real backport target, not
    just something to reject. See conky-windows issue #7.
    """
    m = re.search(r"conky\.text\s*=\s*\[\[(.*)\]\]", source, re.DOTALL)
    if m:
        start_line = source[: m.start(1)].count("\n") + 1
        return m.group(1), start_line

    m = re.search(r"^TEXT\s*$", source, re.MULTILINE)
    if m:
        start_line = source[: m.end()].count("\n") + 1
        return source[m.end() :], start_line

    return None


def is_old_style(source: str) -> bool:
    return not re.search(r"conky\.text\s*=\s*\[\[", source) and bool(
        re.search(r"^TEXT\s*$", source, re.MULTILINE)
    )


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
    if name not in KNOWN_OBJECTS and name not in UNSUPPORTED_EXACT and not any(
        name.startswith(p) for p in UNSUPPORTED_PREFIXES
    ):
        return (
            "UNKNOWN",
            f"${{{name}}} isn't a real conky object in this fork's registry "
            f"(src/core.cc) -- likely a typo, or a long-deprecated/removed "
            f"upstream object. Not a Windows-port gap; a modern Linux conky "
            f"build would show the same unparsed text (see issue #8).",
        )
    if name in UNSUPPORTED_EXACT or any(name.startswith(p) for p in UNSUPPORTED_PREFIXES):
        if name in UNSUPPORTED_MESSAGES:
            return "UNSUPPORTED", UNSUPPORTED_MESSAGES[name]
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
    if name in DISKIO_OBJECTS:
        first_arg = args.strip().split()[0] if args.strip() else ""
        if UNIX_PATH_RE.match(first_arg) and not WINDOWS_DRIVE_RE.match(first_arg):
            return (
                "REVIEW",
                f"'{first_arg}' is a Linux block device path -- ${{{name}}} works "
                f"on Windows, but there's no equivalent by device path. Needs a "
                f"manual rework using a drive letter (e.g. 'C:') or physical disk "
                f"index instead.",
            )
    if name in FS_OBJECTS:
        tokens = args.strip().split()
        # fs_bar/fs_bar_free take a leading "height[,width]" size spec before
        # the path; the others take just the path as their only argument.
        path_arg = tokens[-1] if name in ("fs_bar", "fs_bar_free") else (
            tokens[0] if tokens else ""
        )
        if UNIX_PATH_RE.match(path_arg) and not WINDOWS_DRIVE_RE.match(path_arg):
            return (
                "REVIEW",
                f"'{path_arg}' is a Linux mount path. Windows ${{{name}}} takes a "
                f"drive letter instead (e.g. 'C:') -- a mechanical rename once you "
                f"know which drive this mount point maps to.",
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
    report.findings.extend(lint_config_section(source))
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
    unknown = [f for f in report.findings if f.category == "UNKNOWN"]
    review = [f for f in report.findings if f.category == "REVIEW"]
    exec_auto = [f for f in report.findings if f.category == "EXEC (auto)"]
    exec_manual = [f for f in report.findings if f.category == "EXEC (manual)"]
    config = [f for f in report.findings if f.category == "CONFIG"]

    lines.append(
        f"{report.ok_count} object(s) OK as-is, "
        f"{len(unsupported)} unsupported, {len(unknown)} unrecognized "
        f"(not a real conky object), {len(review)} need review, "
        f"{len(exec_auto)} exec block(s) auto-handled, "
        f"{len(exec_manual)} exec block(s) need manual rewrite, "
        f"{len(config)} conky.config setting(s) flagged."
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

    section("conky.config settings needing review", config)
    section("Unsupported objects - no Windows equivalent, needs manual removal/replacement", unsupported)
    section("Unrecognized objects - not a real conky object (typo, or deprecated upstream)", unknown)
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
