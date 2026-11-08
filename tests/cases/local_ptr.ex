[Main]

func Start() i32 {
    local q : i32 = 1
    local p : i32* = &q
    *p = 7
    return *p
}
