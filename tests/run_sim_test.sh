#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only WITH GPL-3.0-linking-exception
# Host integration test: compiles the browser-simulator sources natively (the same file set that
# tools/Build-Web.ps1 sends to WASM) around a note-spy, then runs tests/key_assist_sim_test.c.
#   tests/run_sim_test.sh              modified firmware: all assertions
#   tests/run_sim_test.sh stock        stock app/screen.c from $STOCK_REF (default HEAD): trace only
#   tests/run_sim_test.sh regress      the same trace from the working tree; diff it against `stock`
#   tests/run_sim_test.sh demo [F]     replay proof/demo.script (or F) against the firmware and check it
#   tests/run_sim_test.sh lcdhash      JSON: reference LCD hashes for the emulator check
#   tests/run_sim_test.sh shot F.ppm   render the KEY ASSIST page and F.ppm.home.ppm
set -euo pipefail
cd "$(dirname "$0")/.."
mode="${1:-ours}"
STOCK_REF="${STOCK_REF:-HEAD}"
CC="${CC:-cc}"   # CI uses clang, like the project's own toolchain
out="$(mktemp -d)"
INC="-Iweb/engine -Iweb/synth -Ithird_party/synth -Isdk/include -Iapp/synth -Iapp"
FLAGS="-std=c11 -O1 -fwrapv -fno-fast-math -ffp-contract=off -DFM1_HOST_TEST -DFM1_OPEN_WEB -Wall -Wextra -Werror -Wno-unused-function -Wno-array-parameter $INC"
objs=()
build() { # source object [extra flags...]
    local src="$1" obj="$2"; shift 2
    "$CC" $FLAGS "$@" -c "$src" -o "$obj"; objs+=("$obj")
}
i=0
while IFS= read -r f; do
    i=$((i + 1))
    if [ "$f" = app/screen.c ]; then
        src="$f"
        if [ "$mode" = stock ]; then
            git show "$STOCK_REF:app/screen.c" > "$out/screen_stock.c"; src="$out/screen_stock.c"
        fi
        build "$src" "$out/$i.o" -Dgroove_midi_local=ka_spy_midi_local
    elif [ "$mode" = stock ] && [ "$f" = app/key_assist.c ]; then
        continue
    else
        build "$f" "$out/$i.o"
    fi
done < <(find app web third_party -name '*.c' -not -path 'app/board/*' | sort)
"$CC" -std=c11 -O1 -Wall -Wextra -Werror -Itests -c tests/key_assist_sim_test.c -o "$out/test.o"
# midi_router.c declares two hooks weak; unresolved they are null, as in the WASM build. ELF linkers
# accept that as is; the macOS linker must be told which symbols may stay undefined.
weak=()
if [ "$(uname)" = Darwin ]; then weak=(-Wl,-U,_groove_midi_panel_reset -Wl,-U,_groove_midi_panel_track_reset); fi
"$CC" "${objs[@]}" "$out/test.o" -lm ${weak[@]+"${weak[@]}"} -o "$out/sim_test"
if [ "$mode" = stock ]; then "$out/sim_test" regress; elif [ "$mode" = regress ]; then "$out/sim_test" regress
elif [ "$mode" = shot ]; then "$out/sim_test" shot "${2:-/tmp/ka_page.ppm}"
elif [ "$mode" = lcdhash ]; then "$out/sim_test" lcdhash
elif [ "$mode" = demo ]; then "$out/sim_test" demo "${2:-proof/demo.script}"
else "$out/sim_test"; fi
