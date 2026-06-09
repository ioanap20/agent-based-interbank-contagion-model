#!/usr/bin/env bash
set -euo pipefail

results_dir="${1:-results}"

if [[ ! -d "$results_dir" ]]; then
  echo "Results directory not found: $results_dir" >&2
  exit 1
fi

found=0

for file in "$results_dir"/seeded_results_*.json; do
  [[ -e "$file" ]] || continue

  matches="$(
    python3 - "$file" <<'PY'
import json
import sys

path = sys.argv[1]
try:
    with open(path, "r", encoding="utf-8") as handle:
        payload = json.load(handle)
except json.JSONDecodeError as exc:
    sys.exit(0)

if not isinstance(payload, dict) or not isinstance(payload.get("results"), list):
    sys.exit(0)

for row in payload.get("results", []):
    if not isinstance(row, dict):
        continue
    sequential_time = row.get("sequential_time_ms")
    if sequential_time is not None:
        print(
            f"seed={row.get('seed')} "
            f"shock={row.get('shock_percentage')} "
            f"sequential_time_ms={sequential_time}"
        )
PY
  )" || status="$?"
  status="${status:-0}"

  if [[ -n "$matches" ]]; then
    found=1
    echo "$file"
    echo "$matches" | sed 's/^/  /'
  fi
  status=0
done

if [[ "$found" -eq 0 ]]; then
  echo "No seeded result entries with sequential_time_ms found in $results_dir"
fi
