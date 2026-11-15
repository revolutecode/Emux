#include "std.ex"

[Main]

global Start() i32 {
    // itoa e abs com INT64_MIN.
    local text : B32
    local n : usize = std::itoa(-9223372036854775807 - 1, &text, 32)
    if (n != 20) {
        return 1
    }
    if (std::str_cmp(&text, "-9223372036854775808") != 0) {
        return 2
    }
    if (std::abs(-9223372036854775807 - 1) != -9223372036854775807 - 1) {
        return 3
    }
    if (std::abs(-42) != 42) {
        return 4
    }

    // join preserva o separador de p1.
    local path : B32
    local plen : usize = std::join(&path, 32, "C:/dir", "file")
    if (plen != 11) {
        return 5
    }
    if (std::str_cmp(&path, "C:/dir/file") != 0) {
        return 6
    }

    // mem::* com caminho de palavras e resto, em enderecos nao alinhados.
    local a : B40 = "abcdefghijklmnopqrst"
    local b : B40 = ""
    local c : B40 = ""

    std::copy(&b + 1, &a + 1, 20)
    if (std::equal(&a + 1, &b + 1, 20) != 1) {
        return 7
    }
    if (b[0] != 0) {
        return 8
    }

    std::set(&c + 1, 65, 20)
    if (c[0] != 0) {
        return 9
    }
    if (c[1] != 65) {
        return 10
    }
    if (c[20] != 65) {
        return 11
    }
    if (std::equal(&a + 1, &c + 1, 20) != 0) {
        return 12
    }

    return 42
}
