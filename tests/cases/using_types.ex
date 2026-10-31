[Main]

[[unrestrict]]
using Number = u32

using Counter = u32

var count : Counter
var plain : Number
var raw : u32

func Echo(u32 value) u32 {
    return value
}

func Start() i32 {
    count = 40
    plain = count + 2
    raw = plain
    return Echo(raw)
}
