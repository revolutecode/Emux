[Main]

global x : i32

global Start() i32 {
    [[const]] x = 1
    x = 2
    return x
}
