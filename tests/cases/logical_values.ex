[Main]

global Start() i32 {
    local a : i32 = 1
    local b : i32 = 0
    local c : i32 = 2

    local x : i32 = a != 0 && c > 1
    local y : i32 = b != 0 || c > 5
    local z : i32 = a != 0 && (b != 0 || c > 1)
    local w : i32 = b != 0 || a == 0 && c == 2

    # x=1, y=0, z=1, w=0  =>  1 + 0 + 4 + 0
    return x + y * 2 + z * 4 + w * 8
}
