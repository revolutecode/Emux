@include "std.ex"

[Main]

global Start() i32 {
    # --- 1. Teste de Matematica ---
    local p : i64 = std::pow(3, 4)      # 81
    local r : u64 = std::sqrt(81)       # 9
    local mx : i64 = std::max(p, 100)   # 100

    # --- 2. Teste de Memoria ---
    local buffer : u8*
    buffer = std::alloc(32)
    if (buffer == null) {
        return 1
    }
    std::set(buffer, 65, 5) # 5 'A's
    buffer[5] = 0

    local len_buf : usize = std::len(buffer) # 5

    # --- 3. Teste de Strings ---
    local num_str : B16
    local itoa_len : usize = std::itoa(42, &num_str, 16) # "42"

    local s1 : u8*
    s1 = "Hello"
    local s2 : u8*
    s2 = "Hello"
    local str_eq : bool = std::str_equal(s1, s2) # 1

    # --- 4. Teste de Caminhos ---
    local filepath : u8*
    filepath = "C:/Emux/workspace/output.txt"
    local base : u8*
    base = std::basename(filepath)
    local ext : u8*
    ext = std::extension(filepath)

    local is_txt : bool = std::has_extension(filepath, ".txt") # 1

    # --- 5. Teste de Arquivo ---
    local filename : u8*
    filename = "build/debug/std_temp.txt"

    local msg : u8*
    msg = "Emux Exir v3 STD"
    local written : bool = std::write_all(filename, msg, 16)

    local read_buf : B32
    local read_bytes : u32 = std::read_all(filename, &read_buf, 32)
    local file_ok : bool = std::exists(filename)
    std::remove(filename)

    std::free(buffer)

    # --- Verificacao Global ---
    if (r == 9 && mx == 100 && len_buf == 5 && itoa_len == 2 && str_eq == 1 && is_txt == 1 && written == 1 && file_ok == 1) {
        std::println("Todos os testes da STD passaram com sucesso!")
        return 42
    }

    return 2
}
