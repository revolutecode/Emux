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
run_case const_scalar 42
run_case export_symbol 3 emux_entry_point
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

run_case_x86 pointer 42 'call _main_Store' 'lea eax, \[_main_value\]' 'mov dword \[ecx\], eax'
run_case_x86 wide_math 42 'call __emux_idiv64' 'mov dword \[_main_b\+4\], edx'
run_case_x86 wide_params 42 'call _main_AddShift' 'add esp, 16' 'call __emux_idiv64'
run_case_x86 abi_local 42 'ret 4' 'ret 8' 'mov ecx, 10' 'mov edx, 20'
run_case_x86 abi_mixed 42 'call @main_Mix@8' 'ret 8' 'mov ecx, 7'
run_case_x86 shift64_left 0 'and ecx, 63'
run_case_x86 shift64_right 1 'and ecx, 63'
run_case_x86 mod_i32 32
run_case_x86 mod_i64 28
run_case_x86 subbyte_u24 0 'and edx, 255'
run_case_x86 subbyte_u33 0 'and edx, 1'
run_case_x86 subbyte_i40 255 'shl edx, cl' 'sar edx, cl'
run_case_x86 std_types 42
run_case_x86 casts 42
run_case_x86 string_literal 65
run_case_x86 buffer_block 65 'rep movsb' 'lea edi, \[_main_text\]'
run_case_x86 buffer_const 65 'section \.rdata' 'db 65, 66, 0, 0, 0, 0, 0, 0'
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
expect_compile_error float_use_error 'not supported yet'
expect_compile_error fixed_use_error 'not supported yet'
expect_compile_error float_param_error 'float e ponto fixo ainda nao tem codegen'
expect_compile_error float_return_error 'float e ponto fixo ainda nao tem codegen'
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
expect_compile_error buffer_const_error 'declared [[const]]'
expect_compile_error const_reassign_error 'declared [[const]]'
expect_compile_error buffer_const_deref_error 'Cannot write through'
expect_compile_error buffer_const_init_error 'must be initialized with a string literal'
expect_compile_error const_fit_error 'does not fit in const variable'
expect_compile_error init_requires_const_error 'is not declared [[const]]'

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

exit $fail
