[Main]
[[const]] var g : u128 = 48775891622776987783

func Start() i32 {
    return static_cast<i32>(g & 255)
}
