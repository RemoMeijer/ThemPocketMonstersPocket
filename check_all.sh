#!/usr/bin/env bash
# check_states over every deck in decks/, one process per deck (its games against all decks), on all cores.
# Usage: ./check_all.sh [path/to/check_states]   (default engine/build/check_states; use engine/build-debug/… for sanitizers)
# GAMES=N sets the games per pairing (default 2000); the sanitizer build is ~8x slower, so GAMES=200 suits it.
bin=${1:-engine/build/check_states}
decks=(decks/*.txt)
out=$(mktemp -d)
seq 0 $((${#decks[@]} - 1)) | xargs -P "$(nproc)" -I{} sh -c "\"$bin\" --games ${GAMES:-2000} --row {} ${decks[*]} > $out/{}.out 2> $out/{}.err"
cat "$out"/*.err | head -40
awk '/^moves checked:/ {m += $3} /^rule violations:/ {v += $3; n++}
     END {print n " of '"${#decks[@]}"' decks done, " m " moves checked, rule violations: " v; exit (v > 0 || n != '"${#decks[@]}"')}' "$out"/*.out
