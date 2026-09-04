#!/usr/bin/env python3
"""Extract per-rig calibration tables from rigs/*.rig into a JS module.

Reads the QSettings-style INI files under rigs/ and emits
resources/web-standalone/civ/rig-caps.js — a global IIFE that publishes
`window.IcomRigCaps` keyed by CI-V address.

Each entry has:
    civAddr (number, hex display)
    model   (string)
    meters  ({sMeter, swr, power, alc, comp, center, voltage, current}
             — each a list of [rigVal, actualVal] pairs, sorted by rigVal)
    preamps / attenuators ([{num, name}, ...] — Num is the CI-V byte)

We only emit Icom rigs (Manufacturer = 0 or "Icom"). Run after editing
.rig files and check the result in:

    resources/web-standalone/civ/rig-caps.js
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
RIGS_DIR = REPO / "rigs"
OUT = REPO / "resources/web-standalone/civ/rig-caps.js"

# Map .rig "Meter=..." labels -> our JS field names.
METER_FIELDS = {
    "S-Meter": "sMeter",
    "SWR":     "swr",
    "Power":   "power",
    "ALC":     "alc",
    "Comp":    "comp",
    "Center":  "center",
    "Voltage": "voltage",
    "Current": "current",
}


def parse_ini(path: Path) -> dict[str, str]:
    """Tiny QSettings-INI parser. We only care about flat key=value pairs."""
    out: dict[str, str] = {}
    section = ""
    with path.open(encoding="utf-8", errors="replace") as f:
        for raw in f:
            line = raw.strip()
            if not line or line.startswith(";") or line.startswith("#"):
                continue
            if line.startswith("[") and line.endswith("]"):
                section = line[1:-1]
                continue
            if "=" not in line:
                continue
            k, _, v = line.partition("=")
            # QSettings flattens nested keys into "Section/Foo\\1\\Bar"
            # but inside a section header [Foo] it's just "Foo\\1\\Bar=...".
            if section and section.lower() != "general":
                k = f"{section}/{k}"
            out[k.strip()] = v.strip()
    return out


def is_icom(props: dict[str, str]) -> bool:
    mfg = props.get("Rig/Manufacturer", "")
    return mfg in ("0", "Icom")


def parse_civ_string(s: str) -> list[int] | None:
    """Decode the Commands\\N\\String= field.

    On disk the bytes look like '\\\\x1a\\\\x05' (each byte = literal backslash,
    backslash, x, hex-digit, hex-digit) — five characters per byte. QSettings
    writes the leading backslash escaped, so we strip the doubled form first.
    """
    if not s:
        return None
    s = s.replace("\\\\", "\\")
    out: list[int] = []
    i = 0
    while i < len(s):
        if s[i] == "\\" and i + 3 < len(s) and s[i + 1] == "x":
            try:
                out.append(int(s[i + 2:i + 4], 16))
            except ValueError:
                return None
            i += 4
        else:
            return None
    return out


# Command-Type labels we want to capture verbatim (CI-V byte sequences).
# Shape: { rig-caps key: ".rig file Type label" }
EXTRACTED_CMDS = {
    "modOff":   "Data Off Mod Input",
    "modData1": "DATA1 Mod Input",
    "modData2": "DATA2 Mod Input",
    "modData3": "DATA3 Mod Input",
    # Antenna selector bytes differ per rig ([0x12] vs IC-7300MK2's
    # [0x12, 0x00]); rxAntenna presence also gates the 0x12 rx flag byte.
    "antenna":   "Antenna",
    "rxAntenna": "RX Antenna",
    # Repeater access tone. Two dialects: the newer rigs (IC-705/9700/905)
    # carry the whole TONE/TSQL/DTCS selection in one "Tone Squelch Type"
    # register (0x16 0x5D); the rest toggle independent booleans (0x16 0x42
    # TONE, 0x43 TSQL, 0x4B DTCS). The sub-command byte differs per rig, so
    # capture the sequences rather than hardcoding them.
    "toneSqlType": "Tone Squelch Type",
    "rptTone":     "Repeater Tone",
    "rptTsql":     "Repeater TSQL",
    "rptDtcs":     "Repeater DTCS",
    # Tone / DTCS frequency registers (0x1B 0x00/0x01/0x02).
    "toneFreq":  "Tone Frequency",
    "tsqlFreq":  "TSQL Frequency",
    "dtcsCode":  "DTCS Code/Polarity",
    # Squelch open/closed readout (0x15 0x05) — the only way to tell that a
    # tone scan has found the repeater's tone.
    "sqlStatus": "Various Squelch",
}


# Command labels that say something about repeater-tone capability.
TONE_CMD_LABELS = {
    "Tone Squelch Type", "Repeater Tone", "Repeater TSQL", "Repeater DTCS",
    "Tone Frequency", "TSQL Frequency", "DTCS Code/Polarity", "Various Squelch",
}


def extract_commands(props: dict[str, str]) -> dict[str, list[int]]:
    """Pull selected Commands\\N\\String byte sequences by Type label."""
    out: dict[str, list[int]] = {}
    pat = re.compile(r"^Rig/Commands\\(\d+)\\(\w+)$")
    found: dict[int, dict[str, str]] = {}
    for k, v in props.items():
        m = pat.match(k)
        if not m:
            continue
        idx = int(m.group(1))
        field = m.group(2)
        found.setdefault(idx, {})[field] = v
    label_to_key = {label: key for key, label in EXTRACTED_CMDS.items()}
    for idx, e in found.items():
        label = e.get("Type", "")
        key = label_to_key.get(label)
        if not key:
            continue
        bytes_ = parse_civ_string(e.get("String", ""))
        if bytes_ is None:
            continue
        out[key] = bytes_
    return out


def normalize_input_name(name: str) -> str:
    """Normalize a .rig input Name to a JS identifier key.

    'USB' -> 'USB', 'M/U' -> 'MU', '"MIC, ACC"' -> 'MICACC'. The Name
    field is the authoritative label for what a Reg value selects; the
    Num field is an enum index that is NOT consistent across .rig files
    (e.g. IC-705 has USB at Num=1, which rigidentities.h calls ACCA, and
    WLAN at Num=3, which it calls USB — keying by Num wrote USB:0x03 and
    made standalone TX silent, issue #79).
    """
    return re.sub(r"[^A-Z0-9]", "", name.upper())


def extract_num_name_list(props: dict[str, str], prefix: str) -> list[dict]:
    """Walk Rig/<prefix>\\N\\{Num,Name,Start,End} entries.

    Returns [{num, name, minFreq?, maxFreq?}, ...] in .rig order — the
    OFF/0-dB entry included, since the SPA cycle code treats index 0 as
    the "off" step.
    """
    pat = re.compile(rf"^Rig/{prefix}\\(\d+)\\(\w+)$")
    found: dict[int, dict[str, str]] = {}
    for k, v in props.items():
        m = pat.match(k)
        if not m:
            continue
        idx = int(m.group(1))
        field = m.group(2)
        found.setdefault(idx, {})[field] = v
    out: list[tuple[int, dict]] = []
    for idx, e in found.items():
        try:
            num = int(e.get("Num", "-1"))
        except ValueError:
            continue
        if num < 0:
            continue
        name = e.get("Name", "").strip()
        item = {"num": num, "name": name}
        # Optional Start/End (Hz) restrict an entry to part of the rig's
        # coverage — the IC-705 only offers PREAMP 2 below 74.8 MHz.
        for field, key in (("Start", "minFreq"), ("End", "maxFreq")):
            try:
                hz = int(e.get(field, "0"))
            except ValueError:
                continue
            if hz > 0:
                item[key] = hz
        out.append((idx, item))
    out.sort(key=lambda t: t[0])
    return [d for _, d in out]


def extract_inputs(props: dict[str, str]) -> dict[str, int]:
    """Walk Inputs\\N\\... entries — each gives a Name -> Reg (byte) map.

    We return a name-keyed dict so the JS side can look up by input kind
    (e.g., 'USB') without knowing the register value. Keys come from the
    .rig Name field (see normalize_input_name), never from the Num enum.
    """
    pat = re.compile(r"^Rig/Inputs\\(\d+)\\(\w+)$")
    found: dict[int, dict[str, str]] = {}
    for k, v in props.items():
        m = pat.match(k)
        if not m:
            continue
        idx = int(m.group(1))
        field = m.group(2)
        found.setdefault(idx, {})[field] = v
    out: dict[str, int] = {}
    for idx in sorted(found):
        e = found[idx]
        try:
            reg = int(e.get("Reg", "-1"))
        except ValueError:
            continue
        name = normalize_input_name(e.get("Name", "").strip().strip('"'))
        if name and reg >= 0:
            out[name] = reg
    return out


def extract_tones(props: dict[str, str], prefix: str) -> list[int]:
    """Walk CTCSS\\N\\Reg / DTCS\\N\\Reg into an ordered list of registers.

    CTCSS registers are tenths of Hz (670 = 67.0 Hz); DTCS registers are the
    code itself (23 = D023). The .rig files also carry a Tone field for CTCSS,
    but it is just Reg/10 — the JS side formats the label from Reg.
    """
    pat = re.compile(rf"^Rig/{prefix}\\(\d+)\\Reg$")
    found: dict[int, int] = {}
    for k, v in props.items():
        m = pat.match(k)
        if not m:
            continue
        try:
            found[int(m.group(1))] = int(v)
        except ValueError:
            continue
    return [found[i] for i in sorted(found)]


def extract_caps(props: dict[str, str]) -> dict:
    """Pull boolean / numeric capability flags from the [Rig] section."""
    def b(key: str, default: bool = False) -> bool:
        v = props.get(f"Rig/{key}", "").strip().lower()
        if v in ("true", "1"): return True
        if v in ("false", "0"): return False
        return default
    def n(key: str, default: int = 0) -> int:
        try:
            return int(props.get(f"Rig/{key}", str(default)))
        except ValueError:
            return default
    # Detect support for the 0x25 0x00 / 0x25 0x01 commands ("Selected Freq" /
    # "Unselected Freq"). On A/B-VFO Icoms (IC-7300, IC-705, …) these let the
    # standalone transport read both VFOs without flipping the rig's selection.
    # "Send Freq Offset" (CI-V 0x0D) is what lets a rig shift its TX frequency
    # for a repeater, so it is the honest test for whether the DUP tile has
    # anything to drive. "Tuner/ATU Status" (CI-V 0x1C 0x01) plays the same
    # role for the TUNE tile: rigs with no tuner (IC-9700/905, the receivers,
    # the older HF rigs) never declare it.
    #
    # Repeater access tone works the same way, but takes two facts: a rig needs
    # a way to engage the tone (either the one-register "Tone Squelch Type" or
    # the per-function TONE/TSQL/DTCS booleans) AND a tone table in its .rig.
    # "Various Squelch" (0x15 0x05) reports squelch open/closed, which is what
    # makes a software tone scan possible — the IC-9100 and IC-7100 lack it.
    has_selected_freq = False
    has_duplex = False
    has_tuner = False
    tone_cmds: set[str] = set()
    pat = re.compile(r"^Rig/Commands\\(\d+)\\Type$")
    for k, v in props.items():
        if not pat.match(k):
            continue
        if v.strip() == "Selected Freq":
            has_selected_freq = True
        elif v.strip() == "Send Freq Offset":
            has_duplex = True
        elif v.strip() == "Tuner/ATU Status":
            has_tuner = True
        elif v.strip() in TONE_CMD_LABELS:
            tone_cmds.add(v.strip())
    can_engage_tone = bool(tone_cmds & {
        "Tone Squelch Type", "Repeater Tone", "Repeater TSQL", "Repeater DTCS"})
    return {
        "hasTransmit": b("HasTransmit", True),
        "hasSpectrum": b("HasSpectrum", False),
        "hasLAN":      b("HasLAN", False),
        "numReceivers": n("NumberOfReceivers", 1),
        "numVFOs":      n("NumberOfVFOs", 1),
        # Cmd29 (Main/Sub prefix) rigs — IC-7610 / IC-785x / IC-7760. The
        # 0x29 0x00 / 0x29 0x01 prefix scopes the next CI-V command to the
        # Main or Sub receiver respectively.
        "hasCommand29": b("HasCommand29", False),
        "hasSelectedFreq": has_selected_freq,
        "hasDuplex": has_duplex,
        "hasTuner": has_tuner,
        "hasCTCSS": can_engage_tone and bool(extract_tones(props, "CTCSS")),
        "hasDTCS": (can_engage_tone and bool(extract_tones(props, "DTCS"))
                    and "DTCS Code/Polarity" in tone_cmds),
        "hasToneSqlType": "Tone Squelch Type" in tone_cmds,
    }


def extract_meters(props: dict[str, str]) -> dict[str, list[list[float]]]:
    """Walk Rig/Meters\\N\\... triplets and bucket into our field names."""
    meters: dict[str, list[tuple[int, float]]] = {k: [] for k in METER_FIELDS.values()}
    pat = re.compile(r"^Rig/Meters\\(\d+)\\(\w+)$")
    found: dict[int, dict[str, str]] = {}
    for k, v in props.items():
        m = pat.match(k)
        if not m:
            continue
        idx = int(m.group(1))
        field = m.group(2)
        found.setdefault(idx, {})[field] = v
    for idx, e in sorted(found.items()):
        label = e.get("Meter", "")
        field = METER_FIELDS.get(label)
        if not field:
            continue
        try:
            rig_val = int(e["RigVal"])
            act_val = float(e["ActualVal"])
        except (KeyError, ValueError):
            continue
        meters[field].append((rig_val, act_val))
    # sort each table by rigVal; drop empties
    return {
        f: [[rv, av] for rv, av in sorted(set(pts))]
        for f, pts in meters.items()
        if pts
    }


def js_pairs(pts: list[list[float]]) -> str:
    return "[" + ",".join(f"[{rv},{av:g}]" for rv, av in pts) + "]"


def js_meters(meters: dict[str, list[list[float]]]) -> str:
    parts = [f"{k}:{js_pairs(v)}" for k, v in meters.items()]
    return "{" + ",".join(parts) + "}"


def js_byte_seq(bs: list[int]) -> str:
    return "[" + ",".join(f"0x{b:02x}" for b in bs) + "]"


def js_commands(cmds: dict[str, list[int]]) -> str:
    parts = [f"{k}:{js_byte_seq(v)}" for k, v in cmds.items()]
    return "{" + ",".join(parts) + "}"


def js_inputs(inputs: dict[str, int]) -> str:
    parts = [f"{k}:0x{v:02x}" for k, v in inputs.items()]
    return "{" + ",".join(parts) + "}"


def js_num_name_list(items: list[dict]) -> str:
    parts = []
    for e in items:
        fields = f"num:{e['num']},name:{js_string(e['name'])}"
        for key in ("minFreq", "maxFreq"):
            if e.get(key):
                fields += f",{key}:{e[key]}"
        parts.append("{" + fields + "}")
    return "[" + ",".join(parts) + "]"


def js_caps(caps: dict) -> str:
    parts = []
    for k, v in caps.items():
        if isinstance(v, bool):
            parts.append(f"{k}:{'true' if v else 'false'}")
        else:
            parts.append(f"{k}:{v}")
    return "{" + ",".join(parts) + "}"


def js_int_list(vals: list[int]) -> str:
    return "[" + ",".join(str(v) for v in vals) + "]"


def js_string(s: str) -> str:
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def main() -> int:
    rigs: list[tuple[int, str, dict, dict, dict, dict, list, list, list]] = []
    ctcss: list[int] = []
    dtcs: list[int] = []
    for p in sorted(RIGS_DIR.glob("*.rig")):
        props = parse_ini(p)
        if not is_icom(props):
            continue
        try:
            civ = int(props.get("Rig/CIVAddress", "0"))
        except ValueError:
            continue
        if civ == 0:
            continue  # DEFAULT-ICOM has no real address
        model = props.get("Rig/Model", p.stem)
        meters = extract_meters(props)
        cmds = extract_commands(props)
        caps = extract_caps(props)
        inputs = extract_inputs(props)
        preamps = extract_num_name_list(props, "Preamps")
        attenuators = extract_num_name_list(props, "Attenuators")
        antennas = extract_num_name_list(props, "Antennas")
        rigs.append((civ, model, meters, cmds, caps, inputs, preamps, attenuators, antennas))
        # The CTCSS (48) and DTCS (104) tables are the standard sets and are
        # byte-identical in every Icom .rig, so they are emitted once instead
        # of being repeated per rig. Bail loudly if that ever stops holding —
        # a silently wrong tone list would key the wrong repeater.
        for prefix, shared in (("CTCSS", ctcss), ("DTCS", dtcs)):
            table = extract_tones(props, prefix)
            if not table:
                continue
            if not shared:
                shared.extend(table)
            elif table != shared:
                print(f"error: {p.name} has a {prefix} table that differs from the "
                      f"shared one; per-rig tone tables are not supported yet",
                      file=sys.stderr)
                return 1

    rigs.sort(key=lambda r: r[0])

    lines = [
        "// Auto-generated by tools/extract-rig-caps.py — do NOT hand-edit.",
        "// Source: rigs/*.rig (Icom only). Re-run the script after editing those.",
        "//",
        "// Each entry: civAddr -> { model, caps, meters, cmds, inputs, preamps, attenuators, antennas }",
        "//   caps:   { hasTransmit, hasSpectrum, hasLAN, numReceivers, numVFOs,",
        "//             hasCommand29, hasSelectedFreq, hasDuplex, hasTuner,",
        "//             hasCTCSS, hasDTCS, hasToneSqlType }",
        "//   meters: { kind: [[rigVal, actualVal], ...] }",
        "//           kinds: sMeter, swr, power, alc, comp, center, voltage, current",
        "//   cmds:   { modOff, modData1, modData2, modData3, antenna, rxAntenna,",
        "//             toneSqlType, rptTone, rptTsql, rptDtcs, toneFreq, tsqlFreq,",
        "//             dtcsCode, sqlStatus }",
        "//           -> CI-V byte sequence",
        "//   inputs: name -> reg byte. The JS side writes <prefix>+<reg> to",
        "//           switch the rig's modulation source (e.g., USB).",
        "//           Names: MIC, ACCA, ACCB, USB, LAN, MICUSB, ACCUSB, …",
        "//   preamps / attenuators / antennas: [{num, name}, ...] — Num is the",
        "//           CI-V byte; the SPA's cycle buttons walk the lists.",
        "",
        "(function (global) {",
        "    'use strict';",
        "    // Standard CTCSS tones in tenths of Hz (670 = 67.0) and DTCS codes,",
        "    // shared by every Icom that has them. Index order is the rig's own.",
        "    global.IcomCtcssTones = " + js_int_list(ctcss) + ";",
        "    global.IcomDtcsCodes = " + js_int_list(dtcs) + ";",
        "    global.IcomRigCaps = {",
    ]
    for civ, model, meters, cmds, caps, inputs, preamps, attenuators, antennas in rigs:
        lines.append(
            f"        0x{civ:02X}: {{ model: {js_string(model)}, "
            f"caps: {js_caps(caps)}, "
            f"meters: {js_meters(meters)}, "
            f"cmds: {js_commands(cmds)}, "
            f"inputs: {js_inputs(inputs)}, "
            f"preamps: {js_num_name_list(preamps)}, "
            f"attenuators: {js_num_name_list(attenuators)}, "
            f"antennas: {js_num_name_list(antennas)} }},"
        )
    lines += [
        "    };",
        "})(typeof window !== 'undefined' ? window : globalThis);",
        "",
    ]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT.relative_to(REPO)} — {len(rigs)} Icom rigs")
    for civ, model, meters, cmds, caps, inputs, preamps, attenuators, antennas in rigs:
        flags = []
        if not caps["hasTransmit"]: flags.append("RX-only")
        if caps["hasSpectrum"]: flags.append("scope")
        if caps["hasLAN"]: flags.append("LAN")
        flag = "  [" + ",".join(flags) + "]" if flags else ""
        usb = inputs.get("USB", -1)
        usb_str = f"0x{usb:02x}" if usb >= 0 else "-"
        pa = f"PA={len(preamps)}" if preamps else "PA=-"
        at = f"ATT={len(attenuators)}" if attenuators else "ATT=-"
        an = f"ANT={len(antennas)}" if antennas else "ANT=-"
        print(f"  0x{civ:02X}  {model:<18}{flag}  USB-reg={usb_str}  {pa} {at} {an}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
