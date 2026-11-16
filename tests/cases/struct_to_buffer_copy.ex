[Main]

[[packed]] struct Pair { a : u16, b : u16 }

// A conversao e uma COPIA: mutar o buffer nao altera a struct de origem.
global Start() i32 {
    local s : Pair = { 258, 513 }
    local buf : B4 = s
    buf[0] = 9
    return s.a
}