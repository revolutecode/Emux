[Main]
var a : f32
var u : u32
var i : i32

func Start() i32 {
    a = 1
    u = reinterpret_cast<u32>(a)
    i = static_cast<i32>((u >> 23) & 255)
    return i - 85
}
