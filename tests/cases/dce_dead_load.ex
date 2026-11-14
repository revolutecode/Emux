[Main]

global g : i32

global Start() i32 {
    g = 5
    static_cast<i32>(g)
    return g
}
