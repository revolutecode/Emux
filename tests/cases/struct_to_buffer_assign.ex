[Main]

[[packed]] struct Pair { a : u16, b : u16 }

// Atribuicao (nao so init) de uma struct a um buffer copia os bytes.
global Start() i32 {
    local s : Pair = { 258, 513 }
    local buf : B8
    buf = s
    return buf[1] + buf[3]*2
}