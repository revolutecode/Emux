[Main]

[[packed]] struct Pair { a : u16, b : u16 }

// Conversao struct->buffer: copia o layout em bytes para um buffer maior.
// a=258 (0x0102) -> bytes 2,1 ; b=513 (0x0201) -> bytes 1,2 ; resto zero.
global Start() i32 {
    local s : Pair = { 258, 513 }
    local buf : B8 = s
    return buf[0] + buf[1]*2 + buf[2]*4 + buf[3]*8 + buf[4]*16
}