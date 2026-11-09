@include "std.ex"

[Main]

func Start() i32 {
    # 1. Testando math
    local m1 : i64 = math::min(10, 20)      # 10
    local m2 : i64 = math::max(10, 20)      # 20
    local c  : i64 = math::clamp(50, 0, 12) # 12
    local sq : u64 = math::sqrt(144)        # 12
    local pw : i64 = math::pow(2, 3)        # 8

    # 2. Testando str
    local s : u8*
    s = "Hello, Emux!"
    local tam : usize = str::len(s)         # 12

    local prefix : u8*
    prefix = "Hello"
    local sw : bool = str::starts_with(s, prefix) # 1

    # Verificacoes: 10 + 20 + 12 = 42
    if ((m1 + m2 + c) == 42 && sq == 12 && pw == 8 && tam == 12 && sw == 1) {
        return 42
    }

    return 1
}
