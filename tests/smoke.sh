#!/usr/bin/env bash
set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
emux="${1:-$root/build/debug/emux.exe}"
out="$root/tests/out"

if [ -n "${MINGW_PREFIX:-}" ]; then
    # Ambiente MSYS2 (local ou CI): /ucrt64 e /mingw32 lado a lado.
    ucrt="$MINGW_PREFIX/bin"
    mingw32="${MINGW_PREFIX%/ucrt64}/mingw32/bin"
else
    ucrt="/c/Users/Ngola/scoop/apps/msys2/current/ucrt64/bin"
    mingw32="/c/Users/Ngola/scoop/apps/msys2/current/mingw32/bin"
fi

if [ -d "$ucrt" ]; then
    export PATH="$ucrt:$PATH"
fi

have_x86_cc=0
if [ -x "$mingw32/gcc.exe" ]; then
    have_x86_cc=1
fi

mkdir -p "$out"

fail=0

run_case() {
    local name="$1" expected="$2" export=""
    if [ $# -ge 3 ]; then
        export="$3"
    fi
    local src="$root/tests/cases/$name.ex"
    local exe="$out/$name.exe"

    if ! "$emux" "$src" -o "$exe" > "$out/$name.log" 2>&1; then
        echo "FAIL $name: compilacao falhou (veja tests/out/$name.log)"
        fail=1
        return
    fi

    if [ -n "$export" ]; then
        if grep -qE "^global[[:space:]]+$export$" "$out/$name.emux"/asm/x86_64/*.asm 2>/dev/null; then
            :
        else
            echo "FAIL $name: simbolo exportado '$export' nao encontrado no assembly"
            fail=1
        fi
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

# Compila um caso com um nivel de otimizacao explicito e confirma que o codigo
# gerado corre com o mesmo exit code da build sem otimizacao.
run_opt_case() {
    local name="$1" expected="$2" level="$3"
    local src="$root/tests/cases/$name.ex"
    local exe="$out/${name}_opt${level}.exe"

    if ! "$emux" "$src" -o "$exe" -O "$level" --force > "$out/${name}_opt${level}.log" 2>&1; then
        echo "FAIL ${name}_O${level}: compilacao falhou (veja tests/out/${name}_opt${level}.log)"
        fail=1
        return
    fi

    "$exe"
    local got=$?
    if [ "$got" -ne "$expected" ]; then
        echo "FAIL ${name}_O${level}: esperado $expected, obtido $got"
        fail=1
    else
        echo "ok   ${name}_O${level} ($got)"
    fi
}

# Caso dedicado de dead code elimination: um 'static_cast' usado como statement
# gera um load cujo resultado e descartado. Em -O0 o load sobrevive; em -O1/-O2
# deve desaparecer e o programa continuar a devolver 5.
run_opt_dce() {
    local name="dce_dead_load"
    local src="$root/tests/cases/$name.ex"
    local exe="$out/$name.exe"
    local exir="$out/$name.exir"

    "$emux" "$src" -o "$exe" -O 0 --force > "$out/${name}_O0.log" 2>&1 ||
        { echo "FAIL $name: -O0 nao compilou"; fail=1; return; }
    "$exe"; [ $? -eq 5 ] || { echo "FAIL $name: -O0 exit != 5"; fail=1; }
    local loads0
    loads0="$(grep -cE '= load' "$exir")"

    "$emux" "$src" -o "$exe" -O 1 --force > "$out/${name}_O1.log" 2>&1 ||
        { echo "FAIL $name: -O1 nao compilou"; fail=1; return; }
    "$exe"; [ $? -eq 5 ] || { echo "FAIL $name: -O1 exit != 5"; fail=1; }
    local loads1
    loads1="$(grep -cE '= load' "$exir")"

    "$emux" "$src" -o "$exe" -O 2 --force > "$out/${name}_O2.log" 2>&1 ||
        { echo "FAIL $name: -O2 nao compilou"; fail=1; return; }
    "$exe"; [ $? -eq 5 ] || { echo "FAIL $name: -O2 exit != 5"; fail=1; }

    if [ "$loads0" -lt 2 ]; then
        echo "FAIL $name: -O0 nao retinha o load morto (loads=$loads0)"
        fail=1
    elif [ "$loads1" -ge "$loads0" ]; then
        echo "FAIL $name: -O1 nao eliminou o load morto (O0=$loads0, O1=$loads1)"
        fail=1
    else
        echo "ok   $name (load morto eliminado em -O1/-O2, exit 5 preservado)"
    fi
}

# Caso dedicado de poda de imports nao usados: dois [[import]] declarados, mas
# apenas 'Sleep' e chamado. Em -O0 ambos ficam no Exir (preserva tudo); em
# -O1/-O2 o import nunca chamado ('GetTickCount') e podado antes de virar Exir,
# e o programa continua a devolver 42.
run_opt_import() {
    local name="opt_unused_import"
    local src="$root/tests/cases/$name.ex"
    local exe="$out/$name.exe"
    local exir="$out/$name.exir"

    "$emux" "$src" -o "$exe" -O 0 --force > "$out/${name}_O0.log" 2>&1 ||
        { echo "FAIL $name: -O0 nao compilou"; fail=1; return; }
    "$exe"; [ $? -eq 42 ] || { echo "FAIL $name: -O0 exit != 42"; fail=1; }
    local imports0 ticks0
    imports0="$(grep -cE '^import ' "$exir")"
    ticks0="$(grep -cE '^import main_GetTickCount ' "$exir")"

    "$emux" "$src" -o "$exe" -O 1 --force > "$out/${name}_O1.log" 2>&1 ||
        { echo "FAIL $name: -O1 nao compilou"; fail=1; return; }
    "$exe"; [ $? -eq 42 ] || { echo "FAIL $name: -O1 exit != 42"; fail=1; }
    local ticks1 sleep1
    ticks1="$(grep -cE '^import main_GetTickCount ' "$exir")"
    sleep1="$(grep -cE '^import main_Sleep ' "$exir")"

    "$emux" "$src" -o "$exe" -O 2 --force > "$out/${name}_O2.log" 2>&1 ||
        { echo "FAIL $name: -O2 nao compilou"; fail=1; return; }
    "$exe"; [ $? -eq 42 ] || { echo "FAIL $name: -O2 exit != 42"; fail=1; }
    local ticks2
    ticks2="$(grep -cE '^import main_GetTickCount ' "$exir")"

    if [ "$ticks0" -ne 1 ] || [ "$imports0" -lt 2 ]; then
        echo "FAIL $name: -O0 nao retinha o import nao usado (imports=$imports0, ticks=$ticks0)"
        fail=1
    elif [ "$ticks1" -ne 0 ] || [ "$ticks2" -ne 0 ]; then
        echo "FAIL $name: import nao usado nao foi podado em -O1/-O2 (O1=$ticks1, O2=$ticks2)"
        fail=1
    elif [ "$sleep1" -ne 1 ]; then
        echo "FAIL $name: import usado (Sleep) foi podado indevidamente em -O1"
        fail=1
    else
        echo "ok   $name (import nao usado podado em -O1/-O2, Sleep preservado, exit 42)"
    fi
}

# Variante com a chamada dentro do corpo de um 'while': prova que a recolha de
# chamadas para poda de imports enxerga para dentro dos blocos de controlo de
# fluxo. Se o Sleep (chamado no loop) fosse podado em -O1, o 'jump' cairia no
# gerador por simbolo ausente; aqui verificamos que ele permanece.
run_opt_import_in_loop() {
    local name="opt_import_in_loop"
    local src="$root/tests/cases/$name.ex"
    local exe="$out/$name.exe"
    local exir="$out/$name.exir"

    "$emux" "$src" -o "$exe" -O 0 --force > "$out/${name}_O0.log" 2>&1 ||
        { echo "FAIL $name: -O0 nao compilou"; fail=1; return; }
    "$exe"; [ $? -eq 42 ] || { echo "FAIL $name: -O0 exit != 42"; fail=1; }
    local ticks0
    ticks0="$(grep -cE '^import main_GetTickCount ' "$exir")"

    "$emux" "$src" -o "$exe" -O 1 --force > "$out/${name}_O1.log" 2>&1 ||
        { echo "FAIL $name: -O1 nao compilou"; fail=1; return; }
    "$exe"; [ $? -eq 42 ] || { echo "FAIL $name: -O1 exit != 42"; fail=1; }
    local ticks1 sleep1
    ticks1="$(grep -cE '^import main_GetTickCount ' "$exir")"
    sleep1="$(grep -cE '^import main_Sleep ' "$exir")"

    if [ "$ticks0" -ne 1 ]; then
        echo "FAIL $name: -O0 nao retinha o import nao usado (ticks=$ticks0)"
        fail=1
    elif [ "$ticks1" -ne 0 ]; then
        echo "FAIL $name: import nao usado nao foi podado em -O1 (ticks=$ticks1)"
        fail=1
    elif [ "$sleep1" -ne 1 ]; then
        echo "FAIL $name: import chamado dentro do 'while' foi podado indevidamente em -O1 (sleep=$sleep1)"
        fail=1
    else
        echo "ok   $name (chamada dentro de 'while' vista pela poda: Sleep preservado, GetTickCount podado)"
    fi
}

expect_compile_error() {
    local name="$1" pattern="${2:-}"
    local src="$root/tests/cases/$name.ex"

    if "$emux" "$src" -o "$out/$name.exe" > "$out/$name.log" 2>&1; then
        echo "FAIL $name: compilacao deveria falhar"
        fail=1
        return
    fi

    if [ -n "$pattern" ] && ! grep -qF "$pattern" "$out/$name.log"; then
        echo "FAIL $name: diagnostico nao contem '$pattern'"
        fail=1
        return
    fi

    echo "ok   $name (erro de compilacao esperado)"
}

expect_compile_error_x86() {
    local name="$1" pattern="${2:-}"
    local src="$root/tests/cases/$name.ex"
    local log="$out/${name}_x86.log"

    if "$emux" "$src" -a x86 -o "$out/${name}_x86.exe" > "$log" 2>&1; then
        echo "FAIL ${name}_x86: compilacao deveria falhar"
        fail=1
        return
    fi

    if [ -n "$pattern" ] && ! grep -qF "$pattern" "$log"; then
        echo "FAIL ${name}_x86: diagnostico nao contem '$pattern'"
        fail=1
        return
    fi

    echo "ok   ${name}_x86 (erro de compilacao esperado)"
}

run_x86_asm() {
    local name="$1"; shift
    local src="$root/tests/cases/$name.ex"
    local exe="$out/${name}_x86.exe"
    local cache="${exe%.exe}.emux"
    local log="$out/${name}_x86.log"

    find "$cache" -mindepth 1 -delete 2>/dev/null

    # Sem toolchain 32-bit o link falha; o objetivo aqui e validar o NASM win32.
    "$emux" "$src" -a x86 --force -o "$exe" > "$log" 2>&1

    local asm_file="$(ls "$cache"/asm/x86/*.asm 2>/dev/null | head -1)"
    local obj_file="$(ls "$cache"/link/x86/*.obj 2>/dev/null | head -1)"

    if [ -z "$asm_file" ] || [ -z "$obj_file" ] || grep -q "NASM failed" "$log"; then
        echo "FAIL ${name}_x86: montagem win32 falhou (veja $log)"
        fail=1
        return
    fi

    for pattern in "$@"; do
        if ! grep -qE "$pattern" "$asm_file"; then
            echo "FAIL ${name}_x86: padrao '$pattern' ausente no assembly"
            fail=1
            return
        fi
    done

    echo "ok   ${name}_x86 (nasm win32)"
}

run_case_x86() {
    local name="$1" expected="$2"; shift 2

    if [ "$have_x86_cc" -ne 1 ]; then
        run_x86_asm "$name" "$@"
        return
    fi

    local src="$root/tests/cases/$name.ex"
    local exe="$out/${name}_x86.exe"
    local cache="${exe%.exe}.emux"
    local log="$out/${name}_x86.log"

    find "$cache" -mindepth 1 -delete 2>/dev/null

    if ! PATH="$mingw32:$PATH" "$emux" "$src" -a x86 --force -o "$exe" > "$log" 2>&1; then
        echo "FAIL ${name}_x86: compilacao/link falhou (veja $log)"
        fail=1
        return
    fi

    local asm_file="$(ls "$cache"/asm/x86/*.asm 2>/dev/null | head -1)"
    for pattern in "$@"; do
        if [ -z "$asm_file" ] || ! grep -qE "$pattern" "$asm_file"; then
            echo "FAIL ${name}_x86: padrao '$pattern' ausente no assembly"
            fail=1
            return
        fi
    done

    PATH="$mingw32:$PATH" "$exe"
    local got=$?

    if [ "$got" -ne "$expected" ]; then
        echo "FAIL ${name}_x86: esperado $expected, obtido $got"
        fail=1
    else
        echo "ok   ${name}_x86 ($got)"
    fi
}

run_project() {
    local name="$1" expected="$2"
    local cfg="$root/tests/multifile/$name/project.json"
    local exe="$out/multifile_$name.exe"

    if ! "$emux" --config "$cfg" > "$out/multifile_$name.log" 2>&1; then
        echo "FAIL multifile_$name: compilacao falhou (veja tests/out/multifile_$name.log)"
        fail=1
        return
    fi

    "$exe"
    local got=$?

    if [ "$got" -ne "$expected" ]; then
        echo "FAIL multifile_$name: esperado $expected, obtido $got"
        fail=1
    else
        echo "ok   multifile_$name ($got)"
    fi
}

run_subsystem() {
    local dir="$out/subsystem"
    local cfg="$dir/project.json"
    local log="$out/subsystem.log"
    local ok=1

    find "$dir" -mindepth 1 -delete 2>/dev/null
    mkdir -p "$dir"

    {
        printf '{\n'
        printf '    "project": {\n'
        printf '        "name": "subsystem",\n'
        printf '        "type": "executable",\n'
        printf '        "include": ["."],\n'
        printf '        "sources": ["main.ex"]\n'
        printf '    },\n'
        printf '    "build": {\n'
        printf '        "arch": "64",\n'
        printf '        "subsystem": "windows",\n'
        printf '        "output": "app"\n'
        printf '    }\n'
        printf '}\n'
    } > "$cfg"

    printf '[Main]\n\nfunc Start() i32 {\n    return 0\n}\n' > "$dir/main.ex"

    if ! "$emux" --config "$cfg" > "$log" 2>&1; then
        echo "FAIL subsystem: compilacao com config falhou"
        fail=1
        return
    fi
    check_log "config windows" "Subsystem: windows." "$log" || ok=0

    if ! "$emux" --config "$cfg" --subsystem console > "$log" 2>&1; then
        echo "FAIL subsystem: override --subsystem console falhou"
        fail=1
        return
    fi
    check_log "override console" "Subsystem: console." "$log" || ok=0

    if "$emux" --config "$cfg" --subsystem bogus > "$log" 2>&1; then
        echo "FAIL subsystem: subsystem invalido deveria falhar"
        fail=1
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        echo "ok   subsystem (config, override e valor invalido)"
    fi
}

run_semicolons() {
    local dir="$out/semicolons"
    local log="$out/semicolons.log"
    local lax="$root/tests/cases/fold.ex"
    local strict="$root/tests/cases/semicolons.ex"
    local ok=1

    find "$dir" -mindepth 1 -delete 2>/dev/null
    mkdir -p "$dir"

    # Padrao: ';' opcional; o caso sem ';' compila e corre.
    if ! "$emux" "$lax" -o "$dir/lax.exe" > "$log" 2>&1; then
        echo "FAIL semicolons: modo padrao rejeitou codigo sem ';'"
        fail=1
        ok=0
    else
        "$dir/lax.exe"
        [ $? -eq 0 ] || { echo "FAIL semicolons: codigo sem ';' nao correu"; fail=1; ok=0; }
    fi

    # --semicolons required: sem ';' falha com diagnostico.
    if "$emux" "$lax" -o "$dir/lax.exe" --semicolons required > "$log" 2>&1; then
        echo "FAIL semicolons: --semicolons required aceitou codigo sem ';'"
        fail=1
        ok=0
    else
        check_log "required" "Expected ';' after statement." "$log" || ok=0
    fi

    # --semicolons required: com ';' compila e corre.
    if ! "$emux" "$strict" -o "$dir/strict.exe" --semicolons required > "$log" 2>&1; then
        echo "FAIL semicolons: codigo com ';' falhou em modo required"
        fail=1
        ok=0
    else
        "$dir/strict.exe"
        [ $? -eq 42 ] || { echo "FAIL semicolons: esperado 42 em modo required"; fail=1; ok=0; }
    fi

    # Valor invalido.
    if "$emux" "$strict" -o "$dir/strict.exe" --semicolons bogus > "$log" 2>&1; then
        echo "FAIL semicolons: modo invalido deveria falhar"
        fail=1
        ok=0
    fi

    # project.json: build.semicolons = required.
    {
        printf '{\n'
        printf '    "project": {\n'
        printf '        "name": "semicolons",\n'
        printf '        "type": "executable",\n'
        printf '        "include": ["."],\n'
        printf '        "sources": ["main.ex"]\n'
        printf '    },\n'
        printf '    "build": {\n'
        printf '        "arch": "64",\n'
        printf '        "semicolons": "required",\n'
        printf '        "output": "app"\n'
        printf '    }\n'
        printf '}\n'
    } > "$dir/project.json"

    printf '[Main]\n\nfunc Start() i32 {\n    return 42\n}\n' > "$dir/main.ex"

    if "$emux" --config "$dir/project.json" > "$log" 2>&1; then
        echo "FAIL semicolons: config required aceitou codigo sem ';'"
        fail=1
        ok=0
    else
        check_log "config required" "Expected ';' after statement." "$log" || ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        echo "ok   semicolons (opcional, required via CLI e config)"
    fi
}

check_log() {
    local label="$1" pattern="$2" log="$3"

    if grep -qF "$pattern" "$log"; then
        return 0
    fi

    echo "FAIL incremental: $label (esperado '$pattern' em $log)"
    fail=1
    return 1
}

run_incremental() {
    local dir="$out/incremental"
    local cfg="$dir/project.json"
    local exe="$dir/app.exe"
    local cache="$dir/app.emux/obj/x86_64"
    local log="$out/incremental.log"
    local ok=1

    find "$dir" -mindepth 1 -delete 2>/dev/null
    mkdir -p "$dir"

    {
        printf '{\n'
        printf '    "project": {\n'
        printf '        "name": "incremental",\n'
        printf '        "type": "executable",\n'
        printf '        "include": ["."],\n'
        printf '        "sources": ["main.ex", "math.ex"]\n'
        printf '    },\n'
        printf '    "build": {\n'
        printf '        "arch": "64",\n'
        printf '        "output": "app"\n'
        printf '    }\n'
        printf '}\n'
    } > "$cfg"

    printf '[Main]\n\nfunc Start() i32 {\n    return Double(21)\n}\n' > "$dir/main.ex"
    printf '[Main]\n\nfunc Double(i32 value) i32 {\n    return value + value\n}\n' > "$dir/math.ex"

    if ! "$emux" --config "$cfg" > "$log" 2>&1; then
        echo "FAIL incremental: compilacao inicial falhou"
        fail=1
        return
    fi

    check_log "compilacao inicial" "Compiled 2 module(s)." "$log" || ok=0

    "$exe"
    [ $? -eq 42 ] || { echo "FAIL incremental: esperado 42"; fail=1; ok=0; }

    local main_emo="$(ls "$cache"/main-*.emo 2>/dev/null | head -1)"
    local math_emo="$(ls "$cache"/math-*.emo 2>/dev/null | head -1)"

    if [ -z "$main_emo" ] || [ -z "$math_emo" ]; then
        echo "FAIL incremental: objetos .emo nao gerados"
        fail=1
        return
    fi

    cp "$main_emo" "$out/incremental_main.emo.bak"
    cp "$math_emo" "$out/incremental_math.emo.bak"

    if ! "$emux" --config "$cfg" > "$log" 2>&1; then
        echo "FAIL incremental: segunda compilacao falhou"
        fail=1
        return
    fi

    check_log "cache total" "Reused 2 module(s) from cache." "$log" || ok=0
    check_log "sem religacao" "Output is up to date." "$log" || ok=0

    cmp -s "$main_emo" "$out/incremental_main.emo.bak" || { echo "FAIL incremental: main mudou sem alteracao"; fail=1; ok=0; }

    printf '[Main]\n\nfunc Double(i32 value) i32 {\n    return value * 2\n}\n' > "$dir/math.ex"

    if ! "$emux" --config "$cfg" > "$log" 2>&1; then
        echo "FAIL incremental: recompilacao parcial falhou"
        fail=1
        return
    fi

    check_log "apenas math recompilado" "Compiled 1 module(s)." "$log" || ok=0
    check_log "main reaproveitado" "Reused 1 module(s) from cache." "$log" || ok=0

    cmp -s "$main_emo" "$out/incremental_main.emo.bak" || { echo "FAIL incremental: main foi recompilado"; fail=1; ok=0; }
    cmp -s "$math_emo" "$out/incremental_math.emo.bak" && { echo "FAIL incremental: math nao foi recompilado"; fail=1; ok=0; }

    "$exe"
    [ $? -eq 42 ] || { echo "FAIL incremental: esperado 42 apos alteracao"; fail=1; ok=0; }

    if ! "$emux" --config "$cfg" --force > "$log" 2>&1; then
        echo "FAIL incremental: --force falhou"
        fail=1
        return
    fi

    check_log "--force" "Compiled 2 module(s)." "$log" || ok=0

    if [ "$ok" -eq 1 ]; then
        echo "ok   incremental (cache, invalidacao e --force)"
    fi
}

run_case fold 0
run_case hex 8
run_case lnot 1
run_case preprocessor_neq 7
run_case shift64_left 0
run_case shift64_right 1
run_case const_fold 6
run_case comparison_int 6
run_case comparison_signed 1
run_case if_else 10
run_case if_elseif 2
run_case while_loop 15
run_case for_loop 20
run_case local_basic 36
run_case local_ptr 7
run_case local_init_expr 42
run_case local_recursion 120
run_case local_frame_attrs 42
run_case local_buffer 66
run_case local_buffer_zero 0
run_case local_buffer_copy 32
run_case local_buffer_arg 215
run_case logical_shortcircuit 12
run_case logical_values 5
run_case logical_loops 26
run_case logical_bitwise 117
run_case break_while 10
run_case break_for 3
run_case continue_for 20
run_case continue_while 16
run_case break_continue_mixed 19
run_case nested_break 36
run_case nested_continue 6
run_case infinite_for_break 5
run_case block_cmp_equal 43
run_case block_cmp_size 54
run_case block_cmp_content 10
run_case block_cmp_slice 30
run_case block_cmp_local 3
run_case bigint_div 10
run_case bigint_shift 64
run_case bigint_negshift 17
run_case bigint_bitwise 255
run_case bigint_cast 35
run_case bigint_u64max 127
run_case bigint_fits 63
run_case bigint_assign_trunc 42
run_case wide_runtime_mul 43
run_case wide_runtime_div 255
run_case wide_runtime_shift 4
run_case wide_runtime_signed 42
run_case wide_runtime_sar 255
run_case wide_runtime_cast 5
run_case wide_const_global 135
run_case wide_runtime_u72 212
run_case wide_runtime_u256 255
run_case wide_runtime_pointer 5
run_case wide_subbyte_u65 0
run_case wide_subbyte_u100 0
run_case wide_subbyte_i129 255
run_case subbyte_u1 42
run_case subbyte_i7 42
run_case subbyte_u24 0
run_case subbyte_i24 255
run_case subbyte_u33 0
run_case subbyte_i40 255
run_case mod_i32 32
run_case mod_signed 28
run_case mod_i64 28
run_case mod_u128 13
run_case mod_i128 7
run_case mod_u65 3
run_case mod_const 2
run_case mod_u3 3
run_case float_ops 42
run_case fixed_ops 42
run_case float_wide 42
run_case fixed_wide 42
run_case float_half 42
run_case float_subbyte 42
run_case fixed_subbyte 42
run_case fixed_wide40 42
run_case float_param 42
run_case fixed_param 42
run_case float_cast 42
run_case float_bits 42
run_case float_inf 42
run_case fixed_divzero 42
run_case prepared_types 0
run_case const_reassign 2
run_case underscore_start 7
run_case signed_widen 251
run_case truncate 44
run_case copy_memory 9
run_case call_with_arg 42
run_case wide_immediate 37
run_case native_import 42
run_case pointer 42
run_case wide_math 42
run_case wide_params 42
run_case abi_local 42
run_case abi_mixed 42
run_case attr_group 42 emux_entry_point
run_case semicolons 42
run_case native_abi 42
run_case native_fastcall 42
run_case std_types 42
run_case using_types 42
run_case using_unrestrict_param 42
run_case enum_basic 41
run_case enum_step 2
run_case enum_anon 2
run_case enum_unrestrict 8
run_case casts 42
run_case address_of_param 42
run_case string_literal 65
run_case string_param 42
run_case string_shared 104
run_case buffer_block 65
run_case buffer_copy 104
run_case buffer_param 90
run_case buffer_pointer 65
run_case buffer_deref_store 66
run_case buffer_return 81
run_case buffer_const 65
run_case buffer_const_zero 0
run_case buffer_index 66
run_case buffer_index_write 66
run_case buffer_convert_pointer 65
run_case buffer_restrict_cast 65
run_case buffer_restrict_index 131
run_case buffer_slice 87
run_case buffer_slice_chain 87
run_case buffer_cast_read 65
run_case buffer_cast_slice_read 68
run_case buffer_slice_write 90
run_case buffer_slice_compound 132
run_case buffer_data_init 65
run_case global_init_scalar 5
run_case pointer_index 67
run_case pointer_add 67
run_case pointer_scale 67
run_case pointer_distance 3
run_case pointer_increment 66
run_case pointer_compound 66
run_case integer_increment 65
run_case compound_assign 51
run_case index_compound 66
run_case deref_compound 66
run_case decimal_fixed_assignment 1
run_case decimal_fixed_add 2
run_case decimal_fixed_func 2
run_case decimal_float_assignment 2
run_case decimal_float_func 3
run_case decimal_cast 3
run_case const_scalar 42
run_case export_symbol 3 emux_entry_point

# Niveis de otimizacao: -O1 faz dead code elimination, -O2 soma o peephole de
# casts identidade. O teste chave e a preservacao de comportamento: os mesmos
# programas, compilados em -O2, tem de devolver os mesmos exit codes.
run_opt_dce
run_opt_import
run_opt_import_in_loop
run_opt_case casts 42 2
run_opt_case float_cast 42 2
run_opt_case using_types 42 2
run_opt_case address_of_param 42 2
run_opt_case compound_assign 51 2
run_opt_case integer_increment 65 2
run_opt_case pointer 42 2
run_opt_case buffer_pointer 65 2
run_opt_case buffer_slice 87 2
run_opt_case buffer_cast_read 65 2
run_opt_case buffer_slice_write 90 2
run_opt_case buffer_data_init 65 2
run_opt_case string_literal 65 2
run_opt_case decimal_fixed_assignment 1 2
run_opt_case decimal_cast 3 2
run_opt_case const_fold 6 2
run_opt_case mod_const 2 2
run_opt_case if_else 10 2
run_opt_case if_elseif 2 2
run_opt_case while_loop 15 2
run_opt_case for_loop 20 2
run_opt_case local_basic 36 2
run_opt_case local_recursion 120 2
run_opt_case local_buffer_arg 215 2
run_opt_case logical_shortcircuit 12 2
run_opt_case logical_loops 26 2
run_opt_case continue_for 20 2
run_opt_case nested_break 36 2
run_opt_case break_continue_mixed 19 2
run_opt_case block_cmp_equal 43 2
run_opt_case block_cmp_size 54 2
run_opt_case block_cmp_content 10 2
run_opt_case block_cmp_slice 30 2
run_opt_case block_cmp_local 3 2

run_project functions 42
run_project vars 5
run_project stl 42
run_project stl_io 69
run_project usings 42
run_project strings 65
run_project consts 65
run_incremental
run_subsystem
run_semicolons

# Literais de string iguais partilham um bloco 'data' e os rótulos dos dados
# ficam privados do módulo (não podem colidir entre módulos).
shared_asm="$(ls "$out"/string_shared.emux/asm/x86_64/*.asm 2>/dev/null | head -1)"
if [ -z "$shared_asm" ]; then
    echo "FAIL string_shared: assembly não gerado"
    fail=1
else
    blocks="$(grep -cE '^__emux_str[0-9]+:' "$shared_asm")"
    if [ "$blocks" -ne 1 ]; then
        echo "FAIL string_shared: esperado 1 bloco de dados, obtidos $blocks"
        fail=1
    elif grep -qE '^global[[:space:]]+__emux_str' "$shared_asm"; then
        echo "FAIL string_shared: rótulo de dados não deve ser global"
        fail=1
    else
        echo "ok   string_shared (interning e rótulo local)"
    fi
fi

# Blocos b: o .bss reserva o numero de bytes do tipo e a atribuicao de
# string usa 'copy' (rep movsb), nao um store escalar.
buffer_asm="$(ls "$out"/buffer_block.emux/asm/x86_64/*.asm 2>/dev/null | head -1)"
if [ -z "$buffer_asm" ]; then
    echo "FAIL buffer_block: assembly não gerado"
    fail=1
elif ! grep -qE '^    resb 8$' "$buffer_asm"; then
    echo "FAIL buffer_block: .bss sem 'resb 8'"
    fail=1
elif ! grep -qE '^    rep movsb$' "$buffer_asm"; then
    echo "FAIL buffer_block: copia de string sem 'rep movsb'"
    fail=1
else
    echo "ok   buffer_block (resb e rep movsb)"
fi

# Globais [[const]] ficam em .rdata, com os bytes do literal.
const_asm="$(ls "$out"/buffer_const.emux/asm/x86_64/*.asm 2>/dev/null | head -1)"
if [ -z "$const_asm" ]; then
    echo "FAIL buffer_const: assembly não gerado"
    fail=1
elif ! grep -qE '^section \.rdata$' "$const_asm"; then
    echo "FAIL buffer_const: falta a secção .rdata"
    fail=1
elif ! grep -qE '^    db 65, 66, 0, 0, 0, 0, 0, 0$' "$const_asm"; then
    echo "FAIL buffer_const: bytes iniciais ausentes em .rdata"
    fail=1
else
    echo "ok   buffer_const (.rdata com bytes iniciais)"
fi

# Enums: os elementos escalares sao constantes dobradas e nao ocupam memoria;
# so os blocos b viram globais em .rdata.
enum_asm="$(ls "$out"/enum_anon.emux/asm/x86_64/*.asm 2>/dev/null | head -1)"
if [ -z "$enum_asm" ]; then
    echo "FAIL enum_anon: assembly não gerado"
    fail=1
elif grep -qE '^main_Ok:' "$enum_asm"; then
    echo "FAIL enum_anon: elemento escalar não devia ocupar .rdata"
    fail=1
elif ! grep -qE '^    db 104, 105, 0, 0' "$enum_asm"; then
    echo "FAIL enum_anon: bloco b do enum ausente em .rdata"
    fail=1
else
    echo "ok   enum_anon (escalares dobrados, blocos b em .rdata)"
fi

run_case_x86 pointer 42 'call _main_Store' 'lea eax, \[_main_value\]' 'mov dword \[ecx\], eax'
run_case_x86 wide_math 42 'call __emux_idiv64' 'mov dword \[_main_b\+4\], edx'
run_case_x86 wide_params 42 'call _main_AddShift' 'add esp, 16' 'call __emux_idiv64'
run_case_x86 abi_local 42 'ret 4' 'ret 8' 'mov ecx, 10' 'mov edx, 20'
run_case_x86 abi_mixed 42 'call @main_Mix@8' 'ret 8' 'mov ecx, 7'
run_case_x86 shift64_left 0 'and ecx, 63'
run_case_x86 shift64_right 1 'and ecx, 63'
run_case_x86 mod_i32 32
run_case_x86 mod_i64 28
run_case_x86 subbyte_u24 0 'and eax, 16777215'
run_case_x86 subbyte_u33 0 'and edx, 1'
run_case_x86 subbyte_i40 255 'shl edx, cl' 'sar edx, cl'
run_case_x86 std_types 42
run_case_x86 casts 42
run_case_x86 string_literal 65
run_case_x86 buffer_block 65 'rep movsb' 'lea edi, \[_main_text\]'
run_case_x86 buffer_const 65 'section \.rdata' 'db 65, 66, 0, 0, 0, 0, 0, 0'
run_case_x86 local_ptr 7
run_case_x86 local_buffer_copy 32 'rep movsb' 'lea edi, \[ebp'
run_case_x86 local_buffer_zero 0 'rep stosb' 'lea edi, \[ebp'
run_case_x86 break_continue_mixed 19 'jmp \.Lctl'
run_case_x86 logical_shortcircuit 12 'je \.Lctl'
run_case_x86 block_cmp_equal 43 'repe cmpsb' 'cmovc eax, edx'
run_case_x86 native_abi 42 'extern _Sleep@4' 'call _Sleep@4' 'extern _GetLastError@0'
run_x86_asm native_fastcall 'extern @GetTickCount@0' 'call @GetTickCount@0'
run_x86_asm stl_x86 'extern _GetStdHandle@4' 'extern _WriteFile@20' 'extern _lstrlenA@4'
expect_compile_error undefined_function
expect_compile_error import_with_body
expect_compile_error big_literal_error 'excede 64 bits'
expect_compile_error bigint_arg_error 'excede 64 bits'
expect_compile_error wide_param_error 'nao sao suportados em parametros'
expect_compile_error wide_return_error 'nao sao suportados em retornos'
expect_compile_error wide_width_error 'Unknown or invalid type'
expect_compile_error fixed_width_error 'Unknown or invalid type'
expect_compile_error_x86 float_use_error 'x86_64'
expect_compile_error_x86 fixed_use_error 'x86_64'
expect_compile_error float_param_error 'a ABI nativa nao suporta float e ponto fixo em parametros'
expect_compile_error float_return_error 'a ABI nativa nao suporta float e ponto fixo em retornos'
expect_compile_error_x86 wide_x86_error 'x86_64'
expect_compile_error void_value_error 'Cannot assign a void expression'
expect_compile_error invalid_type_width 'Unknown or invalid type'
expect_compile_error unterminated_string 'Unterminated string literal.'
expect_compile_error duplicate_symbol_error 'already declared as a variable'
expect_compile_error export_collision_error 'is already defined by'
expect_compile_error entry_return_error 'must return an integer type'
expect_compile_error using_restrict_error 'Impossible conversion of Offset to Counter'
expect_compile_error bool_literal_error 'Impossible conversion of u8 to bool'
expect_compile_error builtin_redefine_error 'cannot redefine a built-in type'
expect_compile_error invalid_escape_error 'Invalid escape sequence'
expect_compile_error buffer_overflow_error 'does not fit in buffer'
expect_compile_error buffer_assign_error 'expected a string literal or another buffer'
expect_compile_error buffer_index_oob_error 'out of bounds'
expect_compile_error buffer_slice_oob_error 'Buffer slice is out of bounds'
expect_compile_error buffer_cast_read_oob_error 'bytes remaining'
expect_compile_error buffer_convert_size_error 'too small to convert'
expect_compile_error buffer_cast_size_error 'Invalid static_cast'
expect_compile_error buffer_restrict_assign_error 'declared [[restrict]]'
expect_compile_error buffer_restrict_arg_error 'declared [[restrict]]'
expect_compile_error buffer_restrict_return_error 'declared [[restrict]]'
expect_compile_error compound_assign_error 'Compound assignment'
expect_compile_error pointer_add_error 'Arithmetic operator requires numeric operands'
expect_compile_error increment_const_error 'declared [[const]]'
expect_compile_error decimal_int_error 'Decimal literal cannot be assigned'
expect_compile_error buffer_const_error 'declared [[const]]'
expect_compile_error const_reassign_error 'declared [[const]]'
expect_compile_error buffer_const_deref_error 'Cannot write through'
expect_compile_error buffer_const_init_error 'must be initialized with a string literal'
expect_compile_error const_fit_error 'not exactly representable'
expect_compile_error global_init_pointer_error 'cannot take an initializer'
expect_compile_error buffer_empty_view_error 'empty buffer view'
expect_compile_error enum_restrict_error 'Impossible conversion of Color to Counter'
expect_compile_error enum_duplicate_error "Enum element 'Red' is already defined."
expect_compile_error enum_no_value_error 'needs an explicit value'
expect_compile_error enum_overflow_error 'not exactly representable'
expect_compile_error enum_autoinc_error 'Auto-incremented value'
expect_compile_error enum_buffer_error 'must be a string literal'
expect_compile_error enum_buffer_fit_error 'does not fit in enum element'
expect_compile_error enum_type_error "Unknown or invalid type 'u4294967296' in enum."
expect_compile_error enum_redefine_error "Type 'main::Color' already defined."
expect_compile_error enum_step_value_error "[[step]] expects an integer or 'off'"
expect_compile_error enum_step_type_error 'has no auto-increment'
expect_compile_error enum_unsupported_error 'is not supported; use an integer'
expect_compile_error var_in_function_error "Use 'local' for variables inside a function"
expect_compile_error local_noframe_error 'is [[noframe]]; it cannot declare local'
expect_compile_error frame_on_var_error "it only applies to a 'local' declared inside a function"
expect_compile_error noframe_on_local_error '[[noframe]] is a function attribute'
expect_compile_error local_buffer_bounds_error 'Index 7 is out of bounds for a buffer of 4 bytes'
expect_compile_error local_const_buffer_write_error 'Cannot write through'
expect_compile_error frame_noframe_conflict_error 'cannot be both [[frame]] and [[noframe]]'
expect_compile_error local_export_error 'a local lives on the stack frame'
expect_compile_error logical_bad_operand_error 'operand of a logical operator must be an integer or a pointer'
expect_compile_error break_outside_loop_error "'break' is only allowed inside a 'while' or a 'for' loop"
expect_compile_error continue_outside_loop_error "'continue' is only allowed inside a 'while' or a 'for' loop"
expect_compile_error block_cmp_int_error 'Comparison requires two integers, two pointers or two blocks'
expect_compile_error_x86 enum_x86_error 'x86_64'

missing_log="$out/missing_file.log"
if "$emux" "$out/definitely_missing.ex" -o "$out/missing.exe" > "$missing_log" 2>&1; then
    echo "FAIL missing_file: ficheiro inexistente deveria falhar"
    fail=1
elif ! grep -qF "Input file not found" "$missing_log"; then
    echo "FAIL missing_file: diagnostico nao menciona o ficheiro de entrada"
    fail=1
else
    echo "ok   missing_file (diagnostico correto)"
fi

# A versao reportada tem de vir de version.json (via CMake -> Version.hpp).
version="$(sed -n 's/.*"compiler"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' "$root/version.json")"
object_format="$(sed -n 's/.*"object_format"[[:space:]]*:[[:space:]]*\([0-9][0-9]*\).*/\1/p' "$root/version.json")"
version_out="$("$emux" --version 2>&1)"

if printf '%s' "$version_out" | grep -qF "Emux $version (object format $object_format)"; then
    echo "ok   version ($version, object format $object_format)"
else
    echo "FAIL version: --version nao reporta '$version (object format $object_format)'"
    fail=1
fi

# A STL tem de acompanhar o executavel: o build copia stl/ para <dir do exe>/stl.
emux_dir="$(cd "$(dirname "$emux")" && pwd)"
if [ -f "$emux_dir/stl/std.ex" ]; then
    echo "ok   stl (junto ao emux.exe)"
else
    echo "FAIL stl: $emux_dir/stl/std.ex ausente - a STL deve ficar ao lado do emux.exe"
    fail=1
fi

exit $fail
