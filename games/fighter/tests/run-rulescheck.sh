#!/usr/bin/env bash
# Builds and runs the rubric checks in rulescheck.c (plain C, the simulation only: no window, no GPU), then the controls:
# each control breaks the move table or a copy of the rules on purpose and the check named must fail.
# CARTWHEEL_ENGINE points at an engine checkout (default: deps/cartwheel-engine).
# run-rulescheck.sh            everything, about an hour on 4 free cores (the FI9 search takes most of it)
# run-rulescheck.sh controls   the controls only
# run-rulescheck.sh checks     the checks only
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="${CARTWHEEL_ENGINE:-$HERE/../../deps/cartwheel-engine}"
OUT="${TMPDIR_FIGHTER:-$HERE/../../out}/rulescheck"
export RC_HERE="$HERE"
MODE="${1:-all}"
mkdir -p "$OUT"
MOVES="$HERE/assets/data/moves.txt"
FLAGS=(-std=gnu17 -O2 -g -Wall -Wextra -Wno-unused-parameter -I"$ENGINE/include")

build() { # build <dir with sim.c sim.h ai.c tests/rulescheck.c> <exe>
  cc "${FLAGS[@]}" -I"$1" "$1/tests/rulescheck.c" "$1/sim.c" "$1/ai.c" "$ENGINE/src/input/motion.c" -o "$2"
}

status=0
if [ "$MODE" != controls ]; then
  build "$HERE" "$OUT/rulescheck" || exit 2
  # one attacker per process: the search is the slow part
  pids=()
  for a in bolt grit vex mara; do
    "$OUT/rulescheck" "$MOVES" --only FI9 --attacker "$a" > "$OUT/fi9-$a.log" 2>&1 &
    pids+=($!)
  done
  for p in "${pids[@]}"; do wait "$p" || status=1; done
  cat "$OUT"/fi9-*.log
  for c in FI8 FI10 AI FI11 FI13 FI16; do "$OUT/rulescheck" "$MOVES" --only "$c" || status=1; done
  # the rendered gates' pinned sim.hash, replayed with no window: did the rules move what they play?
  cc "${FLAGS[@]}" -I"$HERE" "$HERE/tests/gatereplay.c" "$HERE/sim.c" "$HERE/ai.c" "$ENGINE/src/input/motion.c" -o "$OUT/gatereplay" || exit 2
  node "$HERE/tests/gatereplay.mjs" "$OUT/gatereplay" "$MOVES" "$OUT" || status=1
fi

# control <name> <check> <what the log must show> <file or -> <perl substitution or -> [rulescheck arguments...]
control() {
  local name="$1" check="$2" want="$3" file="$4" sub="$5"; shift 5
  local dir="$OUT/ctl-$name"
  rm -rf "$dir"; mkdir -p "$dir/tests" "$dir/assets"
  cp "$HERE/sim.c" "$HERE/sim.h" "$HERE/ai.c" "$dir/"
  cp "$HERE/tests/rulescheck.c" "$dir/tests/"
  if [ "$file" != - ]; then
    perl -0pi -e "$sub" "$dir/$file"
    if cmp -s "$dir/$file" "$HERE/$file"; then echo "control $name: the break did not apply"; status=1; return; fi
  fi
  build "$dir" "$dir/rulescheck" || { echo "control $name: did not build"; status=1; return; }
  "$dir/rulescheck" "$MOVES" --only "$check" "$@" > "$dir/log" 2>&1
  local rc=$?
  if [ "$rc" -eq 1 ] && grep -q "^$check: FAIL" "$dir/log" && grep -Eq "$want" "$dir/log"; then
    echo "control $name: $check failed as it must"
    grep -E -m 3 "$want" "$dir/log" | cut -c1-200 | sed 's/^/    /'
  else
    echo "control $name: $check did NOT fail (exit $rc)"; tail -3 "$dir/log"; status=1
  fi
}

if [ "$MODE" != checks ]; then
  control fi8-plus-jab FI8 DEGENERATE - - --patch 'move bolt 5P bs=20'
  control fi9-no-juggle-cap FI9 'JUGGLE LOOP' sim.h 's/#define FT_JUGGLE_MAX 3/#define FT_JUGGLE_MAX 250/' --attacker vex --budget 2000
  control fi9-no-stun-decay FI9 LOOP sim.h 's/#define FT_STUN_DECAY_FROM 6/#define FT_STUN_DECAY_FROM 30000/' --attacker vex
  control fi9-no-spent-event FI9 'said so: 0' sim.c 's/emit\(g, EV_JUGGLE_SPENT, d, 0, \(hb\.x0 \+ hb\.x1\) \/ 2, \(hb\.y0 \+ hb\.y1\) \/ 2\);//' --attacker vex --budget 2000
  control fi9-shot-hits-every-frame FI9 'hits from one use' sim.c 's/if \(P->hit >= DEF\(g, a\)->mv\[P->mv\]\.hits\) continue;//' --attacker vex --budget 2000
  control fi10-getup-can-be-hit FI10 'no answer:' sim.c 's/D->state == S_GETUP \|\| //; s/f->invuln = 24;/f->invuln = 0;/'
  control fi11-jab-kills FI11 'classes nothing beats: [1-9]' - - --patch 'move bolt 5P su=1 dmg=900' --patch 'move grit 5P su=1 dmg=900' --patch 'move vex 5P su=1 dmg=900' --patch 'move mara 5P su=1 dmg=900'
  # the computer as it was before it read anything, and the new one with its reading taken out
  control ai-old-computer AI 'classes hard loses to: [1-9]' ai.c 'BEGIN { $_old = `git -C "$ENV{RC_HERE}" show b7015c4:games/fighter/ai.c`; die "no old ai.c" unless $_old } $_ = $_old'
  control ai-no-reading AI 'classes hard loses to: [1-9]' ai.c 's/return a->score\[best\] >= LV\[lvl\]\.read \? best : -1;/return -1;/'
  control fi13-first-hit-decides FI13 'reversed in the second' sim.c 's/D->hp -= dd;/D->hp -= dd * 12;/'
  control fi16-no-turning FI16 'facing away' sim.c 's/if \(dx > 0\) f->facing = 1; else if \(dx < 0\) f->facing = -1;\n    \}\n  \}\n  \/\* frame advantage/\n    }\n  }\n  \/* frame advantage/'
  control fi16-buffered-stale FI16 'came out facing away' sim.c 's/        face_other\(g, i\);\n//'
  # the replay must see a change in the rules: one number in the table moves every pin
  cc "${FLAGS[@]}" -I"$HERE" "$HERE/tests/gatereplay.c" "$HERE/sim.c" "$HERE/ai.c" "$ENGINE/src/input/motion.c" -o "$OUT/gatereplay" || exit 2
  sed 's/fighter vex name=Vex hp=[0-9]*/fighter vex name=Vex hp=1500/' "$MOVES" > "$OUT/moves-ctl.txt"
  if node "$HERE/tests/gatereplay.mjs" "$OUT/gatereplay" "$OUT/moves-ctl.txt" "$OUT" --expect-mismatch | tail -1 | grep -q ' moved' && ! cmp -s "$MOVES" "$OUT/moves-ctl.txt"; then echo "control gatereplay: moved the pins as it must"; else echo "control gatereplay: did NOT move"; status=1; fi
fi
if [ "$status" -eq 0 ]; then echo "run-rulescheck: every gate held, every control failed (FI11: the rubric bar is printed, not gated)"; else echo "run-rulescheck: PROBLEMS, see above"; fi
exit "$status"
