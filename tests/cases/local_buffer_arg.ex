[Main]

func Third(u8* p) u8 {
    return p[2]
}

func Start() i32 {
    local buf : B32 = "hello, world"
    buf[31] = 7
    local sum : i32 = buf[31]
    sum = sum + Third(buf)
    local view : u8* = buf + 1
    if (view[0] == 101) {
        sum = sum + 100
    }
    return sum % 256
}
