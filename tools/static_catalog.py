#!/usr/bin/env python3
"""Build a static UI-object catalog from an Il2CppDumper dump.

M1 goal: find UI strings and their likely object keys from code, without
enumerating every screen by hand.  The output is a candidate catalog, not an
authoritative translation table; runtime object dumps reconcile it later.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

try:
    import pefile
    import capstone
except ImportError as exc:
    print("需要 pefile 和 capstone: pip install pefile capstone", file=sys.stderr)
    raise

ROOT = Path(__file__).resolve().parents[1]
GAME_UI_EXACT = {"Menu", "Pause", "Mods", "Multiplayer", "Credits"}
GAME_UI_PREFIXES = ("Menu", "Window", "Pause")
GAME_UI_SUFFIXES = ("Window", "Menu", "Pause", "Mods", "Multiplayer", "Credits")
BUTTON_CLASSES = (
    "Button", "TextButton", "SliderButton", "BoolButton", "KeybindButton",
    "TypeButton", "CategoryButton", "BallButton", "BarButton", "PanelButton",
    "WorldButton", "StringButton", "MenuButton", "TabButton", "IconButton",
)
HEADER_KNOWN = {
    "Menu_Settings_Audio": "audio settings",
    "Menu_Settings_Video": "video settings",
    "Menu_Settings_Graphics": "graphics settings",
    "Menu_Settings_Gameplay": "gameplay settings",
    "Menu_Settings": "tabs",
    "Menu_Main": "",
    "Menu": "",
    "Menu_Promo": "promo",
    "Menu_Pause": "",
    "Menu_Mods": "mods",
    "Menu_Multiplayer": "multiplayer",
    "Menu_NewWorld": "your new world...",
    "Menu_Controls": "controls",
    "Menu_Backups": "backups",
    "Menu_Quit": "",
    "Menu_Updates": "updates",
    "Menu_Credits": "credits",
    "Menu_Printer": "printer",
    "Menu_PrinterActions": "printer",
    "Menu_PrinterSettings": "printer",
    "Menu_WorldDetails": "world details",
    "Menu_MissingTypes": "missing types",
    "Menu_Universes": "universes",
    "Menu_WorldsMain": "world buttons",
    "Menu_Worlds": "world buttons",
    "Menu_CancelGen": "",
}
STOPWORDS = {
    "true", "false", "null", "none", "yes", "no", "on", "off", "ok", "cancel",
    "error", "warning", "default", "test", "debug", "version",
}
SKIP_CLASSES = {"ToolsWindow", "TypeWindow", "BlockWindow"}
SKIP_METHODS = {"Menu_Credits"}
PROPER_NOUNS = {"Karhula", "VOIMA", "Elias Tapani Karhu"}


def parse_dump(path: Path):
    text = path.read_text(encoding="utf-8", errors="replace")
    methods = []
    namespace = ""
    current_class = None
    pending_rva = None
    for line in text.splitlines():
        if line.startswith("// Namespace:"):
            namespace = line.split(":", 1)[1].strip()
            continue
        m = re.match(r"^(?:public|private|internal|protected|sealed|abstract|static|partial|\s)*class\s+([A-Za-z0-9_.<>]+)", line)
        if m and "TypeDefIndex" in line:
            current_class = m.group(1)
            continue
        rm = re.match(r"\s*// RVA: 0x([0-9A-Fa-f]+)\s+Offset:", line)
        if rm:
            pending_rva = int(rm.group(1), 16)
            continue
        if pending_rva is not None and "(" in line and "{ }" in line:
            sig = line.strip()
            before = sig.split("(", 1)[0].strip()
            if before:
                name = before.split()[-1]
                methods.append({"class": current_class, "namespace": namespace,
                                "name": name, "rva": pending_rva, "sig": sig[:240]})
            pending_rva = None
    return methods


def is_ui_class(cls: str) -> bool:
    if not cls or "." in cls or "<" in cls:
        return cls in GAME_UI_EXACT
    if cls == "Window":
        return False
    if cls in GAME_UI_EXACT:
        return True
    if cls.startswith(GAME_UI_PREFIXES):
        return True
    return cls.endswith(GAME_UI_SUFFIXES)


def is_ui_method(m):
    if not m["class"]:
        return False
    if m["namespace"] not in ("", "Assembly-CSharp"):
        return False
    if m["name"].startswith(("get_", "set_", "add_", "remove_", "<")):
        return False
    if m["name"] == ".cctor":
        return False
    if m["class"] == "Menu" and not (m["name"].startswith("Menu_") or m["name"] in {".ctor", "UpdateControlIcons", "UpdateSpace"}):
        return False
    return is_ui_class(m["class"])


def ui_like(s: str) -> bool:
    if not s or len(s) < 2 or len(s) > 120:
        return False
    if len(s.strip()) < 3:
        return False
    if not re.search(r"[A-Za-z]", s):
        return False
    low = s.lower().strip()
    if low in STOPWORDS or low in {"x", "y", "z", "w", "a", "s", "d", "mouse x", "mouse y"}:
        return False
    if s.startswith("\n") and len(s.strip()) < 8:
        return False
    if re.fullmatch(r"[A-Z0-9]{1,5}", s.strip()):
        return False
    if re.fullmatch(r"[A-Z0-9 \n]{1,16}", s.strip()):
        return False
    if any(x in s for x in ("://", "\\", "/", ".png", ".json", ".txt", "%s", "%d", "{", "}", "<", ">", "=", "_")):
        return False
    if s.startswith(("@", "System.", "UnityEngine.", "Mono.", "Steamworks.", "Assembly-")):
        return False
    if re.fullmatch(r"[A-Z0-9 .:+-]+", s) and len(s) > 8:
        return False
    if s.count("\n") > 3:
        return False
    return True


def normalize_name(s: str) -> str:
    s = s.split("\n", 1)[0].strip()
    s = re.sub(r"\s+", " ", s)
    s = re.sub(r"\d+", "#", s)
    return s


def key_header(key: str):
    if key.startswith("W:Window:"):
        return key[len("W:Window:"):].split("|", 1)[0]
    return None


def key_class_field(key: str):
    tail = key.split("|n:", 1)[-1]
    if ":" in tail:
        name, rest = tail.split(":", 1)
        cls, _, field = rest.partition("|")
        if cls and field:
            return name, cls, field
    return None


def unescape(s: str) -> str:
    return s.replace("\\n", "\n").replace("\\t", "\t").replace("\\\\", "\\")


def load_tsv(path: Path):
    by_source = defaultdict(list)
    exact = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) < 3:
            continue
        key, source, zh = cols[0], unescape(cols[1]), cols[2]
        by_source[source].append((key, zh))
        exact[(key, source)] = zh
    return by_source, exact


def pattern_regex(src: str):
    out = []
    pos = 0
    for m in re.finditer(r"\{[0-9*]+\}", src):
        out.append(re.escape(src[pos:m.start()]))
        out.append(r"\d+" if m.group(0) != "{*}" else r".*")
        pos = m.end()
    out.append(re.escape(src[pos:]))
    return re.compile("^" + "".join(out) + "$", re.S)


def find_entry(source: str, context: str, header: str, by_source, flat_entries, flat_patterns):
    exact = by_source.get(source, [])
    candidates = [(k, source, False) for k, _ in exact]
    if not candidates:
        candidates = [(k, s, False) for k, s in flat_entries if s.strip() == source.strip()]
    if not candidates:
        norm_source = " ".join(source.split())
        candidates = [(k, s, False) for k, s in flat_entries if " ".join(s.split()) == norm_source]
    if not candidates:
        candidates = [(k, s, True) for k, s, rx in flat_patterns if rx.fullmatch(source)]
    def ok(key):
        if context == "KEYBIND":
            return key.startswith("KEYBIND:")
        return key.startswith("W:Window:") and key_header(key) == header
    def field_rank(key):
        if key.endswith("|text_name"):
            return 0
        if key.endswith("|text_value"):
            return 1
        if key.endswith("|name"):
            return 2
        return 3
    candidates.sort(key=lambda c: (not c[0].endswith(source.strip()), field_rank(c[0]), c[2]))
    for key, src, is_pattern in candidates:
        if ok(key):
            return key, src, is_pattern
    return None


def load_literals(path: Path):
    return {int(e["address"], 16): e["value"] for e in json.loads(path.read_text(encoding="utf-8"))}


def build_pe(path: Path):
    pe = pefile.PE(str(path), fast_load=True)
    sections = []
    for s in pe.sections:
        sections.append((s.VirtualAddress, s.VirtualAddress + s.Misc_VirtualSize,
                         s.PointerToRawData, s.Misc_VirtualSize))
    raw = path.read_bytes()
    return pe, raw, sections


def read_code(raw, sections, rva, size=30000):
    for va, vend, off, vsize in sections:
        if va <= rva < vend:
            start = off + (rva - va)
            return raw[start:start + min(size, vend - rva)]
    return b""


def disasm_method(md, raw, sections, rva, end_rva, literals, method_by_rva):
    size = min(30000, max(0, end_rva - rva))
    if size <= 0:
        size = min(30000, 4096)
    code = read_code(raw, sections, rva, size)
    if not code:
        return [], []
    strings = []
    ctor_pairs = []
    recent = []
    for ins in md.disasm(code, rva):
        if ins.address >= end_rva:
            break
        if ins.mnemonic in ("lea", "mov"):
            m = re.search(r"(rcx|rdx|r8|r9|rax|rbx),\s*(?:qword ptr\s*)?\[rip \+ (0x[0-9a-fA-F]+)\]", ins.op_str)
            if m:
                disp = int(m.group(2), 16)
                target = ins.address + ins.size + disp
                if target in literals:
                    value = literals[target]
                    recent.append((m.group(1), value))
                    recent = recent[-12:]
                    strings.append(value)
        elif ins.mnemonic == "call" and ins.operands:
            target = ins.operands[0].imm if ins.operands[0].type == capstone.x86.X86_OP_IMM else None
            if target is not None:
                callee = method_by_rva.get(target)
                if callee and callee["name"] == ".ctor" and callee["class"] and any(x in callee["class"] for x in BUTTON_CLASSES):
                    # Nearest string loaded into rcx/rdx is usually the button name.
                    chosen = None
                    for reg, value in reversed(recent):
                        if reg in ("rcx", "rdx") and ui_like(value):
                            chosen = value
                            break
                    if chosen:
                        ctor_pairs.append((callee["class"], chosen))
        if ins.mnemonic == "ret" and ins.address - rva > 16000:
            break
    # de-dup
    seen = set()
    out = []
    for s in strings:
        if ui_like(s) and s not in seen:
            seen.add(s)
            out.append(s)
    seen = set()
    pairs = []
    for c, s in ctor_pairs:
        if (c, s) not in seen:
            seen.add((c, s))
            pairs.append((c, s))
    return out, pairs


WINDOW_HEADER_ALIASES = {
    "ColorWindow": "set color",
    "ModdingWindow": "mods",
    "CrafterWindow": "crafter",
    "InvWindow": "inventory",
    "BlockWindow": "block",
    "SandboxWindow": "sandbox",
    "Menu": "",
    "MenuUniverse": "",
}


def choose_header(method, strings, headers):
    name = method["name"]
    if name in HEADER_KNOWN:
        return HEADER_KNOWN[name], "known-method"
    cls = (method["class"] or "").split(".")[-1]
    if cls in WINDOW_HEADER_ALIASES:
        return WINDOW_HEADER_ALIASES[cls], "class-alias"
    low = name.lower()
    for h in headers:
        if not h:
            continue
        toks = [t for t in re.split(r"[^a-z0-9]+", h.lower()) if t]
        if toks and all(t in low for t in toks):
            return h, "method-token"
    if cls.endswith("Window"):
        stem = cls[:-6].lower()
        for h in headers:
            if h and stem and (stem == h.lower() or h.lower().startswith(stem + " ") or h.lower().endswith(" " + stem)):
                return h, "class-stem"
        return stem, "class-stem"
    if "settings" in low:
        for s in strings:
            if s.lower().endswith("settings"):
                return s.lower(), "string"
    if "menu" in low:
        return "", "root-menu"
    return "", "default-root"


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dump", required=True, help="dump.cs from Il2CppDumper")
    ap.add_argument("--stringliteral", required=True, help="stringliteral.json")
    ap.add_argument("--gameassembly", required=True, help="GameAssembly.dll")
    ap.add_argument("--tsv", default=str(ROOT / "translations" / "translations.tsv"))
    ap.add_argument("--out", default=str(ROOT / "catalog"))
    ap.add_argument("--max-methods", type=int, default=0)
    args = ap.parse_args()

    methods = parse_dump(Path(args.dump))
    all_methods = len(methods)
    print(f"parsed methods: {all_methods}", file=sys.stderr)
    method_by_rva = {m["rva"]: m for m in methods}
    rvas = sorted({m["rva"] for m in methods if m["rva"] > 0})
    method_ends = {}
    for i, rva in enumerate(rvas):
        method_ends[rva] = rvas[i + 1] if i + 1 < len(rvas) else rva + 30000
    by_source, exact = load_tsv(Path(args.tsv))
    exact_keys = {k for k, _ in exact.keys()}
    literals = load_literals(Path(args.stringliteral))
    pe, raw, sections = build_pe(Path(args.gameassembly))
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    md.detail = True

    headers = {key_header(k) for k, _ in exact.keys() if key_header(k) is not None}
    flat_entries = [(k, s) for s, entries in by_source.items() for k, _ in entries]
    flat_patterns = [(k, s, pattern_regex(s)) for k, s in flat_entries if "{" in s]
    methods = [m for m in methods if is_ui_method(m) and m["rva"] not in (0, 0x344C70)]
    if args.max_methods:
        methods = methods[:args.max_methods]
    print(f"ui methods: {len(methods)}", file=sys.stderr)

    records = []
    seen = set()
    for idx, method in enumerate(methods):
        if method["class"] in SKIP_CLASSES or method["name"] in SKIP_METHODS:
            continue
        strings, pairs = disasm_method(md, raw, sections, method["rva"],
                                        method_ends.get(method["rva"], method["rva"] + 4096),
                                        literals, method_by_rva)
        if not strings:
            continue
        is_keybind = "ControlIcon" in method["name"]
        if is_keybind:
            header, header_reason = "KEYBIND", "control-icons"
        else:
            header, header_reason = choose_header(method, strings, headers)
        ctor_class = {}
        for cls, s in pairs:
            ctor_class[s] = cls
        context = "KEYBIND" if is_keybind else "W"
        for source in strings:
            if not is_keybind and source.lower().strip() == header.lower().strip():
                continue
            if source not in by_source and len(source.strip()) >= 4 and any(
                    "{" in s and source.strip().lower() in s.lower() for _, s in flat_entries):
                continue
            found = find_entry(source, context, header, by_source, flat_entries, flat_patterns)
            if found:
                key, _, is_pattern = found
                match = "PATTERN_MATCH" if is_pattern else "EXISTING"
                parsed = key_class_field(key)
                if parsed:
                    name, cls, field = parsed
                else:
                    name = normalize_name(source)
                    cls = "KeybindButton" if is_keybind else ctor_class.get(source, "Button")
                    field = "text_name"
            else:
                name = normalize_name(source)
                cls = "KeybindButton" if is_keybind else ctor_class.get(source, "Button")
                field = "text_name"
                key = f"KEYBIND:{name}" if is_keybind else f"W:Window:{header}|n:{name}:{cls}|{field}"
                if (key, source) in exact:
                    match = "EXISTING"
                elif key in exact_keys:
                    match = "KEY_SOURCE_MISMATCH"
                elif source in by_source:
                    match = "SOURCE_KNOWN"
                else:
                    match = "NEW"
            if cls == "RequestButton":
                continue
            if source.strip() in PROPER_NOUNS:
                continue
            if found:
                confidence = "HIGH" if not is_pattern else "MEDIUM"
            elif source in ctor_class and header_reason not in ("default-root", "class-stem"):
                confidence = "HIGH"
            elif source in ctor_class or header_reason in ("known-method", "class-alias"):
                confidence = "MEDIUM"
            else:
                confidence = "LOW"
            ident = (key, source)
            if ident in seen:
                continue
            seen.add(ident)
            records.append({
                "owner_class": method["class"], "owner_method": method["name"],
                "window_header": header, "button_name": name, "button_class": cls,
                "field": field, "source_text": source, "static_key": key,
                "confidence": confidence, "match": match,
                "header_reason": header_reason,
            })
        if idx and idx % 200 == 0:
            print(f"scanned {idx}/{len(methods)}", file=sys.stderr)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    cols = ["owner_class", "owner_method", "window_header", "button_name", "button_class",
            "field", "source_text", "static_key", "confidence", "match", "header_reason"]
    with (out / "ui-objects.tsv").open("w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(cols) + "\n")
        for r in records:
            row = []
            for c in cols:
                row.append(str(r[c]).replace("\t", " ").replace("\n", "\\n"))
            f.write("\t".join(row) + "\n")

    gaps = [r for r in records if r["match"] not in ("EXISTING", "PATTERN_MATCH")]
    with (out / "untranslated-static.tsv").open("w", encoding="utf-8", newline="\n") as f:
        f.write("match\tconfidence\tstatic_key\tsource_text\towner_class\towner_method\treason\n")
        for r in gaps:
            reason = "no translation for object key" if r["match"] == "NEW" else (
                "source exists under another object key" if r["match"] == "SOURCE_KNOWN" else "same key, different source")
            src = r["source_text"].replace("\t", " ").replace("\n", "\\n")
            f.write("\t".join([r["match"], r["confidence"], r["static_key"], src,
                              r["owner_class"], r["owner_method"], reason]) + "\n")

    summary = {
        "all_methods": all_methods,
        "ui_methods": len(methods),
        "records": len(records),
        "matches": dict(Counter(r["match"] for r in records)),
        "confidence": dict(Counter(r["confidence"] for r in records)),
        "top_owners": Counter(r["owner_class"] + "." + r["owner_method"] for r in records).most_common(30),
        "headers": dict(Counter(r["window_header"] for r in records).most_common()),
    }
    (out / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=1), encoding="utf-8")
    (out / "README.md").write_text(
        "# Static UI catalog\n\n"
        "Generated by tools/static_catalog.py. Do not edit by hand.\n\n"
        "Inputs: dump.cs, stringliteral.json, GameAssembly.dll, translations/translations.tsv.\n\n"
        "Columns: owner_class, owner_method, window_header, button_name, button_class, field, source_text, static_key, confidence, match, header_reason.\n\n"
        "match meanings: EXISTING = exact key+source in TSV; PATTERN_MATCH = existing {0}/{*} pattern covers it; SOURCE_KNOWN = source exists under another object key; KEY_SOURCE_MISMATCH = key exists with another source; NEW = no entry.\n",
        encoding="utf-8")
    print(json.dumps({k: summary[k] for k in ("all_methods", "ui_methods", "records", "matches", "confidence")}, ensure_ascii=False, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
