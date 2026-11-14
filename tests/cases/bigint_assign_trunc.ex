[Main]
global small : u8

global Start() i32 {
    small = 0x10000000000000000
    return small + 42
}
