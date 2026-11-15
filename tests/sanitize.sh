#!/usr/bin/env bash
# Corre o front-end do Emux sobre todas as fontes de teste com ASan/UBSan.
# O objetivo nao e validar o codigo gerado (isso e o smoke.sh), mas apanhar
# erros de memoria e comportamento indefinido no compilador.
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
emux="${1:-$root/build/sanitize/emux}"
out="$root/tests/out-sanitize"

if [ ! -x "$emux" ]; then
    echo "FAIL sanitizers: executavel ausente em $emux"
    exit 1
fi

mkdir -p "$out"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0:abort_on_error=1}"
export UBSAN_OPTIONS="${UBSAN_OPTIONS:-halt_on_error=1:print_stacktrace=1}"

fail=0
count=0

for src in "$root"/tests/cases/*.ex; do
    [ -f "$src" ] || continue
    name="$(basename "$src" .ex)"
    log="$out/$name.log"
    count=$((count + 1))

    # Alguns casos falham de proposito; o que interessa aqui e que o
    # front-end nao produza relatorios dos sanitizers.
    "$emux" "$src" -o "$out/$name.exe" > "$log" 2>&1 || true

    if grep -qE 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:|SUMMARY: ' "$log"; then
        echo "FAIL sanitizers: $name"
        grep -m 3 -E 'AddressSanitizer|UndefinedBehaviorSanitizer|runtime error:|SUMMARY: ' "$log"
        fail=1
    fi
done

if [ "$fail" -eq 0 ]; then
    echo "ok   sanitizers ($count fontes, sem relatorios ASan/UBSan)"
fi

exit "$fail"
