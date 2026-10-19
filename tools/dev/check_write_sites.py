"""H3 lint (docs/redesign/plan.md section 0.3): no file under Source/UI may call an
Ep40Writer write method (upload, uploadToPads, assignPad, deleteSound) except
through the write gate, which always opens a confirm first.

Run by CTest:  python -I tools/dev/check_write_sites.py <repo root>
Exits 1 and names every offending line when a write site is outside the allow-list.
"""

import pathlib
import re
import sys

WRITE_METHODS = ("upload", "uploadToPads", "assignPad", "deleteSound")

# A direct call on the writer: deviceWriter().upload (...), or any Ep40Writer
# reference taken in UI code (which could then be used to write unseen).
DIRECT_CALL = re.compile(r"deviceWriter\s*\(\s*\)\s*\.\s*(" + "|".join(WRITE_METHODS) + r")\b")
ALIAS = re.compile(r"\bEp40Writer\s*&")

# File (relative to Source/UI) -> how many direct write calls it may hold.
# R3: only the write gate talks to Ep40Writer. Pages call WriteGate::request
# and write through the GatedWriter handed to the confirmed action; the
# gate's device adapter is the one place holding an Ep40Writer reference.
ALLOWED = {
    "Design/WriteGate.h": 0,         # declares makeDeviceWrites (Ep40Writer&)
    "Design/WriteGate.cpp": 0,
    "Design/WriteGateDevice.cpp": 0, # the adapter: Ep40Writer& writer
}
# Any other use of the app's writer (e.g. "auto& w = model.deviceWriter()")
# could write unseen: only isBusy() may be read outside the gate.
WRITER_USE = re.compile(r"deviceWriter\s*\(\s*\)(?!\s*\.\s*isBusy\b)")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), text, flags=re.S)
    return re.sub(r"//[^\n]*", "", text)


def main() -> int:
    root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else ".").resolve()
    ui = root / "Source" / "UI"
    problems = []
    counts = {}
    for path in sorted(ui.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        rel = path.relative_to(ui).as_posix()
        code = strip_comments(path.read_text(encoding="utf-8", errors="replace"))
        for lineno, line in enumerate(code.splitlines(), 1):
            for m in DIRECT_CALL.finditer(line):
                counts[rel] = counts.get(rel, 0) + 1
                if rel not in ALLOWED:
                    problems.append(f"{rel}:{lineno}: direct device write '{m.group(1)}' outside the write gate")
            if ALIAS.search(line) and rel not in ALLOWED:
                problems.append(f"{rel}:{lineno}: holds an Ep40Writer reference outside the write gate")
            if WRITER_USE.search(line) and rel not in ALLOWED:
                problems.append(f"{rel}:{lineno}: uses the device writer outside the write gate (only isBusy() may be read)")
    for rel, n in counts.items():
        if rel in ALLOWED and n > ALLOWED[rel]:
            problems.append(f"{rel}: {n} direct write calls, allowed {ALLOWED[rel]}")
    if problems:
        print("check_write_sites: FAILED")
        for p in problems:
            print("  " + p)
        return 1
    print("check_write_sites: ok (every device write in Source/UI goes through the write gate)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
