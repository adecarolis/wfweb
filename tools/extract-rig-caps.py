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


# inputType enum from include/rigidentities.h. We only really need USB
# (the modulation source the browser audio path drives) but extract a few
# more so the code can swap more easily later.
INPUT_NAMES = {
    0: "MIC", 1: "ACCA", 2: "ACCB", 3: "USB", 4: "LAN",
    5: "MICACCA", 6: "MICACCB", 7: "ACCAACCB", 8: "MICACCAACCB",
    9: "SPDIF", 10: "MICUSB", 11: "AV", 12: "MICAV",
    13: "ACCUSB", 14: "LINE",
}


def extract_num_name_list(props: dict[str, str], prefix: str) -> list[dict]:
    """Walk Rig/<prefix>\\N\\{Num,Name} entries and return [{num, name}, ...].

    Excludes the OFF/0-dB entry (Num=0) — the SPA cycle code uses Num=0 as
    the "off" value already, so listing it would create a redundant cycle step.
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
        out.append((idx, {"num": num, "name": name}))
    out.sort(key=lambda t: t[0])
    return [d for _, d in out]


def extract_inputs(props: dict[str, str]) -> dict[str, int]:
    """Walk Inputs\\N\\... entries — each gives a Num (enum) -> Reg (byte) map.

    We return a name-keyed dict so the JS side can look up by input kind
    (e.g., 'USB') without knowing the enum value.
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
    for idx, e in found.items():
        try:
            num = int(e.get("Num", "-1"))
            reg = int(e.get("Reg", "-1"))
        except ValueError:
            continue
        name = INPUT_NAMES.get(num)
        if name and reg >= 0:
            out[name] = reg
    return out


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
    return {
        "hasTransmit": b("HasTransmit", True),
        "hasSpectrum": b("HasSpectrum", False),
        "hasLAN":      b("HasLAN", False),
        "numReceivers": n("NumberOfReceivers", 1),
        "numVFOs":      n("NumberOfVFOs", 1),
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
    parts = [f"{{num:{e['num']},name:{js_string(e['name'])}}}" for e in items]
    return "[" + ",".join(parts) + "]"


def js_caps(caps: dict) -> str:
    parts = []
    for k, v in caps.items():
        if isinstance(v, bool):
            parts.append(f"{k}:{'true' if v else 'false'}")
        else:
            parts.append(f"{k}:{v}")
    return "{" + ",".join(parts) + "}"


def js_string(s: str) -> str:
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def main() -> int:
    rigs: list[tuple[int, str, dict, dict, dict, dict, list, list]] = []
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
        rigs.append((civ, model, meters, cmds, caps, inputs, preamps, attenuators))

    rigs.sort(key=lambda r: r[0])

    lines = [
        "// Auto-generated by tools/extract-rig-caps.py — do NOT hand-edit.",
        "// Source: rigs/*.rig (Icom only). Re-run the script after editing those.",
        "//",
        "// Each entry: civAddr -> { model, caps, meters, cmds, inputs, preamps, attenuators }",
        "//   caps:   { hasTransmit, hasSpectrum, hasLAN, numReceivers, numVFOs }",
        "//   meters: { kind: [[rigVal, actualVal], ...] }",
        "//           kinds: sMeter, swr, power, alc, comp, center, voltage, current",
        "//   cmds:   { modOff, modData1, modData2, modData3 } -> CI-V byte sequence",
        "//   inputs: name -> reg byte. The JS side writes <prefix>+<reg> to",
        "//           switch the rig's modulation source (e.g., USB).",
        "//           Names: MIC, ACCA, ACCB, USB, LAN, MICUSB, ACCUSB, …",
        "//   preamps / attenuators: [{num, name}, ...] — Num is the CI-V byte;",
        "//           the SPA's P.AMP/ATT cycle button walks the list.",
        "",
        "(function (global) {",
        "    'use strict';",
        "    global.IcomRigCaps = {",
    ]
    for civ, model, meters, cmds, caps, inputs, preamps, attenuators in rigs:
        lines.append(
            f"        0x{civ:02X}: {{ model: {js_string(model)}, "
            f"caps: {js_caps(caps)}, "
            f"meters: {js_meters(meters)}, "
            f"cmds: {js_commands(cmds)}, "
            f"inputs: {js_inputs(inputs)}, "
            f"preamps: {js_num_name_list(preamps)}, "
            f"attenuators: {js_num_name_list(attenuators)} }},"
        )
    lines += [
        "    };",
        "})(typeof window !== 'undefined' ? window : globalThis);",
        "",
    ]
    OUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUT.relative_to(REPO)} — {len(rigs)} Icom rigs")
    for civ, model, meters, cmds, caps, inputs, preamps, attenuators in rigs:
        flags = []
        if not caps["hasTransmit"]: flags.append("RX-only")
        if caps["hasSpectrum"]: flags.append("scope")
        if caps["hasLAN"]: flags.append("LAN")
        flag = "  [" + ",".join(flags) + "]" if flags else ""
        usb = inputs.get("USB", -1)
        usb_str = f"0x{usb:02x}" if usb >= 0 else "-"
        pa = f"PA={len(preamps)}" if preamps else "PA=-"
        at = f"ATT={len(attenuators)}" if attenuators else "ATT=-"
        print(f"  0x{civ:02X}  {model:<18}{flag}  USB-reg={usb_str}  {pa} {at}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
