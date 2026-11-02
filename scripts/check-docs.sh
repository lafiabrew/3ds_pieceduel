#!/usr/bin/env bash
set -euo pipefail

REQUIRED_FILES=(
  "README.md"
  "AGENTS.md"
  "IMPLEMENTATION_PLAN.md"
  "Makefile"
  "Containerfile.3ds"
  "meta/app.rsf"
  "meta/generate_assets.py"
  "scripts/verify-3ds-build.sh"
)

for f in "${REQUIRED_FILES[@]}"; do
  if [[ ! -s "$f" ]]; then
    echo "FAIL: Missing or empty required file: $f" >&2
    exit 1
  fi
done

python3 - <<'PY'
import os, re, sys

md_files = ["README.md", "AGENTS.md", "IMPLEMENTATION_PLAN.md"]
if os.path.isdir("docs"):
    for root, _, files in os.walk("docs"):
        for fn in files:
            if fn.endswith(".md"):
                md_files.append(os.path.join(root, fn))

for path in md_files:
    text = open(path, "r", encoding="utf-8").read()
    fences = len(re.findall(r"^```", text, flags=re.M))
    if fences % 2 != 0:
        print(f"FAIL: Unbalanced code fences in {path} ({fences})", file=sys.stderr)
        sys.exit(1)
    for m in re.finditer(r"\[[^\]]+\]\(([^)]+)\)", text):
        target = m.group(1).split("#")[0].strip()
        if not target or target.startswith(("http://", "https://", "mailto:")):
            continue
        resolved = os.path.normpath(os.path.join(os.path.dirname(path), target))
        if not os.path.exists(resolved):
            print(f"FAIL: Dead link in {path}: {target} -> {resolved}", file=sys.stderr)
            sys.exit(1)

print(f"PASS: Checked {len(md_files)} markdown files (fences balanced, local links valid).")
PY
