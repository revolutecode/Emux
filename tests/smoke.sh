#!/usr/bin/env bash
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
emux="${1:-$root/build/debug/emux.exe}"
out="$root/tests/out"

ucrt="/c/Users/Ngola/scoop/apps/msys2/current/ucrt64/bin"
if [ -d "$ucrt" ]; then
    export PATH="$ucrt:$PATH"
fi

mkdir -p "$out"

fail=0

run_case() {
    local name="$1" expected="$2"
    local src="$root/tests/cases/$name.ex"
    local exe="$out/$name.exe"

    if ! "$emux" "$src" -o "$exe" > "$out/$name.log" 2>&1; then
        echo "FAIL $name: compilacao falhou (veja tests/out/$name.log)"
        fail=1
        return
    fi

    "$exe"
    local got=$?

    if [ "$got" -ne "$expected" ]; then
        echo "FAIL $name: esperado $expected, obtido $got"
        fail=1
    else
        echo "ok   $name ($got)"
    fi
}

expect_compile_error() {
    local name="$1"
    local src="$root/tests/cases/$name.ex"

    if "$emux" "$src" -o "$out/$name.exe" > "$out/$name.log" 2>&1; then
        echo "FAIL $name: compilacao deveria falhar"
        fail=1
    else
        echo "ok   $name (erro de compilacao esperado)"
    fi
}

run_case fold 0
run_case hex 8
run_case lnot 1
run_case underscore_start 7
run_case signed_widen 251
run_case truncate 44
run_case copy_memory 9
run_case call_with_arg 42
run_case wide_immediate 37
expect_compile_error undefined_function

exit $fail
