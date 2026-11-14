[Main]

global Start() i32 {
    local w : i128 = 5
    local f : f32 = 1.5
    local q : q16.8 = 0.5
    w = w + 3
    local s : f32 = f + 2.25
    local h : q16.8 = q + 1.25
    local total : i32 = static_cast<i32>(w)
    total = total + static_cast<i32>(s)
    total = total + static_cast<i32>(h)
    return total
}
