[Main]

[[packed]] struct Pair { a : u16, b : u16 }

// Uma chamada que retorna a struct por valor tambem se converte para buffer.
global Make() Pair {
    local p : Pair = { 258, 513 }
    return p
}

global Start() i32 {
    local buf : B8 = Make()
    return buf[0] + buf[2]*4
}