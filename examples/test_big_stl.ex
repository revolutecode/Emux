@include "std.ex"

[Main]

func Start() i32 {
    # --- 1. Teste de Matematica ---
    local p : i64 = math::pow(3, 4)      # 81
    local r : u64 = math::sqrt(81)       # 9
    local mx : i64 = math::max(p, 100)   # 100

    # --- 2. Teste de Memoria ---
    local buffer : u8*
    buffer = mem::alloc(32)
    if (buffer == null) {
        return 1
    }
    mem::set(buffer, 65, 5) # 5 'A's
    buffer[5] = 0

    local len_buf : usize = str::len(buffer) # 5

    # --- 3. Teste de Strings ---
    local num_str : B16
    local itoa_len : usize = str::itoa(42, &num_str, 16) # "42"

    local s1 : u8*
    s1 = "Hello"
    local s2 : u8*
    s2 = "Hello"
    local str_eq : bool = str::equal(s1, s2) # 1

    # --- 4. Teste de Caminhos ---
    local filepath : u8*
    filepath = "C:/Emux/workspace/output.txt"
    local base : u8*
    base = path::basename(filepath)
    local ext : u8*
    ext = path::extension(filepath)

    local is_txt : bool = path::has_extension(filepath, ".txt") # 1

    # --- 5. Teste de Arquivo ---
    local filename : u8*
    filename = "build/debug/stl_temp.txt"

    local msg : u8*
    msg = "Emux Exir v3 STL"
    local written : bool = file::write_all(filename, msg, 16)

    local read_buf : B32
    local read_bytes : u32 = file::read_all(filename, &read_buf, 32)
    local file_ok : bool = file::exists(filename)
    file::remove(filename)

    mem::free(buffer)

    # --- Verificacao Global ---
    if (r == 9 && mx == 100 && len_buf == 5 && itoa_len == 2 && str_eq == 1 && is_txt == 1 && written == 1 && file_ok == 1) {
        io::println("Todos os testes da STL passaram com sucesso!")
        return 42
    }

    return 2
}
