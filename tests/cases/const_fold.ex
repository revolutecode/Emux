[Main]

global x : i32
global y : i32

global Start() i32 {
    [[const]] x = 2
    y = x * 3
    return y
}
