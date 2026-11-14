[Main]

[[unrestrict]]
using Number = u32

using Counter = u32

global count : Counter
global plain : Number
global raw : u32

global Echo(u32 value) u32 {
    return value
}

global Start() i32 {
    count = 40
    plain = count + 2
    raw = plain
    return Echo(raw)
}
