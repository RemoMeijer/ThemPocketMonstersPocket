#!/usr/bin/env bash
# check_states over every deck in decks/, one process per deck (its games against all decks), on all cores.
# Usage: ./check_all.sh [path/to/check_states]   (default engine/build/check_states; use engine/build-debug/… for sanitizers)
bin=${1:-engine/build/check_states}
decks=(decks/*.txt)
out=$(mktemp -d)
seq 0 $((${#decks[@]} - 1)) | xargs -P "$(nproc)" -I{} sh -c "\"$bin\" --row {} ${decks[*]} > $out/{}.out 2> $out/{}.err"
cat "$out"/*.err | head -40
awk '/^moves checked:/ {m += $3} /^rule violations:/ {v += $3; n++}
     END {print n " of '"${#decks[@]}"' decks done, " m " moves checked, rule violations: " v; exit (v > 0 || n != '"${#decks[@]}"')}' "$out"/*.out
