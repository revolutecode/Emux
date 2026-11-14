[Main]
[[const]] global g : u128 = 48775891622776987783

global Start() i32 {
    return static_cast<i32>(g & 255)
}
