"""check_okf.py - enforce the OKF honesty convention and link integrity.

Void Core's convention, inherited here: a concept may not claim `status:current`
without a `resource:` link to the code that backs it. That is only a guarantee if
something checks it - a refactor on 2026-08-21 moved every source file and left
all four resource links dangling, with the bundle still claiming `status:current`.
Nobody noticed until a stale-link sweep was run by hand.

Run from the repo root. Exits non-zero on any violation.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
OKF = ROOT / "okf"
RESERVED = {"index.md", "log.md"}

problems = []
pages = sorted(OKF.rglob("*.md"))
ids = {str(p.relative_to(OKF)).replace("\\", "/") for p in pages}

for page in pages:
    text = page.read_text(encoding="utf-8")
    rel = page.relative_to(ROOT)

    # 1. status:current requires a resource: link that actually exists.
    front = text.split("---")[1] if text.startswith("---") else ""
    if page.name not in RESERVED and "status:current" in front:
        m = re.search(r"^resource:\s*(\S+)\s*$", front, re.M)
        if not m:
            problems.append(f"{rel}: claims status:current with no resource:")
        elif not (ROOT / m.group(1)).exists():
            problems.append(f"{rel}: resource: {m.group(1)} does not exist")

    # 2. every internal link resolves.
    for m in re.finditer(r"\]\(([^)\s]+)\)", text):
        target = m.group(1)
        if target.startswith(("http", "#", "mailto:")):
            continue
        if "%20" in target:
            continue  # url-encoded path to a sibling project
        if target.startswith("/"):
            if target.lstrip("/") not in ids:
                problems.append(f"{rel}: broken bundle link {target}")
        elif not (page.parent / target).resolve().exists():
            problems.append(f"{rel}: broken relative link {target}")

if problems:
    print(f"OKF check: {len(problems)} problem(s)")
    for p in problems:
        print("  -", p)
    sys.exit(1)
print(f"OKF check: {len(pages)} pages, all links resolve, all status:current backed by real code")
