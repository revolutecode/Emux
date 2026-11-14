[Main]

local counter : i32 = 10
global multiplier : i32 = 3

local helper(i32 x) i32 {
    local temp : i32 = x * multiplier
    return temp + counter
}

global Start() i32 {
    local val : i32 = 2
    local res : i32 = helper(val)
    return res
}
