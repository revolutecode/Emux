[Main]

[[packed]] struct Pair { a : u16, b : u16 }

// O buffer alvo tem de ser de tamanho igual ou superior ao da struct:
// B2 (2 bytes) nao cabe a struct de 4 bytes -> erro de compilacao.
global Start() i32 {
    local s : Pair = { 258, 513 }
    local buf : B2 = s
    return buf[0]
}